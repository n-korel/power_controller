/*
 * Unit tests: Fault manager — thresholds, consecutive counting, shutdown policy (Rules 7)
 *
 * Key requirements:
 *   - Faults are latched (no auto-reset) — Rules 7.1
 *   - RESET_FAULT clears flags but doesn't re-enable anything — Rules 7.1
 *   - 5 consecutive out-of-range measurements to confirm fault — Rules 5.3
 *   - Any in-range measurement resets the consecutive counter — Rules 5.3
 *   - Each fault type maps to specific domain shutdowns — Rules 7.3
 *
 * Source included directly for access to static state (counters, thresholds).
 */
#include "unity.h"
#include "config.h"

/* --- Mocks for fault_manager.c dependencies --- */
static uint16_t mock_voltage_mv[4];
static int16_t  mock_current_ma[5];
static uint8_t  mock_pgood = 1;
static uint8_t  mock_faultz = 1;
static uint8_t  mock_power_state;
static uint16_t mock_force_off_called_with;

uint16_t adc_get_voltage_mv(uint8_t idx) { return (idx < 4) ? mock_voltage_mv[idx] : 0; }
int16_t  adc_get_current_ma(uint8_t idx) { return (idx < 5) ? mock_current_ma[idx] : 0; }
uint8_t  input_get_pgood(void)           { return mock_pgood; }
uint8_t  input_get_faultz(void)          { return mock_faultz; }
uint8_t  power_get_state(void)           { return mock_power_state; }

void power_force_off_domains(uint16_t domain_mask)
{
    mock_force_off_called_with = domain_mask;
}

void power_emergency_display_off(void) {}

volatile uint32_t systick_ms;

#include "fault_manager.c"

void setUp(void)
{
    fault_manager_init();
    mock_force_off_called_with = 0;
    mock_pgood = 1;
    mock_faultz = 1;
    mock_power_state = 0;
    for (uint8_t i = 0; i < 4; i++) mock_voltage_mv[i] = 0;
    for (uint8_t i = 0; i < 5; i++) mock_current_ma[i] = 0;
}

void tearDown(void) {}

/* ===== Consecutive confirmation (Rules 5.3) ===== */

void test_voltage_fault_after_5_consecutive(void)
{
    /* V24 out of range for 5 consecutive calls → FAULT_V24_RANGE set */
    TEST_IGNORE_MESSAGE("TODO: set mock_voltage_mv[0]=30000, power_state=DOM_SCALER, call process() 5x, assert FAULT_V24_RANGE set");
}

void test_voltage_fault_reset_by_normal_reading(void)
{
    /* 4 out-of-range, then 1 normal → counter resets, no fault */
    TEST_IGNORE_MESSAGE("TODO: 4x bad, 1x good, 4x bad → still no fault (counter reset)");
}

void test_current_fault_after_5_consecutive(void)
{
    /* LCD current over threshold for 5 calls → FAULT_LCD */
    TEST_IGNORE_MESSAGE("TODO: set mock_current_ma[0]=3000, power_state=DOM_LCD, call 5x, assert FAULT_LCD");
}

void test_faultz_active_low_confirms_after_5(void)
{
    /* FAULTZ=0 (active low) for 5 calls when AUDIO on → FAULT_AMP_FAULTZ */
    TEST_IGNORE_MESSAGE("TODO: mock_faultz=0, power_state=DOM_AUDIO, call 5x, assert FAULT_AMP_FAULTZ");
}

void test_pgood_loss_confirms_after_5(void)
{
    /* PGOOD=0 with any domain on, 5 calls → FAULT_PGOOD_LOST */
    TEST_IGNORE_MESSAGE("TODO: mock_pgood=0, power_state!=0, call 5x, assert FAULT_PGOOD_LOST");
}

/* ===== Fault latching (Rules 7.1) ===== */

void test_fault_is_latched(void)
{
    /* Once fault is set, it stays even if readings return to normal */
    TEST_IGNORE_MESSAGE("TODO: trigger fault, restore normal readings, assert flag still set");
}

void test_reset_fault_clears_all_flags(void)
{
    /* fault_clear_flags() zeros fault_flags and all counters */
    TEST_IGNORE_MESSAGE("TODO: trigger faults, call fault_clear_flags(), assert fault_get_flags()==0");
}

void test_reset_fault_does_not_reenable_domains(void)
{
    /* After clear, power_state should NOT change — Rules 7.1 */
    TEST_IGNORE_MESSAGE("TODO: trigger fault (domains off), clear flags, verify power_force_off not called again and domains stay off");
}

/* ===== Shutdown policy (Rules 7.3) ===== */

void test_fault_scaler_shuts_scaler_lcd_bl(void)
{
    /* FAULT_SCALER → power_force_off(SCALER|LCD|BACKLIGHT) */
    TEST_IGNORE_MESSAGE("TODO: fault_set_flag(FAULT_SCALER), assert mock_force_off == DOM_SCALER|DOM_LCD|DOM_BACKLIGHT");
}

void test_fault_lcd_shuts_lcd_bl(void)
{
    /* FAULT_LCD → power_force_off(LCD|BACKLIGHT) */
    TEST_IGNORE_MESSAGE("TODO: fault_set_flag(FAULT_LCD), assert mock_force_off == DOM_LCD|DOM_BACKLIGHT");
}

void test_fault_backlight_shuts_bl_only(void)
{
    /* FAULT_BACKLIGHT → power_force_off(BACKLIGHT) */
    TEST_IGNORE_MESSAGE("TODO: fault_set_flag(FAULT_BACKLIGHT), assert mock_force_off == DOM_BACKLIGHT");
}

void test_fault_audio_shuts_audio(void)
{
    /* FAULT_AUDIO → power_force_off(AUDIO) */
    TEST_IGNORE_MESSAGE("TODO: fault_set_flag(FAULT_AUDIO), assert mock_force_off == DOM_AUDIO");
}

void test_fault_amp_faultz_shuts_audio(void)
{
    /* FAULT_AMP_FAULTZ → power_force_off(AUDIO) */
    TEST_IGNORE_MESSAGE("TODO: fault_set_flag(FAULT_AMP_FAULTZ), assert mock_force_off == DOM_AUDIO");
}

void test_fault_pgood_shuts_all_domains(void)
{
    /* FAULT_PGOOD_LOST → power_force_off(ALL domains) */
    TEST_IGNORE_MESSAGE("TODO: fault_set_flag(FAULT_PGOOD_LOST), assert mock_force_off includes all 7 domain bits");
}

void test_fault_v24_range_shuts_all_domains(void)
{
    TEST_IGNORE_MESSAGE("TODO: fault_set_flag(FAULT_V24_RANGE), assert all domains shut down");
}

void test_fault_v12_range_shuts_all_domains(void)
{
    TEST_IGNORE_MESSAGE("TODO: fault_set_flag(FAULT_V12_RANGE), assert all domains shut down");
}

void test_fault_seq_abort_shuts_display(void)
{
    /* FAULT_SEQ_ABORT → SCALER+LCD+BACKLIGHT off */
    TEST_IGNORE_MESSAGE("TODO: fault_set_flag(FAULT_SEQ_ABORT), assert mock_force_off == DOM_SCALER|DOM_LCD|DOM_BACKLIGHT");
}

void test_fault_internal_shuts_all(void)
{
    /* FAULT_INTERNAL → all domains */
    TEST_IGNORE_MESSAGE("TODO: fault_set_flag(FAULT_INTERNAL), assert all domains shut down");
}

/* ===== Threshold update ===== */

void test_set_voltage_threshold(void)
{
    /* fault_set_threshold(0, min, max) updates v24 thresholds */
    TEST_IGNORE_MESSAGE("TODO: call fault_set_threshold(0, 19000, 27000), verify new thresholds used in process()");
}

void test_set_current_threshold(void)
{
    /* fault_set_threshold(4, 0, max) updates LCD current threshold */
    TEST_IGNORE_MESSAGE("TODO: call fault_set_threshold(4, 0, 2500), verify new max used");
}

/* ===== Conditional checks ===== */

void test_voltage_not_checked_when_all_off(void)
{
    /* If power_state == 0, voltage thresholds not evaluated */
    TEST_IGNORE_MESSAGE("TODO: power_state=0, set bad voltages, call 10x, verify no fault");
}

void test_current_only_checked_for_active_domain(void)
{
    /* LCD current not checked if DOM_LCD not in power_state */
    TEST_IGNORE_MESSAGE("TODO: power_state=DOM_SCALER (no LCD), mock bad LCD current, call 10x, no FAULT_LCD");
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    /* Consecutive */
    RUN_TEST(test_voltage_fault_after_5_consecutive);
    RUN_TEST(test_voltage_fault_reset_by_normal_reading);
    RUN_TEST(test_current_fault_after_5_consecutive);
    RUN_TEST(test_faultz_active_low_confirms_after_5);
    RUN_TEST(test_pgood_loss_confirms_after_5);
    /* Latching */
    RUN_TEST(test_fault_is_latched);
    RUN_TEST(test_reset_fault_clears_all_flags);
    RUN_TEST(test_reset_fault_does_not_reenable_domains);
    /* Policy */
    RUN_TEST(test_fault_scaler_shuts_scaler_lcd_bl);
    RUN_TEST(test_fault_lcd_shuts_lcd_bl);
    RUN_TEST(test_fault_backlight_shuts_bl_only);
    RUN_TEST(test_fault_audio_shuts_audio);
    RUN_TEST(test_fault_amp_faultz_shuts_audio);
    RUN_TEST(test_fault_pgood_shuts_all_domains);
    RUN_TEST(test_fault_v24_range_shuts_all_domains);
    RUN_TEST(test_fault_v12_range_shuts_all_domains);
    RUN_TEST(test_fault_seq_abort_shuts_display);
    RUN_TEST(test_fault_internal_shuts_all);
    /* Thresholds */
    RUN_TEST(test_set_voltage_threshold);
    RUN_TEST(test_set_current_threshold);
    /* Conditional */
    RUN_TEST(test_voltage_not_checked_when_all_off);
    RUN_TEST(test_current_only_checked_for_active_domain);
    return UNITY_END();
}
