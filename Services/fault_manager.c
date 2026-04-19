#include "fault_manager.h"
#include "config.h"
#include "adc_service.h"
#include "input_service.h"
#include "power_manager.h"

/* ===== Latched fault flags (Rules 7.1) ===== */
static volatile uint16_t fault_flags;

/* ===== Voltage thresholds [4]: v24, v12, v5, v3v3 ===== */
static uint16_t v_thresh_min[4];
static uint16_t v_thresh_max[4];

/* ===== Current thresholds [5]: lcd, bl, scaler, audio_l, audio_r ===== */
static uint16_t i_thresh_max[5];

/* ===== Consecutive counters (Rules 5.3) ===== */
static uint8_t v_consec[4];
static uint8_t i_consec[5];
static uint8_t faultz_consec;
static uint8_t pgood_consec;

static uint8_t current_exceeds_threshold(uint8_t ch)
{
    int16_t current_ma = adc_get_current_ma(ch);
    int32_t threshold_ma = (int32_t)i_thresh_max[ch];

    if (current_ma <= 0) {
        return 0;
    }

    return ((int32_t)current_ma > threshold_ma) ? 1U : 0U;
}

/* ===== Fault -> shutdown policy (Rules 7.3) ===== */
static void apply_fault_policy(uint16_t flag)
{
    fault_flags |= flag;

    switch (flag) {
    case FAULT_SCALER:
    case FAULT_SEQ_ABORT:
        power_force_off_domains(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT);
        break;

    case FAULT_LCD:
        power_force_off_domains(DOM_LCD | DOM_BACKLIGHT);
        break;

    case FAULT_BACKLIGHT:
        power_force_off_domains(DOM_BACKLIGHT);
        break;

    case FAULT_AUDIO:
    case FAULT_AMP_FAULTZ:
        power_force_off_domains(DOM_AUDIO);
        break;

    case FAULT_PGOOD_LOST:
    case FAULT_V24_RANGE:
    case FAULT_V12_RANGE:
    case FAULT_V5_RANGE:
    case FAULT_V3V3_RANGE:
    case FAULT_INTERNAL:
        power_force_off_domains(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT |
                                DOM_AUDIO | DOM_ETH1 | DOM_ETH2 | DOM_TOUCH);
        break;

    default:
        break;
    }
}

/* ===== Init ===== */
void fault_manager_init(void)
{
    fault_flags = 0;

    v_thresh_min[0] = THRESH_V24_MIN;   v_thresh_max[0] = THRESH_V24_MAX;
    v_thresh_min[1] = THRESH_V12_MIN;   v_thresh_max[1] = THRESH_V12_MAX;
    v_thresh_min[2] = THRESH_V5_MIN;    v_thresh_max[2] = THRESH_V5_MAX;
    v_thresh_min[3] = THRESH_V3V3_MIN;  v_thresh_max[3] = THRESH_V3V3_MAX;

    i_thresh_max[0] = THRESH_I_LCD_MAX;
    i_thresh_max[1] = THRESH_I_BL_MAX;
    i_thresh_max[2] = THRESH_I_SCALER_MAX;
    i_thresh_max[3] = THRESH_I_AUDIO_LR_MAX;
    i_thresh_max[4] = THRESH_I_AUDIO_LR_MAX;

    for (uint8_t i = 0; i < 4; i++) v_consec[i] = 0;
    for (uint8_t i = 0; i < 5; i++) i_consec[i] = 0;
    faultz_consec = 0;
    pgood_consec  = 0;
}

/* ===== Process (called every main loop iteration) =====
 * NOTE: pstate is re-read before every independent block because
 * apply_fault_policy -> power_force_off_domains mutates power_state mid-run.
 * A stale snapshot could latch spurious downstream faults (e.g. FAULT_LCD
 * after FAULT_V24_RANGE already de-powered the LCD sensors). Rules §7.1
 * forbids auto-reset of latched faults, so a single ghost flag is permanent.
 */
void fault_manager_process(void)
{
    uint8_t pstate = power_get_state();

    /* --- Voltage checks (only when any power domain is on) --- */
    if (pstate) {
        static const uint16_t v_fault_bits[4] = {
            FAULT_V24_RANGE, FAULT_V12_RANGE, FAULT_V5_RANGE, FAULT_V3V3_RANGE
        };
        for (uint8_t i = 0; i < 4; i++) {
            uint16_t val = adc_get_voltage_mv(i);
            if (val < v_thresh_min[i] || val > v_thresh_max[i]) {
                v_consec[i]++;
                if (v_consec[i] >= FAULT_CONFIRM_COUNT) {
                    apply_fault_policy(v_fault_bits[i]);
                    v_consec[i] = 0;
                }
            } else {
                v_consec[i] = 0;
            }
        }
    }

    /* --- Current checks (signed comparison: negative = sensor below offset, not a fault) --- */
    /* LCD current — check only if LCD domain on */
    pstate = power_get_state();
    if (pstate & DOM_LCD) {
        if (current_exceeds_threshold(0)) {
            i_consec[0]++;
            if (i_consec[0] >= FAULT_CONFIRM_COUNT) {
                apply_fault_policy(FAULT_LCD);
                i_consec[0] = 0;
            }
        } else {
            i_consec[0] = 0;
        }
    }

    /* Backlight current */
    pstate = power_get_state();
    if (pstate & DOM_BACKLIGHT) {
        if (current_exceeds_threshold(1)) {
            i_consec[1]++;
            if (i_consec[1] >= FAULT_CONFIRM_COUNT) {
                apply_fault_policy(FAULT_BACKLIGHT);
                i_consec[1] = 0;
            }
        } else {
            i_consec[1] = 0;
        }
    }

    /* Scaler current */
    pstate = power_get_state();
    if (pstate & DOM_SCALER) {
        if (current_exceeds_threshold(2)) {
            i_consec[2]++;
            if (i_consec[2] >= FAULT_CONFIRM_COUNT) {
                apply_fault_policy(FAULT_SCALER);
                i_consec[2] = 0;
            }
        } else {
            i_consec[2] = 0;
        }
    }

    /* Audio L/R current */
    pstate = power_get_state();
    if (pstate & DOM_AUDIO) {
        for (uint8_t ch = 3; ch < 5; ch++) {
            if (current_exceeds_threshold(ch)) {
                i_consec[ch]++;
                if (i_consec[ch] >= FAULT_CONFIRM_COUNT) {
                    apply_fault_policy(FAULT_AUDIO);
                    i_consec[ch] = 0;
                }
            } else {
                i_consec[ch] = 0;
            }
        }
    }

    /* --- Faultz input (active LOW from TPA3118, only when AUDIO on) --- */
    pstate = power_get_state();
    if (pstate & DOM_AUDIO) {
        if (!input_get_faultz()) {
            faultz_consec++;
            if (faultz_consec >= FAULT_CONFIRM_COUNT) {
                apply_fault_policy(FAULT_AMP_FAULTZ);
                faultz_consec = 0;
            }
        } else {
            faultz_consec = 0;
        }
    } else {
        faultz_consec = 0;
    }

    /* --- PGOOD loss --- */
    pstate = power_get_state();
    if (!input_get_pgood() && pstate) {
        pgood_consec++;
        if (pgood_consec >= FAULT_CONFIRM_COUNT) {
            apply_fault_policy(FAULT_PGOOD_LOST);
            pgood_consec = 0;
        }
    } else {
        pgood_consec = 0;
    }
}

/* ===== Public API ===== */
uint16_t fault_get_flags(void)
{
    return fault_flags;
}

void fault_clear_flags(void)
{
    fault_flags = 0;
    for (uint8_t i = 0; i < 4; i++) v_consec[i] = 0;
    for (uint8_t i = 0; i < 5; i++) i_consec[i] = 0;
    faultz_consec = 0;
    pgood_consec  = 0;
}

void fault_set_flag(uint16_t flag)
{
    apply_fault_policy(flag);
}

void fault_set_threshold(uint8_t idx, uint16_t min_val, uint16_t max_val)
{
    /* idx 0..3 = voltage thresholds, 4..8 = current max (min ignored for current) */
    if (idx < 4) {
        v_thresh_min[idx] = min_val;
        v_thresh_max[idx] = max_val;
    } else if (idx < 9) {
        i_thresh_max[idx - 4] = max_val;
    }
}
