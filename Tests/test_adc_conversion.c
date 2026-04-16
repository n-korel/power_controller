/*
 * Unit tests: ADC raw → physical conversion (Rules 2.2, 2.3, 5.1-5.3)
 *
 * Formulas under test:
 *   Voltage: raw * 2500 / 4096 → adc_mv → adc_mv * 11616 / 1000 → vin_mV
 *   Current: (adc_mv - offset_mv) * 1000 / 264 → mA
 *   Sliding window: 8-sample average
 *
 * Source included directly for access to static state (window, dma_buf, avg_raw).
 */
#include "unity.h"

volatile uint32_t systick_ms;

#include "adc_service.c"

void setUp(void)
{
    adc_service_init();
}

void tearDown(void) {}

/* ===== Voltage conversion (Rules 2.2, 2.3) ===== */

void test_voltage_zero_raw_gives_zero_mv(void)
{
    /* raw=0 on any voltage channel → 0 mV after divider */
    TEST_IGNORE_MESSAGE("TODO: inject raw=0 into dma_buf[ADC_IDX_V24], process, check adc_get_voltage_mv(0)==0");
}

void test_voltage_full_scale_raw(void)
{
    /* raw=4095 → adc_mv=2499 → vin_mV = 2499*11616/1000 ≈ 29028 */
    TEST_IGNORE_MESSAGE("TODO: inject raw=4095, verify vin_mV matches integer formula");
}

void test_voltage_v24_nominal(void)
{
    /* 24V → after divider ~2066 mV → raw ≈ 2066*4096/2500 ≈ 3380 */
    TEST_IGNORE_MESSAGE("TODO: inject raw≈3380, verify adc_get_voltage_mv returns ~24000");
}

void test_voltage_v12_nominal(void)
{
    /* 12V → after divider ~1033 mV → raw ≈ 1693 */
    TEST_IGNORE_MESSAGE("TODO: inject raw≈1693, verify ~12000 mV");
}

void test_voltage_v5_nominal(void)
{
    /* 5V → after divider ~430 mV → raw ≈ 706 */
    TEST_IGNORE_MESSAGE("TODO: inject raw≈706, verify ~5000 mV");
}

void test_voltage_v3v3_nominal(void)
{
    /* 3.3V → after divider ~284 mV → raw ≈ 466 */
    TEST_IGNORE_MESSAGE("TODO: inject raw≈466, verify ~3300 mV");
}

/* ===== Current conversion (Rules 2.2) ===== */

void test_current_zero_at_default_offset(void)
{
    /* raw = default_offset_raw → 0 mA */
    TEST_IGNORE_MESSAGE("TODO: inject raw=default_offset into current channel, verify 0 mA");
}

void test_current_positive_above_offset(void)
{
    /* raw > offset → positive mA (264 mV/A sensitivity) */
    TEST_IGNORE_MESSAGE("TODO: inject raw above offset by known delta, verify mA = delta*2500/4096*1000/264");
}

void test_current_negative_below_offset(void)
{
    /* raw < offset → negative mA (possible in edge cases) */
    TEST_IGNORE_MESSAGE("TODO: inject raw below offset, verify negative mA");
}

void test_current_max_before_clipping(void)
{
    /* VDDA=2.5V, sensor 3.3V supply → max ~3200 mA (Rules 2.2 note) */
    TEST_IGNORE_MESSAGE("TODO: inject raw=4095 for current channel, verify value < ~3200 mA");
}

void test_current_custom_offset(void)
{
    /* adc_set_current_offset changes the baseline */
    TEST_IGNORE_MESSAGE("TODO: set custom offset, inject raw, verify mA relative to new offset");
}

/* ===== Sliding window averaging (Rules 5.3) ===== */

void test_window_single_sample(void)
{
    /* After 1 process() call, average = that single DMA snapshot */
    TEST_IGNORE_MESSAGE("TODO: one adc_service_process(), check avg_raw == dma_buf value");
}

void test_window_full_8_samples(void)
{
    /* After 8 calls with different DMA values, average is arithmetic mean */
    TEST_IGNORE_MESSAGE("TODO: feed 8 known values, verify avg_raw = sum/8");
}

void test_window_wraps_correctly(void)
{
    /* After 9+ calls, oldest sample is replaced */
    TEST_IGNORE_MESSAGE("TODO: feed 9 values, verify oldest value dropped from average");
}

void test_window_all_channels_independent(void)
{
    /* Averaging of channel 0 doesn't affect channel 13 */
    TEST_IGNORE_MESSAGE("TODO: feed different values per channel, verify each averages independently");
}

/* ===== Getter bounds (defensive) ===== */

void test_voltage_getter_out_of_range(void)
{
    TEST_IGNORE_MESSAGE("TODO: adc_get_voltage_mv(4) returns 0, adc_get_voltage_mv(255) returns 0");
}

void test_current_getter_out_of_range(void)
{
    TEST_IGNORE_MESSAGE("TODO: adc_get_current_ma(5) returns 0");
}

void test_temp_always_returns_minus32768(void)
{
    /* NTC not installed (Rules 2.4) */
    TEST_IGNORE_MESSAGE("TODO: adc_get_temp(0) == -32768, adc_get_temp(1) == -32768");
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    /* Voltage */
    RUN_TEST(test_voltage_zero_raw_gives_zero_mv);
    RUN_TEST(test_voltage_full_scale_raw);
    RUN_TEST(test_voltage_v24_nominal);
    RUN_TEST(test_voltage_v12_nominal);
    RUN_TEST(test_voltage_v5_nominal);
    RUN_TEST(test_voltage_v3v3_nominal);
    /* Current */
    RUN_TEST(test_current_zero_at_default_offset);
    RUN_TEST(test_current_positive_above_offset);
    RUN_TEST(test_current_negative_below_offset);
    RUN_TEST(test_current_max_before_clipping);
    RUN_TEST(test_current_custom_offset);
    /* Window */
    RUN_TEST(test_window_single_sample);
    RUN_TEST(test_window_full_8_samples);
    RUN_TEST(test_window_wraps_correctly);
    RUN_TEST(test_window_all_channels_independent);
    /* Getters */
    RUN_TEST(test_voltage_getter_out_of_range);
    RUN_TEST(test_current_getter_out_of_range);
    RUN_TEST(test_temp_always_returns_minus32768);
    return UNITY_END();
}
