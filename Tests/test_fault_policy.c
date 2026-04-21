/*
 * Unit tests: Fault manager — thresholds, consecutive counting, shutdown policy (Rules 7)
 *
 * Key requirements:
 *   - Faults are latched (no auto-reset) — Rules 7.1
 *   - RESET_FAULT clears flags but doesn't re-enable anything — Rules 7.1
 *   - 5 consecutive out-of-range measurements to confirm fault — Rules 5.3
 *   - Any in-range measurement resets the consecutive counter — Rules 5.3
 *   - Each fault type maps to specific domain shutdowns — Rules 7.3
 */
#include "unity.h"
#include "config.h"

static uint16_t mock_voltage_mv[4];
static int16_t  mock_current_ma[5];
static uint8_t  mock_pgood = 1;
static uint8_t  mock_faultz = 1;
static uint8_t  mock_power_state;
static uint16_t mock_force_off_called_with;
static uint8_t  mock_force_off_call_count;

uint16_t adc_get_voltage_mv(uint8_t idx) { return (idx < 4) ? mock_voltage_mv[idx] : 0; }
int16_t  adc_get_current_ma(uint8_t idx) { return (idx < 5) ? mock_current_ma[idx] : 0; }
uint8_t  input_get_pgood(void)           { return mock_pgood; }
uint8_t  input_get_faultz(void)          { return mock_faultz; }
uint8_t  power_get_state(void)           { return mock_power_state; }

void power_force_off_domains(uint16_t domain_mask)
{
    mock_force_off_called_with = domain_mask;
    mock_force_off_call_count++;
}

void power_emergency_display_off(void) {}

volatile uint32_t systick_ms;

#include "fault_manager.c"

#define ALL_DOMAINS  (DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | DOM_AUDIO | \
                      DOM_ETH1 | DOM_ETH2 | DOM_TOUCH)

static void set_v_nominal(void)
{
    mock_voltage_mv[0] = 24000;
    mock_voltage_mv[1] = 12000;
    mock_voltage_mv[2] = 5000;
    mock_voltage_mv[3] = 3300;
}

void setUp(void)
{
    fault_manager_init();
    mock_force_off_called_with = 0;
    mock_force_off_call_count  = 0;
    mock_pgood = 1;
    mock_faultz = 1;
    mock_power_state = 0;
    set_v_nominal();
    for (uint8_t i = 0; i < 5; i++) mock_current_ma[i] = 0;
}

void tearDown(void) {}

/* ===== Consecutive confirmation (Rules 5.3) ===== */

void test_voltage_fault_after_5_consecutive(void)
{
    mock_power_state    = DOM_SCALER;
    mock_voltage_mv[0]  = 30000; /* out of max=26000 */

    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT - 1; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_V24_RANGE);
    }
    fault_manager_process();

    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_V24_RANGE);
}

void test_voltage_fault_reset_by_normal_reading(void)
{
    mock_power_state   = DOM_SCALER;
    mock_voltage_mv[0] = 30000;

    for (uint8_t i = 0; i < 4; i++) fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_V24_RANGE);

    mock_voltage_mv[0] = 24000;
    fault_manager_process();

    mock_voltage_mv[0] = 30000;
    for (uint8_t i = 0; i < 4; i++) fault_manager_process();

    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_V24_RANGE);
}

void test_current_fault_after_5_consecutive(void)
{
    mock_power_state   = DOM_LCD;
    mock_current_ma[0] = 3000; /* > THRESH_I_LCD_MAX=2000 */

    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT - 1; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_LCD);
    }
    fault_manager_process();

    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_LCD);
}

void test_faultz_active_low_confirms_after_5(void)
{
    mock_power_state = DOM_AUDIO;
    mock_faultz = 0;

    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT - 1; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_AMP_FAULTZ);
    }
    fault_manager_process();

    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_AMP_FAULTZ);
}

void test_pgood_loss_confirms_after_5(void)
{
    mock_power_state = DOM_SCALER;
    mock_pgood = 0;

    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT - 1; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_PGOOD_LOST);
    }
    fault_manager_process();

    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_PGOOD_LOST);
}

/* ===== Fault latching (Rules 7.1) ===== */

void test_fault_is_latched(void)
{
    mock_power_state   = DOM_SCALER;
    mock_voltage_mv[0] = 30000;
    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT; i++) fault_manager_process();
    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_V24_RANGE);

    set_v_nominal();
    for (uint8_t i = 0; i < 20; i++) fault_manager_process();

    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_V24_RANGE);
}

void test_reset_fault_clears_all_flags(void)
{
    fault_set_flag(FAULT_SCALER);
    fault_set_flag(FAULT_AUDIO);
    TEST_ASSERT_NOT_EQUAL(0, fault_get_flags());

    fault_clear_flags();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags());
}

void test_reset_fault_does_not_reenable_domains(void)
{
    mock_power_state   = DOM_SCALER;
    mock_voltage_mv[0] = 30000;
    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT; i++) fault_manager_process();
    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_V24_RANGE);

    uint8_t call_count_before = mock_force_off_call_count;
    fault_clear_flags();

    TEST_ASSERT_EQUAL_UINT8(call_count_before, mock_force_off_call_count);
}

void test_multiple_fault_reasons_or_aggregate_in_same_cycle_after_confirmation(void)
{
    /* Two independent fault conditions held simultaneously should latch both bits. */
    mock_power_state   = DOM_SCALER;
    mock_voltage_mv[0] = 30000; /* V24 out-of-range */
    mock_pgood = 0;             /* PGOOD lost */

    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT; i++)
        fault_manager_process();

    uint16_t flags = fault_get_flags();
    TEST_ASSERT_TRUE(flags & FAULT_V24_RANGE);
    TEST_ASSERT_TRUE(flags & FAULT_PGOOD_LOST);
}

void test_reset_fault_clears_flags_but_never_auto_enables_domains_even_if_measurements_normal(void)
{
    mock_power_state   = DOM_SCALER;
    mock_voltage_mv[0] = 30000;
    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT; i++) fault_manager_process();
    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_V24_RANGE);

    mock_power_state = 0;
    set_v_nominal();
    for (uint8_t i = 0; i < 20; i++) fault_manager_process();
    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_V24_RANGE);

    fault_clear_flags();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags());

    /* With all measurements nominal, the fault manager must not turn anything back on. */
    for (uint8_t i = 0; i < 50; i++) fault_manager_process();
    TEST_ASSERT_EQUAL_UINT8(0, mock_power_state);
}

void test_voltage_threshold_boundaries_at_min_and_max_do_not_fault(void)
{
    mock_power_state = DOM_SCALER;

    mock_voltage_mv[0] = THRESH_V24_MIN;
    for (uint8_t i = 0; i < 20; i++) fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_V24_RANGE);

    mock_voltage_mv[0] = THRESH_V24_MAX;
    for (uint8_t i = 0; i < 20; i++) fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_V24_RANGE);

    mock_voltage_mv[0] = THRESH_V24_MAX + 1;
    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT; i++) fault_manager_process();
    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_V24_RANGE);
}

/* ===== Shutdown policy (Rules 7.3) ===== */

void test_fault_scaler_shuts_scaler_lcd_bl(void)
{
    fault_set_flag(FAULT_SCALER);
    TEST_ASSERT_EQUAL_HEX16(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT,
                            mock_force_off_called_with);
}

void test_fault_lcd_shuts_lcd_bl(void)
{
    fault_set_flag(FAULT_LCD);
    TEST_ASSERT_EQUAL_HEX16(DOM_LCD | DOM_BACKLIGHT, mock_force_off_called_with);
}

void test_fault_backlight_shuts_bl_only(void)
{
    fault_set_flag(FAULT_BACKLIGHT);
    TEST_ASSERT_EQUAL_HEX16(DOM_BACKLIGHT, mock_force_off_called_with);
}

void test_fault_audio_shuts_audio(void)
{
    fault_set_flag(FAULT_AUDIO);
    TEST_ASSERT_EQUAL_HEX16(DOM_AUDIO, mock_force_off_called_with);
}

void test_fault_amp_faultz_shuts_audio(void)
{
    fault_set_flag(FAULT_AMP_FAULTZ);
    TEST_ASSERT_EQUAL_HEX16(DOM_AUDIO, mock_force_off_called_with);
}

void test_fault_pgood_shuts_all_domains(void)
{
    fault_set_flag(FAULT_PGOOD_LOST);
    TEST_ASSERT_EQUAL_HEX16(ALL_DOMAINS, mock_force_off_called_with);
}

void test_fault_v24_range_shuts_all_domains(void)
{
    fault_set_flag(FAULT_V24_RANGE);
    TEST_ASSERT_EQUAL_HEX16(ALL_DOMAINS, mock_force_off_called_with);
}

void test_fault_v12_range_shuts_all_domains(void)
{
    fault_set_flag(FAULT_V12_RANGE);
    TEST_ASSERT_EQUAL_HEX16(ALL_DOMAINS, mock_force_off_called_with);
}

void test_fault_seq_abort_shuts_display(void)
{
    fault_set_flag(FAULT_SEQ_ABORT);
    TEST_ASSERT_EQUAL_HEX16(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT,
                            mock_force_off_called_with);
}

void test_fault_internal_shuts_all(void)
{
    fault_set_flag(FAULT_INTERNAL);
    TEST_ASSERT_EQUAL_HEX16(ALL_DOMAINS, mock_force_off_called_with);
}

/* ===== Threshold update ===== */

void test_set_voltage_threshold(void)
{
    fault_set_threshold(0, 19000, 27000);

    mock_power_state   = DOM_SCALER;
    mock_voltage_mv[0] = 28000;
    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT; i++) fault_manager_process();
    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_V24_RANGE);

    fault_clear_flags();
    mock_voltage_mv[0] = 20000;
    for (uint8_t i = 0; i < 20; i++) fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_V24_RANGE);
}

void test_set_current_threshold(void)
{
    fault_set_threshold(4, 0, 2500);

    mock_power_state   = DOM_LCD;
    mock_current_ma[0] = 2400;
    for (uint8_t i = 0; i < 20; i++) fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_LCD);

    mock_current_ma[0] = 2600;
    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT; i++) fault_manager_process();
    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_LCD);
}

/* ===== Threshold input validation (fault_set_threshold contract) ===== */

void test_set_threshold_idx_out_of_range_is_noop(void)
{
    /* idx >= 9 must not corrupt any threshold array. Defaults for V24 stay in effect. */
    fault_set_threshold(9,   1, 2);
    fault_set_threshold(10,  1, 2);
    fault_set_threshold(255, 1, 2);

    mock_power_state   = DOM_SCALER;
    mock_voltage_mv[0] = 24000; /* nominal, within default 20000..26000 */
    for (uint8_t i = 0; i < 20; i++) fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags());

    mock_voltage_mv[0] = 30000; /* > default max */
    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT; i++) fault_manager_process();
    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_V24_RANGE);
}

void test_set_threshold_min_ge_max_makes_range_always_invalid(void)
{
    /* fault_manager does not validate min<max (that's the protocol layer's job).
       With inverted range (min > max), every reading will be "out of range" and
       confirm a fault after FAULT_CONFIRM_COUNT cycles. */
    fault_set_threshold(0, 27000, 24000);

    mock_power_state   = DOM_SCALER;
    mock_voltage_mv[0] = 25000; /* would be valid under defaults */
    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT; i++) fault_manager_process();
    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_V24_RANGE);
}

/* ===== ETH/TOUCH fault policy (Rules 7.3) ===== */

void test_fault_touch_latched_with_domain_shutdown(void)
{
    /* FAULT_TOUCH must be latched and force TOUCH domain shutdown. */
    fault_set_flag(FAULT_TOUCH);

    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_TOUCH);
    TEST_ASSERT_EQUAL_UINT8(1, mock_force_off_call_count);
    TEST_ASSERT_EQUAL_HEX16(DOM_TOUCH, mock_force_off_called_with);
}

void test_fault_eth1_eth2_latched_with_domain_shutdown(void)
{
    fault_set_flag(FAULT_ETH1);
    TEST_ASSERT_EQUAL_UINT8(1, mock_force_off_call_count);
    TEST_ASSERT_EQUAL_HEX16(DOM_ETH1, mock_force_off_called_with);

    fault_set_flag(FAULT_ETH2);

    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_ETH1);
    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_ETH2);
    TEST_ASSERT_EQUAL_UINT8(2, mock_force_off_call_count);
    TEST_ASSERT_EQUAL_HEX16(DOM_ETH2, mock_force_off_called_with);
}

/* ===== Current threshold boundary (strict `>` check) ===== */

void test_current_at_and_below_threshold_no_fault(void)
{
    /* Check is strict `>`: value == threshold and 95% of threshold must not confirm. */
    mock_power_state = DOM_LCD;

    mock_current_ma[0] = (int16_t)THRESH_I_LCD_MAX; /* exactly 100% */
    for (uint8_t i = 0; i < 20; i++) fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_LCD);

    mock_current_ma[0] = (int16_t)(THRESH_I_LCD_MAX * 95 / 100); /* 95% */
    for (uint8_t i = 0; i < 20; i++) fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_LCD);

    mock_current_ma[0] = (int16_t)THRESH_I_LCD_MAX + 1; /* just above */
    for (uint8_t i = 0; i < FAULT_CONFIRM_COUNT; i++) fault_manager_process();
    TEST_ASSERT_TRUE(fault_get_flags() & FAULT_LCD);
}

/* ===== Conditional checks ===== */

void test_voltage_not_checked_when_all_off(void)
{
    mock_power_state   = 0;
    mock_voltage_mv[0] = 30000;
    mock_voltage_mv[1] = 0;

    for (uint8_t i = 0; i < 10; i++) fault_manager_process();

    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags());
}

void test_current_only_checked_for_active_domain(void)
{
    mock_power_state   = DOM_SCALER;
    mock_current_ma[0] = 3000;

    for (uint8_t i = 0; i < 10; i++) fault_manager_process();

    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_LCD);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_voltage_fault_after_5_consecutive);
    RUN_TEST(test_voltage_fault_reset_by_normal_reading);
    RUN_TEST(test_current_fault_after_5_consecutive);
    RUN_TEST(test_faultz_active_low_confirms_after_5);
    RUN_TEST(test_pgood_loss_confirms_after_5);
    RUN_TEST(test_fault_is_latched);
    RUN_TEST(test_reset_fault_clears_all_flags);
    RUN_TEST(test_reset_fault_does_not_reenable_domains);
    RUN_TEST(test_multiple_fault_reasons_or_aggregate_in_same_cycle_after_confirmation);
    RUN_TEST(test_reset_fault_clears_flags_but_never_auto_enables_domains_even_if_measurements_normal);
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
    RUN_TEST(test_set_voltage_threshold);
    RUN_TEST(test_set_current_threshold);
    RUN_TEST(test_set_threshold_idx_out_of_range_is_noop);
    RUN_TEST(test_set_threshold_min_ge_max_makes_range_always_invalid);
    RUN_TEST(test_fault_touch_latched_with_domain_shutdown);
    RUN_TEST(test_fault_eth1_eth2_latched_with_domain_shutdown);
    RUN_TEST(test_current_at_and_below_threshold_no_fault);
    RUN_TEST(test_voltage_not_checked_when_all_off);
    RUN_TEST(test_current_only_checked_for_active_domain);
    RUN_TEST(test_voltage_threshold_boundaries_at_min_and_max_do_not_fault);
    return UNITY_END();
}
