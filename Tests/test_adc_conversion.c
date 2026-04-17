/*
 * Unit tests: ADC raw → physical conversion (Rules 2.2, 2.3, 5.1-5.3)
 *
 * Integer formulas (no float):
 *   adc_mv  = raw * 2500 / 4096
 *   vin_mV  = adc_mv * 11616 / 1000
 *   cur_mA  = (adc_mv - offset_mv) * 1000 / 264
 *   avg_raw = sum(window[..fill]) / fill, window size = 8
 */
#include "unity.h"
#include "config.h"

volatile uint32_t systick_ms;

#include "adc_service.c"

static uint16_t v_to_raw_mv(uint16_t raw)
{
    uint32_t adc_mv = (uint32_t)raw * ADC_VREF_MV / ADC_RESOLUTION;
    return (uint16_t)(adc_mv * VDIV_MULT / VDIV_DIV);
}

void setUp(void)
{
    adc_service_init();
    for (uint8_t i = 0; i < ADC_CHANNEL_COUNT; i++)
        dma_buf[i] = 0;
}

void tearDown(void) {}

/* ===== Voltage conversion (Rules 2.2, 2.3) ===== */

void test_voltage_zero_raw_gives_zero_mv(void)
{
    dma_buf[ADC_IDX_V24] = 0;
    adc_service_process();
    TEST_ASSERT_EQUAL_UINT16(0, adc_get_voltage_mv(0));
}

void test_voltage_full_scale_raw(void)
{
    dma_buf[ADC_IDX_V24] = 4095;
    adc_service_process();
    TEST_ASSERT_EQUAL_UINT16(v_to_raw_mv(4095), adc_get_voltage_mv(0));
}

void test_voltage_v24_nominal(void)
{
    dma_buf[ADC_IDX_V24] = 3385;
    adc_service_process();

    uint16_t got = adc_get_voltage_mv(0);
    TEST_ASSERT_UINT16_WITHIN(50, 24000, got);
    TEST_ASSERT_EQUAL_UINT16(v_to_raw_mv(3385), got);
}

void test_voltage_v12_nominal(void)
{
    dma_buf[ADC_IDX_V12] = 1693;
    adc_service_process();

    uint16_t got = adc_get_voltage_mv(1);
    TEST_ASSERT_UINT16_WITHIN(50, 12000, got);
    TEST_ASSERT_EQUAL_UINT16(v_to_raw_mv(1693), got);
}

void test_voltage_v5_nominal(void)
{
    dma_buf[ADC_IDX_V5] = 706;
    adc_service_process();

    uint16_t got = adc_get_voltage_mv(2);
    TEST_ASSERT_UINT16_WITHIN(50, 5000, got);
    TEST_ASSERT_EQUAL_UINT16(v_to_raw_mv(706), got);
}

void test_voltage_v3v3_nominal(void)
{
    dma_buf[ADC_IDX_V3V3] = 466;
    adc_service_process();

    uint16_t got = adc_get_voltage_mv(3);
    TEST_ASSERT_UINT16_WITHIN(50, 3300, got);
    TEST_ASSERT_EQUAL_UINT16(v_to_raw_mv(466), got);
}

/* ===== Current conversion (Rules 2.2) ===== */

void test_current_zero_at_default_offset(void)
{
    uint16_t off = default_offset_raw();
    dma_buf[ADC_IDX_LCD_CURRENT] = off;
    adc_service_process();

    TEST_ASSERT_EQUAL_INT16(0, adc_get_current_ma(0));
}

void test_current_positive_above_offset(void)
{
    uint16_t off = default_offset_raw();
    uint16_t raw = off + 300;
    dma_buf[ADC_IDX_LCD_CURRENT] = raw;
    adc_service_process();

    uint32_t adc_mv    = (uint32_t)raw * ADC_VREF_MV / ADC_RESOLUTION;
    uint32_t offset_mv = (uint32_t)off * ADC_VREF_MV / ADC_RESOLUTION;
    int32_t  diff_mv   = (int32_t)adc_mv - (int32_t)offset_mv;
    int16_t  expected  = (int16_t)(diff_mv * 1000 / (int32_t)CURRENT_SENSITIVITY_MV_PER_A);

    int16_t got = adc_get_current_ma(0);
    TEST_ASSERT_EQUAL_INT16(expected, got);
    TEST_ASSERT_TRUE(got > 0);
}

void test_current_negative_below_offset(void)
{
    uint16_t off = default_offset_raw();
    uint16_t raw = off - 200;
    dma_buf[ADC_IDX_LCD_CURRENT] = raw;
    adc_service_process();

    TEST_ASSERT_TRUE(adc_get_current_ma(0) < 0);
}

void test_current_max_before_clipping(void)
{
    dma_buf[ADC_IDX_LCD_CURRENT] = 4095;
    adc_service_process();

    int16_t got = adc_get_current_ma(0);
    /* VDDA=2.5V, sensor 3.3V supply, offset ~1650 mV:
       max diff = 2499 - 1649 = 850 mV → ~3219 mA. Upper bound ~3300. */
    TEST_ASSERT_TRUE(got > 0);
    TEST_ASSERT_TRUE(got < 3300);
}

void test_current_custom_offset(void)
{
    uint16_t custom = 3000;
    adc_set_current_offset(0, custom);
    TEST_ASSERT_EQUAL_UINT16(custom, adc_get_current_offset(0));

    dma_buf[ADC_IDX_LCD_CURRENT] = custom;
    adc_service_process();
    TEST_ASSERT_EQUAL_INT16(0, adc_get_current_ma(0));

    dma_buf[ADC_IDX_LCD_CURRENT] = custom + 400;
    adc_service_process();
    TEST_ASSERT_TRUE(adc_get_current_ma(0) > 0);
}

/* ===== Sliding window averaging (Rules 5.3) ===== */

void test_window_single_sample(void)
{
    dma_buf[ADC_IDX_V24] = 1234;
    adc_service_process();
    TEST_ASSERT_EQUAL_UINT16(1234, adc_get_raw_avg(ADC_IDX_V24));
}

void test_window_full_8_samples(void)
{
    const uint16_t vals[8] = { 100, 200, 300, 400, 500, 600, 700, 800 };
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 8; i++) {
        dma_buf[ADC_IDX_V24] = vals[i];
        adc_service_process();
        sum += vals[i];
    }
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(sum / 8), adc_get_raw_avg(ADC_IDX_V24));
}

void test_window_wraps_correctly(void)
{
    const uint16_t vals[9] = { 100, 200, 300, 400, 500, 600, 700, 800, 900 };
    for (uint8_t i = 0; i < 9; i++) {
        dma_buf[ADC_IDX_V24] = vals[i];
        adc_service_process();
    }
    /* After 9 feeds: window[0]=900 (overwrote 100); others unchanged. Still fill=8 → avg of current window */
    uint32_t expected = 900 + 200 + 300 + 400 + 500 + 600 + 700 + 800;
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(expected / 8), adc_get_raw_avg(ADC_IDX_V24));
}

void test_window_all_channels_independent(void)
{
    dma_buf[ADC_IDX_V24]  = 1000;
    dma_buf[ADC_IDX_TEMP1] = 2000;
    adc_service_process();
    dma_buf[ADC_IDX_V24]  = 1100;
    dma_buf[ADC_IDX_TEMP1] = 2200;
    adc_service_process();

    TEST_ASSERT_EQUAL_UINT16((1000 + 1100) / 2, adc_get_raw_avg(ADC_IDX_V24));
    TEST_ASSERT_EQUAL_UINT16((2000 + 2200) / 2, adc_get_raw_avg(ADC_IDX_TEMP1));
}

/* ===== Getter bounds ===== */

void test_voltage_getter_out_of_range(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, adc_get_voltage_mv(4));
    TEST_ASSERT_EQUAL_UINT16(0, adc_get_voltage_mv(255));
}

void test_current_getter_out_of_range(void)
{
    TEST_ASSERT_EQUAL_INT16(0, adc_get_current_ma(5));
    TEST_ASSERT_EQUAL_INT16(0, adc_get_current_ma(255));
}

void test_temp_always_returns_minus32768(void)
{
    TEST_ASSERT_EQUAL_INT16(-32768, adc_get_temp(0));
    TEST_ASSERT_EQUAL_INT16(-32768, adc_get_temp(1));
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_voltage_zero_raw_gives_zero_mv);
    RUN_TEST(test_voltage_full_scale_raw);
    RUN_TEST(test_voltage_v24_nominal);
    RUN_TEST(test_voltage_v12_nominal);
    RUN_TEST(test_voltage_v5_nominal);
    RUN_TEST(test_voltage_v3v3_nominal);
    RUN_TEST(test_current_zero_at_default_offset);
    RUN_TEST(test_current_positive_above_offset);
    RUN_TEST(test_current_negative_below_offset);
    RUN_TEST(test_current_max_before_clipping);
    RUN_TEST(test_current_custom_offset);
    RUN_TEST(test_window_single_sample);
    RUN_TEST(test_window_full_8_samples);
    RUN_TEST(test_window_wraps_correctly);
    RUN_TEST(test_window_all_channels_independent);
    RUN_TEST(test_voltage_getter_out_of_range);
    RUN_TEST(test_current_getter_out_of_range);
    RUN_TEST(test_temp_always_returns_minus32768);
    return UNITY_END();
}
