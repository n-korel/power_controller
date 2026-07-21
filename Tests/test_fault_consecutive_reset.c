/*
 * Unit tests: Rules_POWER.md invariant #41
 *
 *   41. Any in-range measurement resets that channel's consecutive confirmation counter.
 *
 * Also verifies counters are independent per channel (V12/V5/V3V3, currents 0..4,
 * faultz, pgood): resetting one must not clear another's accumulated count.
 */
#include "unity.h"
#include "config.h"

static uint16_t mock_voltage_mv[4];
static int16_t  mock_current_ma[5];
static uint8_t  mock_pgood = 1;
static uint8_t  mock_faultz = 1;
static uint8_t  mock_power_state;
static uint8_t  mock_audio_oc_armed = 1;
static uint16_t mock_force_off_called_with;
static uint8_t  mock_force_off_call_count;
static uint8_t  mock_safe_state_call_count;

uint16_t adc_get_voltage_mv(uint8_t idx) { return (idx < 4) ? mock_voltage_mv[idx] : 0; }
int16_t  adc_get_current_ma(uint8_t idx) { return (idx < 5) ? mock_current_ma[idx] : 0; }
uint8_t  adc_service_consume_new_sample(void) { return 1; }
uint8_t  input_get_pgood(void)           { return mock_pgood; }
uint8_t  input_get_faultz(void)          { return mock_faultz; }
uint8_t  power_get_state(void)           { return mock_power_state; }
uint8_t  power_audio_overcurrent_armed(void) { return mock_audio_oc_armed; }

void power_force_off_domains(uint16_t domain_mask)
{
    mock_force_off_called_with = domain_mask;
    mock_force_off_call_count++;
}

void power_emergency_display_off(void) {}
void power_safe_state(void)
{
    mock_safe_state_call_count++;
    power_force_off_domains(DOM_SCALER | DOM_LCD | DOM_BACKLIGHT | DOM_AUDIO |
                            DOM_TOUCH);
    mock_power_state = DOM_ETH1 | DOM_ETH2;
}

volatile uint32_t systick_ms;

#include "fault_manager.c"

static void set_v_nominal(void)
{
    mock_voltage_mv[0] = 24000;
    mock_voltage_mv[1] = 12000;
    mock_voltage_mv[2] = 5000;
    mock_voltage_mv[3] = 3300;
}

static void set_i_nominal(void)
{
    for (uint8_t i = 0; i < 5; i++) {
        mock_current_ma[i] = 0;
    }
}

void setUp(void)
{
    fault_manager_init();
    mock_force_off_called_with = 0;
    mock_force_off_call_count  = 0;
    mock_safe_state_call_count = 0;
    mock_pgood = 1;
    mock_faultz = 1;
    mock_power_state = 0;
    mock_audio_oc_armed = 1;
    set_v_nominal();
    set_i_nominal();
}

void tearDown(void) {}

static void assert_voltage_consecutive_reset(uint8_t vidx, uint16_t bad_mv, uint16_t fault_mask)
{
    mock_voltage_mv[vidx] = bad_mv;
    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & fault_mask);
    }

    set_v_nominal();
    fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & fault_mask);

    mock_voltage_mv[vidx] = bad_mv;
    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & fault_mask);
    }
}

static void assert_current_consecutive_reset(uint8_t ch, int16_t bad_ma, uint16_t fault_mask)
{
    mock_current_ma[ch] = bad_ma;
    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & fault_mask);
    }

    mock_current_ma[ch] = 0;
    fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & fault_mask);

    mock_current_ma[ch] = bad_ma;
    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & fault_mask);
    }
}

/*
 * Call after 4 cycles with both channels out-of-range and channel A restored in-range.
 * Independent counters: B confirms on the next cycle; A must not fault.
 */
static void assert_peer_confirms_after_local_reset(uint16_t fault_a, uint16_t fault_b)
{
    fault_manager_process();

    TEST_ASSERT_TRUE(fault_get_flags() & fault_b);
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & fault_a);
}

/* ===== Per-channel consecutive reset (invariant #41) ===== */

#if (ENABLE_V24_FAULT_CHECK != 0U)
void test_v24_consecutive_reset_by_in_range_sample(void)
{
    mock_power_state = DOM_SCALER;
    assert_voltage_consecutive_reset(0, THRESH_V24_MAX + 1, FAULT_V24_RANGE);
}
#endif

void test_v12_consecutive_reset_by_in_range_sample(void)
{
    mock_power_state = DOM_SCALER;
    assert_voltage_consecutive_reset(1, THRESH_V12_MAX + 1, FAULT_V12_RANGE);
}

void test_v5_consecutive_reset_by_in_range_sample(void)
{
    mock_power_state = DOM_SCALER;
    assert_voltage_consecutive_reset(2, THRESH_V5_MAX + 1, FAULT_V5_RANGE);
}

void test_v3v3_consecutive_reset_by_in_range_sample(void)
{
    mock_power_state = DOM_SCALER;
    assert_voltage_consecutive_reset(3, THRESH_V3V3_MAX + 1, FAULT_V3V3_RANGE);
}

void test_lcd_current_consecutive_reset_by_in_range_sample(void)
{
    mock_power_state = DOM_LCD;
    assert_current_consecutive_reset(0, (int16_t)THRESH_I_LCD_MAX + 1, FAULT_LCD);
}

void test_backlight_current_consecutive_reset_by_in_range_sample(void)
{
    mock_power_state = DOM_BACKLIGHT;
    assert_current_consecutive_reset(1, (int16_t)THRESH_I_BL_MAX + 1, FAULT_BACKLIGHT);
}

void test_scaler_current_consecutive_reset_by_in_range_sample(void)
{
    mock_power_state = DOM_SCALER;
    assert_current_consecutive_reset(2, (int16_t)THRESH_I_SCALER_MAX + 1, FAULT_SCALER);
}

void test_audio_l_current_consecutive_reset_by_in_range_sample(void)
{
    mock_power_state = DOM_AUDIO;
    assert_current_consecutive_reset(3, (int16_t)THRESH_I_AUDIO_LR_MAX + 1, FAULT_AUDIO);
}

void test_audio_r_current_consecutive_reset_by_in_range_sample(void)
{
    mock_power_state = DOM_AUDIO;
    assert_current_consecutive_reset(4, (int16_t)THRESH_I_AUDIO_LR_MAX + 1, FAULT_AUDIO);
}

void test_faultz_consecutive_reset_by_in_range_sample(void)
{
    mock_power_state = DOM_AUDIO;
    mock_faultz = 0;
    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_AMP_FAULTZ);
    }

    mock_faultz = 1;
    fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_AMP_FAULTZ);

    mock_faultz = 0;
    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_AMP_FAULTZ);
    }
}

void test_pgood_consecutive_reset_by_in_range_sample(void)
{
    mock_power_state = DOM_SCALER;
    mock_pgood = 0;
    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_PGOOD_LOST);
    }

    mock_pgood = 1;
    fault_manager_process();
    TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_PGOOD_LOST);

    mock_pgood = 0;
    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
        TEST_ASSERT_EQUAL_HEX16(0, fault_get_flags() & FAULT_PGOOD_LOST);
    }
}

/* ===== Counter independence (reset on A must not touch B) ===== */

#if (ENABLE_V24_FAULT_CHECK != 0U)
void test_v24_reset_does_not_clear_v12_consecutive_counter(void)
{
    mock_power_state   = DOM_SCALER;
    mock_voltage_mv[0] = THRESH_V24_MAX + 1;
    mock_voltage_mv[1] = THRESH_V12_MAX + 1;

    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
    }

    mock_voltage_mv[0] = 24000;
    assert_peer_confirms_after_local_reset(FAULT_V24_RANGE, FAULT_V12_RANGE);
}
#endif

void test_v5_reset_does_not_clear_v3v3_consecutive_counter(void)
{
    mock_power_state   = DOM_SCALER;
    mock_voltage_mv[2] = THRESH_V5_MAX + 1;
    mock_voltage_mv[3] = THRESH_V3V3_MAX + 1;

    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
    }

    mock_voltage_mv[2] = 5000;
    assert_peer_confirms_after_local_reset(FAULT_V5_RANGE, FAULT_V3V3_RANGE);
}

#if (ENABLE_BL_CURRENT_SENSOR != 0U)
void test_lcd_current_reset_does_not_clear_backlight_consecutive_counter(void)
{
    mock_power_state   = DOM_LCD | DOM_BACKLIGHT;
    mock_current_ma[0] = (int16_t)THRESH_I_LCD_MAX + 1;
    mock_current_ma[1] = (int16_t)THRESH_I_BL_MAX + 1;

    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
    }

    mock_current_ma[0] = 0;
    assert_peer_confirms_after_local_reset(FAULT_LCD, FAULT_BACKLIGHT);
}
#endif

void test_scaler_current_reset_does_not_clear_lcd_consecutive_counter(void)
{
    mock_power_state   = DOM_SCALER | DOM_LCD;
    mock_current_ma[2] = (int16_t)THRESH_I_SCALER_MAX + 1;
    mock_current_ma[0] = (int16_t)THRESH_I_LCD_MAX + 1;

    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
    }

    mock_current_ma[2] = 0;
    assert_peer_confirms_after_local_reset(FAULT_SCALER, FAULT_LCD);
}

void test_audio_l_reset_does_not_clear_audio_r_consecutive_counter(void)
{
    mock_power_state   = DOM_AUDIO;
    mock_current_ma[3] = (int16_t)THRESH_I_AUDIO_LR_MAX + 1;
    mock_current_ma[4] = (int16_t)THRESH_I_AUDIO_LR_MAX + 1;

    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
    }

    mock_current_ma[3] = 0;
    assert_peer_confirms_after_local_reset(0, FAULT_AUDIO);
}

void test_faultz_reset_does_not_clear_pgood_consecutive_counter(void)
{
    mock_power_state = DOM_AUDIO | DOM_SCALER;
    mock_faultz = 0;
    mock_pgood = 0;

    for (uint8_t i = 0; i < 4; i++) {
        fault_manager_process();
    }

    mock_faultz = 1;
    assert_peer_confirms_after_local_reset(FAULT_AMP_FAULTZ, FAULT_PGOOD_LOST);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
#if (ENABLE_V24_FAULT_CHECK != 0U)
    RUN_TEST(test_v24_consecutive_reset_by_in_range_sample);
#endif
    RUN_TEST(test_v12_consecutive_reset_by_in_range_sample);
    RUN_TEST(test_v5_consecutive_reset_by_in_range_sample);
    RUN_TEST(test_v3v3_consecutive_reset_by_in_range_sample);
    RUN_TEST(test_lcd_current_consecutive_reset_by_in_range_sample);
    RUN_TEST(test_backlight_current_consecutive_reset_by_in_range_sample);
    RUN_TEST(test_scaler_current_consecutive_reset_by_in_range_sample);
    RUN_TEST(test_audio_l_current_consecutive_reset_by_in_range_sample);
    RUN_TEST(test_audio_r_current_consecutive_reset_by_in_range_sample);
    RUN_TEST(test_faultz_consecutive_reset_by_in_range_sample);
    RUN_TEST(test_pgood_consecutive_reset_by_in_range_sample);
#if (ENABLE_V24_FAULT_CHECK != 0U)
    RUN_TEST(test_v24_reset_does_not_clear_v12_consecutive_counter);
#endif
    RUN_TEST(test_v5_reset_does_not_clear_v3v3_consecutive_counter);
#if (ENABLE_BL_CURRENT_SENSOR != 0U)
    RUN_TEST(test_lcd_current_reset_does_not_clear_backlight_consecutive_counter);
#endif
    RUN_TEST(test_scaler_current_reset_does_not_clear_lcd_consecutive_counter);
    RUN_TEST(test_audio_l_reset_does_not_clear_audio_r_consecutive_counter);
    RUN_TEST(test_faultz_reset_does_not_clear_pgood_consecutive_counter);
    return UNITY_END();
}
