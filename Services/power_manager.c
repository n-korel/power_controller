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

/* ===== Bridge reset SM ===== */
static uint8_t  bridge_rst_active;
static uint32_t bridge_rst_timer;

/* ===== SUS_S3# auto-start Linux (Rules 8) ===== */
static uint32_t sus_low_since;
static uint8_t  sus_low_tracking;
static uint8_t  pwrbtn_active;
static uint32_t pwrbtn_timer;
static uint32_t sus_cooldown_ts;

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
    bridge_rst_active = 0;
    sus_low_tracking  = 0;
    pwrbtn_active     = 0;
    sus_cooldown_ts   = 0;
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
    __HAL_TIM_SET_COMPARE(&htim17, TIM_CHANNEL_1, 0);

    power_state    = 0;
    brightness_pwm = 0;
    dseq           = DSEQ_IDLE;
    aseq           = ASEQ_IDLE;
}

/* ===== Emergency display off (no delays, Rules 6.2) ===== */
void power_emergency_display_off(void)
{
    __HAL_TIM_SET_COMPARE(&htim17, TIM_CHANNEL_1, 0);
    HAL_GPIO_WritePin(BACKLIGHT_ON_GPIO_Port, BACKLIGHT_ON_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_POWER_ON_GPIO_Port, LCD_POWER_ON_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RST_CH7511B_GPIO_Port,  RST_CH7511B_Pin,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SCALER_POWER_ON_GPIO_Port, SCALER_POWER_ON_Pin, GPIO_PIN_RESET);

    power_state &= (uint8_t)~(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT);
    brightness_pwm = 0;
    dseq = DSEQ_IDLE;
}

/* ===== Display sequencing SM ===== */
static void dseq_process(void)
{
    uint32_t now = systick_ms;

    /* PGOOD check during any active UP sequence (Rules 6.2) */
    if (dseq != DSEQ_IDLE && dseq < DSEQ_DN_PWM_ZERO) {
        if (!input_get_pgood()) {
            power_emergency_display_off();
            fault_set_flag(FAULT_PGOOD_LOST);
            fault_set_flag(FAULT_SEQ_ABORT);
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
        uint32_t adc_mv = (uint32_t)adc_get_raw_avg(ADC_IDX_SCALER_POWER) * ADC_VREF_MV / ADC_RESOLUTION;
        uint32_t vin_mv = adc_mv * VDIV_MULT / VDIV_DIV;
        if (vin_mv >= SEQ_VERIFY_SCALER_MV) {
            dseq = DSEQ_UP_RST_RELEASE;
        } else if ((now - dseq_timer) >= SEQ_VERIFY_TIMEOUT) {
            power_emergency_display_off();
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
        uint32_t adc_mv = (uint32_t)adc_get_raw_avg(ADC_IDX_LCD_POWER) * ADC_VREF_MV / ADC_RESOLUTION;
        uint32_t vin_mv = adc_mv * VDIV_MULT / VDIV_DIV;
        if (vin_mv >= SEQ_VERIFY_LCD_MV) {
            if (dseq_up_with_bl)
                dseq = DSEQ_UP_BL_ON;
            else
                dseq = DSEQ_UP_DONE;
        } else if ((now - dseq_timer) >= SEQ_VERIFY_TIMEOUT) {
            power_emergency_display_off();
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
        uint32_t adc_mv = (uint32_t)adc_get_raw_avg(ADC_IDX_BL_POWER) * ADC_VREF_MV / ADC_RESOLUTION;
        uint32_t vin_mv = adc_mv * VDIV_MULT / VDIV_DIV;
        if (vin_mv >= SEQ_VERIFY_BL_MV) {
            dseq = DSEQ_UP_DONE;
        } else if ((now - dseq_timer) >= SEQ_VERIFY_TIMEOUT) {
            power_emergency_display_off();
            fault_set_flag(FAULT_SEQ_ABORT);
        }
        break;
    }

    case DSEQ_UP_DONE:
        dseq = DSEQ_IDLE;
        break;

    /* === DOWN === */
    case DSEQ_DN_PWM_ZERO:
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
    if (power_state & DOM_BACKLIGHT)
        __HAL_TIM_SET_COMPARE(&htim17, TIM_CHANNEL_1, pwm);
}

void power_reset_bridge(void)
{
    if (!(power_state & DOM_SCALER) || !(power_state & DOM_LCD)) return;
    HAL_GPIO_WritePin(RST_CH7511B_GPIO_Port, RST_CH7511B_Pin, GPIO_PIN_RESET);
    bridge_rst_active = 1;
    bridge_rst_timer  = systick_ms;
}

uint8_t power_ctrl_request(uint16_t mask, uint16_t value)
{
    /* Compute desired future state for validation */
    uint8_t future = (power_state & ~(uint8_t)mask) | (uint8_t)(value & mask);

    /* BACKLIGHT ON requires both SCALER and LCD ON (Rules 4.5) */
    if ((future & DOM_BACKLIGHT) && (!(future & DOM_SCALER) || !(future & DOM_LCD)))
        return 1;

    /* LCD ON without SCALER is forbidden (Rules 13.7) */
    if ((mask & DOM_LCD) && (value & DOM_LCD) && !(future & DOM_SCALER))
        return 1;

    /* Sequencer must be idle to accept display commands */
    uint8_t disp_mask = mask & (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT);
    if (disp_mask && dseq != DSEQ_IDLE)
        return 1;

    /* Simple domains (ETH1, ETH2, TOUCH) — direct control */
    uint8_t simple_doms[] = { DOM_ETH1, DOM_ETH2, DOM_TOUCH };
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t dom = simple_doms[i];
        if (!(mask & dom)) continue;
        uint8_t on = (value & dom) ? 1 : 0;
        gpio_domain_set(dom, on);
        if (on) power_state |= dom;
        else    power_state &= ~dom;
    }

    /* Display domains — via sequencing SM (Rules 13.7) */
    if (disp_mask) {
        uint8_t want_scaler_off = (mask & DOM_SCALER) && !(value & DOM_SCALER);
        uint8_t want_lcd_off    = (mask & DOM_LCD)     && !(value & DOM_LCD);
        uint8_t want_bl_off     = (mask & DOM_BACKLIGHT) && !(value & DOM_BACKLIGHT);
        uint8_t want_bl_on      = (mask & DOM_BACKLIGHT) && (value & DOM_BACKLIGHT);

        if (want_scaler_off || want_lcd_off) {
            /* Turning off SCALER or LCD: full shutdown sequencing first */
            dseq = DSEQ_DN_PWM_ZERO;
        } else if (want_bl_off && (power_state & DOM_BACKLIGHT)) {
            /* BL-only off with 10ms delay (Rules 13.7) */
            dseq = DSEQ_BLOFF_PWM_ZERO;
        } else if ((mask & DOM_SCALER) && (value & DOM_SCALER) && !(power_state & DOM_SCALER)) {
            /* SCALER ON (from OFF): full UP sequencing */
            dseq_up_with_bl = want_bl_on ? 1 : 0;
            dseq = DSEQ_UP_SCALER_ON;
        } else if ((mask & DOM_LCD) && (value & DOM_LCD) &&
                   (power_state & DOM_SCALER) && !(power_state & DOM_LCD)) {
            /* LCD ON when SCALER already ON: partial sequencing (Rules 13.7) */
            dseq_up_with_bl = want_bl_on ? 1 : 0;
            dseq = DSEQ_UP_RST_RELEASE;
        } else if (want_bl_on &&
                   (power_state & DOM_SCALER) && (power_state & DOM_LCD) &&
                   !(power_state & DOM_BACKLIGHT)) {
            /* BL-only ON when SCALER+LCD already on */
            dseq = DSEQ_UP_BL_ON;
        }
    }

    /* Audio — via sequencing SM */
    if ((mask & DOM_AUDIO) && aseq == ASEQ_IDLE) {
        if ((value & DOM_AUDIO) && !(power_state & DOM_AUDIO))
            aseq = ASEQ_ON_POWER;
        else if (!(value & DOM_AUDIO) && (power_state & DOM_AUDIO))
            aseq = ASEQ_OFF_MUTE;
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

void power_auto_startup(void)
{
    /* SCALER + LCD (no BL), TOUCH, AUDIO (safe mode) per Rules 6.5 */
    dseq_up_with_bl = 0;
    dseq = DSEQ_UP_SCALER_ON;

    /* TOUCH on directly */
    gpio_domain_set(DOM_TOUCH, 1);
    power_state |= DOM_TOUCH;

    /* AUDIO power on, but SDZ=0, MUTE=1 (safe) */
    gpio_domain_set(DOM_AUDIO, 1);
    power_state |= DOM_AUDIO;
    /* SDZ and MUTE already in safe state from init */
}

/* ===== Main-loop process ===== */
void power_manager_process(void)
{
    dseq_process();
    aseq_process();
    bridge_rst_process();
    sus_s3_process();
}
