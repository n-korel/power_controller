#include "power_manager.h"
#include "config.h"
#include "main.h"
#include "tim.h"
#include "adc_service.h"
#include "input_service.h"
#include "fault_manager.h"

/* ===== Domain state (bitmask mirrors DOM_* in config.h) ===== */
static uint8_t power_state;
static uint16_t brightness_pwm;

/* ===== Display sequencing SM (Rules 6, 13) ===== */
typedef enum {
    DSEQ_IDLE,
    /* Full UP: SCALER → RST → LCD → (optional BL) */
    DSEQ_UP_SCALER_ON,
    DSEQ_UP_WAIT_SCALER,
    DSEQ_UP_VERIFY_SCALER,
    DSEQ_UP_RST_RELEASE,
    DSEQ_UP_WAIT_RST,
    DSEQ_UP_LCD_ON,
    DSEQ_UP_WAIT_LCD,
    DSEQ_UP_VERIFY_LCD,
    DSEQ_UP_BL_ON,
    DSEQ_UP_VERIFY_BL,
    DSEQ_UP_DONE,
    /* Full DOWN: PWM=0 → BL OFF → LCD OFF → RST LOW → SCALER OFF */
    DSEQ_DN_PWM_ZERO,
    DSEQ_DN_WAIT_PWM,
    DSEQ_DN_BL_OFF,
    DSEQ_DN_WAIT_BL,
    DSEQ_DN_LCD_OFF,
    DSEQ_DN_WAIT_LCD,
    DSEQ_DN_RST_ASSERT,
    DSEQ_DN_SCALER_OFF,
    DSEQ_DN_DONE,
    /* BL-only OFF: PWM=0 → wait 10ms → BL GPIO OFF (Rules 13.7) */
    DSEQ_BLOFF_PWM_ZERO,
    DSEQ_BLOFF_WAIT,
    DSEQ_BLOFF_GPIO,
    DSEQ_BLOFF_DONE,
} dseq_state_t;

static dseq_state_t dseq;
static uint32_t     dseq_timer;
static uint8_t      dseq_up_with_bl; /* start BL during UP? */

/* ===== Audio SM (Rules 9) ===== */
typedef enum {
    ASEQ_IDLE,
    ASEQ_ON_POWER,
    ASEQ_ON_WAIT_SDZ,
    ASEQ_ON_SDZ,
    ASEQ_ON_WAIT_MUTE,
    ASEQ_ON_DONE,
    ASEQ_OFF_MUTE,
    ASEQ_OFF_WAIT,
    ASEQ_OFF_SDZ,
    ASEQ_OFF_POWER,
    ASEQ_OFF_DONE,
} aseq_state_t;

static aseq_state_t aseq;
static uint32_t     aseq_timer;
/* 1 when amplifier fully active (SDZ=1, MUTE=0). 0 when safe (SDZ=0, MUTE=1).
 * Used together with (power_state & DOM_AUDIO) to distinguish the
 * auto-startup "safe-on" state (Rules 6.5: POWER_AUDIO=1 but amp safe)
 * from the full-on state entered after ASEQ_ON_DONE. */
static uint8_t      amp_active;

/* ===== Bridge reset SM ===== */
static uint8_t  bridge_rst_active;
static uint32_t bridge_rst_timer;

/* ===== Startup SM: non-blocking PGOOD wait (Rules 6.5, §12) =====
 * Replaces the blocking pre-main-loop wait so HAL_IWDG_Refresh stays in
 * exactly one place (the end of main loop). */
typedef enum {
    STARTUP_IDLE,
    STARTUP_WAIT_PGOOD,
} sseq_state_t;

static sseq_state_t sseq;
static uint32_t     sseq_timer;

/* ===== SUS_S3# auto-start Linux (Rules 8) ===== */
static uint32_t sus_low_since;
static uint8_t  sus_low_tracking;
static uint8_t  pwrbtn_active;
static uint32_t pwrbtn_timer;
static uint32_t sus_cooldown_ts;

static void power_auto_startup(void);

/* ===== GPIO helpers ===== */
static void gpio_domain_set(uint8_t dom, uint8_t on)
{
    GPIO_PinState st = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    switch (dom) {
    case DOM_SCALER:    HAL_GPIO_WritePin(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, st); break;
    case DOM_LCD:       HAL_GPIO_WritePin(LCD_POWER_ON_GPIO_Port,    LCD_POWER_ON_Pin,    st); break;
    case DOM_BACKLIGHT: HAL_GPIO_WritePin(BACKLIGHT_ON_GPIO_Port,    BACKLIGHT_ON_Pin,    st); break;
    case DOM_AUDIO:     HAL_GPIO_WritePin(POWER_AUDIO_GPIO_Port,     POWER_AUDIO_Pin,     st); break;
    case DOM_ETH1:      HAL_GPIO_WritePin(POWER_ETH1_GPIO_Port,      POWER_ETH1_Pin,      st); break;
    case DOM_ETH2:      HAL_GPIO_WritePin(POWER_ETH2_GPIO_Port,      POWER_ETH2_Pin,      st); break;
    case DOM_TOUCH:     HAL_GPIO_WritePin(POWER_TOUCH_GPIO_Port,     POWER_TOUCH_Pin,     st); break;
    default: break;
    }
}

/* ===== Init ===== */
void power_manager_init(void)
{
    power_state    = 0;
    brightness_pwm = 0;
    dseq           = DSEQ_IDLE;
    aseq           = ASEQ_IDLE;
    amp_active     = 0;
    bridge_rst_active = 0;
    sus_low_tracking  = 0;
    pwrbtn_active     = 0;
    sus_cooldown_ts   = 0;
    sseq              = STARTUP_IDLE;
    sseq_timer        = 0;
}

void power_startup_begin(void)
{
    sseq       = STARTUP_WAIT_PGOOD;
    sseq_timer = systick_ms;
}

/* ===== Safe state (Rules 3.2) ===== */
void power_safe_state(void)
{
    /* All domains OFF */
    gpio_domain_set(DOM_SCALER, 0);
    gpio_domain_set(DOM_LCD, 0);
    gpio_domain_set(DOM_BACKLIGHT, 0);
    gpio_domain_set(DOM_AUDIO, 0);
    gpio_domain_set(DOM_ETH1, 0);
    gpio_domain_set(DOM_ETH2, 0);
    gpio_domain_set(DOM_TOUCH, 0);

    /* Amplifier safe */
    HAL_GPIO_WritePin(SDZ_GPIO_Port,  SDZ_Pin,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MUTE_GPIO_Port, MUTE_Pin, GPIO_PIN_SET);

    /* OD release */
    HAL_GPIO_WritePin(PWRBTN_GPIO_Port, PWRBTN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(RSTBTN_GPIO_Port, RSTBTN_Pin, GPIO_PIN_SET);

    /* RST_CH7511b hold LOW (default before sequencing) */
    HAL_GPIO_WritePin(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, GPIO_PIN_RESET);

    /* PWM = 0 */
    /* cppcheck-suppress duplicateValueTernary ; HAL macro expands to channel ternary */
    __HAL_TIM_SET_COMPARE(&htim17, TIM_CHANNEL_1, 0);

    power_state    = 0;
    brightness_pwm = 0;
    dseq           = DSEQ_IDLE;
    aseq           = ASEQ_IDLE;
    sseq           = STARTUP_IDLE;
    amp_active     = 0;
}

/* ===== Emergency display off (no delays, Rules 6.2) ===== */
void power_emergency_display_off(void)
{
    /* cppcheck-suppress duplicateValueTernary ; HAL macro expands to channel ternary */
    __HAL_TIM_SET_COMPARE(&htim17, TIM_CHANNEL_1, 0);
    HAL_GPIO_WritePin(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RST_CH7511B_GPIO_Port,  RST_CH7511B_Pin,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, GPIO_PIN_RESET);

    power_state &= (uint8_t)~(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT);
    brightness_pwm = 0;
    dseq = DSEQ_IDLE;
    bridge_rst_active = 0;
}

/* ===== Display sequencing SM ===== */
static void dseq_process(void)
{
    uint32_t now = systick_ms;

    /* PGOOD check during any active UP sequence (Rules 6.2).
     * Shutdown of the display rails is driven through the fault policy:
     * fault_set_flag(FAULT_PGOOD_LOST) -> power_force_off_domains(ALL)
     * already calls power_emergency_display_off() internally. */
    if (dseq != DSEQ_IDLE && dseq < DSEQ_DN_PWM_ZERO) {
        if (!input_get_pgood()) {
            fault_set_flag(FAULT_PGOOD_LOST | FAULT_SEQ_ABORT);
            return;
        }
    }

    switch (dseq) {
    case DSEQ_IDLE:
        break;

    /* === UP === */
    case DSEQ_UP_SCALER_ON:
        gpio_domain_set(DOM_SCALER, 1);
        power_state |= DOM_SCALER;
        dseq_timer = now;
        dseq = DSEQ_UP_WAIT_SCALER;
        break;

    case DSEQ_UP_WAIT_SCALER:
        if ((now - dseq_timer) >= SEQ_DELAY_SCALER_ON) {
            dseq_timer = now;
            dseq = DSEQ_UP_VERIFY_SCALER;
        }
        break;

    case DSEQ_UP_VERIFY_SCALER: {
        uint32_t vin_mv = ADC_RAIL_MV_FROM_RAW(adc_get_raw_avg(ADC_IDX_SCALER_POWER));
        if (vin_mv >= SEQ_VERIFY_SCALER_MV) {
            dseq = DSEQ_UP_RST_RELEASE;
        } else if ((now - dseq_timer) >= SEQ_VERIFY_TIMEOUT) {
            fault_set_flag(FAULT_SEQ_ABORT);
        }
        break;
    }

    case DSEQ_UP_RST_RELEASE:
        HAL_GPIO_WritePin(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, GPIO_PIN_SET);
        dseq_timer = now;
        dseq = DSEQ_UP_WAIT_RST;
        break;

    case DSEQ_UP_WAIT_RST:
        if ((now - dseq_timer) >= SEQ_DELAY_RST_RELEASE)
            dseq = DSEQ_UP_LCD_ON;
        break;

    case DSEQ_UP_LCD_ON:
        gpio_domain_set(DOM_LCD, 1);
        power_state |= DOM_LCD;
        dseq_timer = now;
        dseq = DSEQ_UP_WAIT_LCD;
        break;

    case DSEQ_UP_WAIT_LCD:
        if ((now - dseq_timer) >= SEQ_DELAY_LCD_ON) {
            dseq_timer = now;
            dseq = DSEQ_UP_VERIFY_LCD;
        }
        break;

    case DSEQ_UP_VERIFY_LCD: {
        uint32_t vin_mv = ADC_RAIL_MV_FROM_RAW(adc_get_raw_avg(ADC_IDX_LCD_POWER));
        if (vin_mv >= SEQ_VERIFY_LCD_MV) {
            if (dseq_up_with_bl)
                dseq = DSEQ_UP_BL_ON;
            else
                dseq = DSEQ_UP_DONE;
        } else if ((now - dseq_timer) >= SEQ_VERIFY_TIMEOUT) {
            fault_set_flag(FAULT_SEQ_ABORT);
        }
        break;
    }

    case DSEQ_UP_BL_ON:
        gpio_domain_set(DOM_BACKLIGHT, 1);
        power_state |= DOM_BACKLIGHT;
        HAL_TIM_PWM_Start(&htim17, TIM_CHANNEL_1);
        __HAL_TIM_SET_COMPARE(&htim17, TIM_CHANNEL_1, brightness_pwm);
        dseq_timer = now;
        dseq = DSEQ_UP_VERIFY_BL;
        break;

    case DSEQ_UP_VERIFY_BL: {
        /* BACKLIGHT_POWER_M uses the same divider as SCALER/LCD (R169..R182, 4.99k/470).
         * ~12V at the rail -> ~1033mV at ADC input, restored via VDIV_MULT/VDIV_DIV.
         * SEQ_VERIFY_BL_MV=9000 is compared against the restored rail voltage. */
        uint32_t vin_mv = ADC_RAIL_MV_FROM_RAW(adc_get_raw_avg(ADC_IDX_BL_POWER));
        if (vin_mv >= SEQ_VERIFY_BL_MV) {
            dseq = DSEQ_UP_DONE;
        } else if ((now - dseq_timer) >= SEQ_VERIFY_TIMEOUT) {
            fault_set_flag(FAULT_SEQ_ABORT);
        }
        break;
    }

    case DSEQ_UP_DONE:
        dseq = DSEQ_IDLE;
        break;

    /* === DOWN === */
    case DSEQ_DN_PWM_ZERO:
        /* cppcheck-suppress duplicateValueTernary ; HAL macro expands to channel ternary */
        __HAL_TIM_SET_COMPARE(&htim17, TIM_CHANNEL_1, 0);
        brightness_pwm = 0;
        dseq_timer = now;
        dseq = DSEQ_DN_WAIT_PWM;
        break;

    case DSEQ_DN_WAIT_PWM:
        if ((now - dseq_timer) >= SEQ_DELAY_PWM_OFF)
            dseq = DSEQ_DN_BL_OFF;
        break;

    case DSEQ_DN_BL_OFF:
        gpio_domain_set(DOM_BACKLIGHT, 0);
        power_state &= (uint8_t)~DOM_BACKLIGHT;
        dseq_timer = now;
        dseq = DSEQ_DN_WAIT_BL;
        break;

    case DSEQ_DN_WAIT_BL:
        if ((now - dseq_timer) >= SEQ_DELAY_BL_OFF)
            dseq = DSEQ_DN_LCD_OFF;
        break;

    case DSEQ_DN_LCD_OFF:
        gpio_domain_set(DOM_LCD, 0);
        power_state &= (uint8_t)~DOM_LCD;
        dseq_timer = now;
        dseq = DSEQ_DN_WAIT_LCD;
        break;

    case DSEQ_DN_WAIT_LCD:
        if ((now - dseq_timer) >= SEQ_DELAY_LCD_OFF)
            dseq = DSEQ_DN_RST_ASSERT;
        break;

    case DSEQ_DN_RST_ASSERT:
        HAL_GPIO_WritePin(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, GPIO_PIN_RESET);
        dseq = DSEQ_DN_SCALER_OFF;
        break;

    case DSEQ_DN_SCALER_OFF:
        gpio_domain_set(DOM_SCALER, 0);
        power_state &= (uint8_t)~DOM_SCALER;
        dseq = DSEQ_DN_DONE;
        break;

    case DSEQ_DN_DONE:
        dseq = DSEQ_IDLE;
        break;

    /* === BL-only OFF (Rules 13.7) === */
    case DSEQ_BLOFF_PWM_ZERO:
        /* cppcheck-suppress duplicateValueTernary ; HAL macro expands to channel ternary */
        __HAL_TIM_SET_COMPARE(&htim17, TIM_CHANNEL_1, 0);
        brightness_pwm = 0;
        dseq_timer = now;
        dseq = DSEQ_BLOFF_WAIT;
        break;

    case DSEQ_BLOFF_WAIT:
        if ((now - dseq_timer) >= SEQ_DELAY_PWM_OFF)
            dseq = DSEQ_BLOFF_GPIO;
        break;

    case DSEQ_BLOFF_GPIO:
        gpio_domain_set(DOM_BACKLIGHT, 0);
        power_state &= (uint8_t)~DOM_BACKLIGHT;
        dseq = DSEQ_BLOFF_DONE;
        break;

    case DSEQ_BLOFF_DONE:
        dseq = DSEQ_IDLE;
        break;
    }
}

/* ===== Audio SM (Rules 9) ===== */
static void aseq_process(void)
{
    uint32_t now = systick_ms;

    switch (aseq) {
    case ASEQ_IDLE:
        break;

    /* ON */
    case ASEQ_ON_POWER:
        gpio_domain_set(DOM_AUDIO, 1);
        power_state |= DOM_AUDIO;
        aseq_timer = now;
        aseq = ASEQ_ON_WAIT_SDZ;
        break;

    case ASEQ_ON_WAIT_SDZ:
        if ((now - aseq_timer) >= AUDIO_SDZ_DELAY_MS)
            aseq = ASEQ_ON_SDZ;
        break;

    case ASEQ_ON_SDZ:
        HAL_GPIO_WritePin(SDZ_GPIO_Port, SDZ_Pin, GPIO_PIN_SET);
        aseq_timer = now;
        aseq = ASEQ_ON_WAIT_MUTE;
        break;

    case ASEQ_ON_WAIT_MUTE:
        if ((now - aseq_timer) >= AUDIO_MUTE_DELAY_MS)
            aseq = ASEQ_ON_DONE;
        break;

    case ASEQ_ON_DONE:
        HAL_GPIO_WritePin(MUTE_GPIO_Port, MUTE_Pin, GPIO_PIN_RESET);
        amp_active = 1;
        aseq = ASEQ_IDLE;
        break;

    /* OFF */
    case ASEQ_OFF_MUTE:
        HAL_GPIO_WritePin(MUTE_GPIO_Port, MUTE_Pin, GPIO_PIN_SET);
        aseq_timer = now;
        aseq = ASEQ_OFF_WAIT;
        break;

    case ASEQ_OFF_WAIT:
        if ((now - aseq_timer) >= AUDIO_MUTE_DELAY_MS)
            aseq = ASEQ_OFF_SDZ;
        break;

    case ASEQ_OFF_SDZ:
        HAL_GPIO_WritePin(SDZ_GPIO_Port, SDZ_Pin, GPIO_PIN_RESET);
        aseq = ASEQ_OFF_POWER;
        break;

    case ASEQ_OFF_POWER:
        gpio_domain_set(DOM_AUDIO, 0);
        power_state &= (uint8_t)~DOM_AUDIO;
        amp_active = 0;
        aseq = ASEQ_OFF_DONE;
        break;

    case ASEQ_OFF_DONE:
        aseq = ASEQ_IDLE;
        break;
    }
}

/* ===== Bridge reset SM ===== */
static void bridge_rst_process(void)
{
    if (!bridge_rst_active) return;

    if ((systick_ms - bridge_rst_timer) >= BRIDGE_RST_PULSE_MS) {
        HAL_GPIO_WritePin(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, GPIO_PIN_SET);
        bridge_rst_active = 0;
    }
}

/* ===== SUS_S3# + PWRBTN# (Rules 8) ===== */
static void sus_s3_process(void)
{
    uint32_t now = systick_ms;

    /* PWRBTN# pulse in progress */
    if (pwrbtn_active) {
        if ((now - pwrbtn_timer) >= PWRBTN_PULSE_MS) {
            HAL_GPIO_WritePin(PWRBTN_GPIO_Port, PWRBTN_Pin, GPIO_PIN_SET);
            pwrbtn_active = 0;
            sus_cooldown_ts = now;
        }
        return;
    }

    if (!input_get_pgood()) {
        sus_low_tracking = 0;
        return;
    }

    /* Cooldown */
    if (sus_cooldown_ts && (now - sus_cooldown_ts) < SUS_S3_COOLDOWN_MS)
        return;

    /* cppcheck-suppress knownConditionTrueFalse ; input_get_sus_s3() is a runtime input,
     *                                             cppcheck sees only the test mock value. */
    if (!input_get_sus_s3()) {
        /* SUS_S3# is LOW */
        if (!sus_low_tracking) {
            sus_low_tracking = 1;
            sus_low_since = now;
        } else if ((now - sus_low_since) >= SUS_S3_THRESHOLD_MS) {
            /* Fire PWRBTN# pulse */
            HAL_GPIO_WritePin(PWRBTN_GPIO_Port, PWRBTN_Pin, GPIO_PIN_RESET);
            pwrbtn_active = 1;
            pwrbtn_timer  = now;
            sus_low_tracking = 0;
        }
    } else {
        sus_low_tracking = 0;
    }
}

/* ===== Public API ===== */
uint8_t power_get_state(void)
{
    return power_state;
}

void power_set_brightness(uint16_t pwm)
{
    if (pwm > 1000) pwm = 1000;
    brightness_pwm = pwm;
    if ((power_state & DOM_BACKLIGHT) &&
        (dseq == DSEQ_IDLE ||
         (dseq >= DSEQ_UP_SCALER_ON && dseq <= DSEQ_UP_DONE)))
        __HAL_TIM_SET_COMPARE(&htim17, TIM_CHANNEL_1, pwm);
}

void power_reset_bridge(void)
{
    if (!(power_state & DOM_SCALER) || !(power_state & DOM_LCD)) return;
    /* Do not interfere with active display sequencing or an in-flight pulse */
    if (dseq != DSEQ_IDLE) return;
    if (bridge_rst_active) return;
    HAL_GPIO_WritePin(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, GPIO_PIN_RESET);
    bridge_rst_active = 1;
    bridge_rst_timer  = systick_ms;
}

uint8_t power_ctrl_request(uint16_t mask, uint16_t value)
{
    /* Reject any unknown bits in mask/value (Rules §4.5 / invariant list §11: domain bits 0..6 only). */
    const uint16_t valid = (uint16_t)(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | DOM_AUDIO |
                                      DOM_ETH1 | DOM_ETH2 | DOM_TOUCH);
    if (((mask | value) & (uint16_t)~valid) != 0U) {
        return 1;
    }

    /* Compute desired future state for validation */
    uint8_t future = (power_state & ~(uint8_t)mask) | (uint8_t)(value & mask);
    uint8_t disp_mask = mask & (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT);
    uint8_t next_dseq = DSEQ_IDLE;
    uint8_t apply_dseq = 0;
    uint8_t next_up_with_bl = dseq_up_with_bl;

    /* Rules §23: BACKLIGHT may be turned ON only if SCALER+LCD will also be ON.
     * Rules §24: SCALER=OFF / LCD=OFF while BL=ON must *not* be rejected here;
     * it triggers a proper shutdown sequence below (DSEQ_DN_*). So the guard
     * only applies when BL is being explicitly driven to ON. */
    if ((mask & DOM_BACKLIGHT) && (value & DOM_BACKLIGHT) &&
        (!(future & DOM_SCALER) || !(future & DOM_LCD)))
        return 1;

    /* LCD ON without SCALER is forbidden (Rules 13.7) */
    if ((mask & DOM_LCD) && (value & DOM_LCD) && !(future & DOM_SCALER))
        return 1;

    /* Display precheck: reject early to keep request application atomic.
     * No domain state is changed before all display-side failures are ruled out. */
    if (disp_mask) {
        uint8_t want_scaler_off = (mask & DOM_SCALER) && !(value & DOM_SCALER);
        uint8_t want_lcd_off    = (mask & DOM_LCD)     && !(value & DOM_LCD);
        uint8_t want_bl_off     = (mask & DOM_BACKLIGHT) && !(value & DOM_BACKLIGHT);
        uint8_t want_bl_on      = (mask & DOM_BACKLIGHT) && (value & DOM_BACKLIGHT);

        if (dseq != DSEQ_IDLE)
            return 1;

        if (want_scaler_off || want_lcd_off) {
            /* Turning off SCALER or LCD: full shutdown sequencing first */
            next_dseq = DSEQ_DN_PWM_ZERO;
            apply_dseq = 1;
        } else if (want_bl_off && (power_state & DOM_BACKLIGHT)) {
            /* BL-only off with 10ms delay (Rules 13.7) */
            next_dseq = DSEQ_BLOFF_PWM_ZERO;
            apply_dseq = 1;
        } else if ((mask & DOM_SCALER) && (value & DOM_SCALER) && !(power_state & DOM_SCALER)) {
            /* SCALER ON (from OFF): full UP sequencing */
            if (!input_get_pgood())
                return 1;
            next_up_with_bl = want_bl_on ? 1 : 0;
            next_dseq = DSEQ_UP_SCALER_ON;
            apply_dseq = 1;
        } else if ((mask & DOM_LCD) && (value & DOM_LCD) &&
                   (power_state & DOM_SCALER) && !(power_state & DOM_LCD)) {
            /* LCD ON when SCALER already ON: partial sequencing (Rules 13.7) */
            if (!input_get_pgood())
                return 1;
            next_up_with_bl = want_bl_on ? 1 : 0;
            next_dseq = DSEQ_UP_RST_RELEASE;
            apply_dseq = 1;
        } else if (want_bl_on &&
                   (power_state & DOM_SCALER) && (power_state & DOM_LCD) &&
                   !(power_state & DOM_BACKLIGHT)) {
            /* BL-only ON when SCALER+LCD already on */
            if (!input_get_pgood())
                return 1;
            next_dseq = DSEQ_UP_BL_ON;
            apply_dseq = 1;
        }
    }

    /* Simple domains (ETH1, ETH2, TOUCH) — direct control */
    static const uint8_t simple_doms[] = { DOM_ETH1, DOM_ETH2, DOM_TOUCH };
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t dom = simple_doms[i];
        if (!(mask & dom)) continue;
        uint8_t on = (value & dom) ? 1 : 0;
        gpio_domain_set(dom, on);
        if (on) power_state |= dom;
        else    power_state &= ~dom;
    }

    /* Audio — via sequencing SM (Rules §6.5, §9).
     * Three distinct start states are possible:
     *   OFF          (power_state=0, amp_active=0) -> full ON: POWER_AUDIO->SDZ->MUTE
     *   safe-on      (power_state=1, amp_active=0) -> partial ON: SDZ->MUTE only
     *                (entered by power_auto_startup per §6.5)
     *   full-on      (power_state=1, amp_active=1) -> already active, no-op */
    if (mask & DOM_AUDIO) {
        if (value & DOM_AUDIO) {
            if ((aseq == ASEQ_IDLE) && !amp_active) {
                aseq = (power_state & DOM_AUDIO) ? ASEQ_ON_SDZ : ASEQ_ON_POWER;
            }
        } else {
            /* AUDIO=OFF has priority over any in-flight audio startup sequence. */
            if (aseq != ASEQ_IDLE || (power_state & DOM_AUDIO)) {
                aseq = ASEQ_OFF_MUTE;
            }
        }
    }

    /* Display domains — via sequencing SM (Rules 13.7). */
    if (apply_dseq) {
        dseq_up_with_bl = next_up_with_bl;
        dseq = (dseq_state_t)next_dseq;
    }

    return 0;
}

void power_force_off_domains(uint16_t domain_mask)
{
    /* Audio safe shutdown */
    if (domain_mask & DOM_AUDIO) {
        HAL_GPIO_WritePin(MUTE_GPIO_Port, MUTE_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(SDZ_GPIO_Port,  SDZ_Pin,  GPIO_PIN_RESET);
        gpio_domain_set(DOM_AUDIO, 0);
        power_state &= (uint8_t)~DOM_AUDIO;
        aseq = ASEQ_IDLE;
        amp_active = 0;
    }

    /* Display emergency shutdown */
    if (domain_mask & (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT)) {
        power_emergency_display_off();
    }

    /* Simple domains */
    uint8_t simple_domains[] = { DOM_ETH1, DOM_ETH2, DOM_TOUCH };
    for (uint8_t i = 0; i < 3; i++) {
        if (domain_mask & simple_domains[i]) {
            gpio_domain_set(simple_domains[i], 0);
            power_state &= ~simple_domains[i];
        }
    }
}

static void power_auto_startup(void)
{
    /* Rules §6.5 / README §13.6 target state after PGOOD=HIGH:
     *   SCALER=ON, LCD=ON, BACKLIGHT=OFF, TOUCH=ON, AUDIO=ON (amp safe).
     * DOM_AUDIO bit mirrors POWER_AUDIO=1; SDZ=0/MUTE=1 are kept from init,
     * and amp_active stays 0 so POWER_CTRL AUDIO=ON from Q7 runs only the
     * partial SDZ->MUTE tail of ASEQ (Rules §9). */
    dseq_up_with_bl = 0;
    dseq = DSEQ_UP_SCALER_ON;

    gpio_domain_set(DOM_TOUCH, 1);
    power_state |= DOM_TOUCH;

    gpio_domain_set(DOM_AUDIO, 1);
    power_state |= DOM_AUDIO;
}

/* ===== Startup SM (Rules 6.5): non-blocking PGOOD wait + auto-start ===== */
static void sseq_process(void)
{
    if (sseq != STARTUP_WAIT_PGOOD) return;

    if (input_get_pgood()) {
        sseq = STARTUP_IDLE;
        power_auto_startup();
    } else if ((systick_ms - sseq_timer) >= PGOOD_TIMEOUT_MS) {
        sseq = STARTUP_IDLE;
        fault_set_flag(FAULT_PGOOD_LOST);
    }
}

/* ===== Main-loop process ===== */
void power_manager_process(void)
{
    sseq_process();
    dseq_process();
    aseq_process();
    bridge_rst_process();
    sus_s3_process();
}
