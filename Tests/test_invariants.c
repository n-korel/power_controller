/*
 * Unit tests: Contract invariants (Rules_POWER.md §12)
 *
 * These tests verify that compile-time constants and protocol definitions
 * match the immutable contract. Any change to these constants is a contract
 * violation and must be caught.
 *
 * No HAL dependencies — only checks config.h values.
 */
#include "unity.h"
#include "config.h"
#include "uart_protocol.h"

volatile uint32_t systick_ms;

void setUp(void) {}
void tearDown(void) {}

/* ===== ADC invariants (Rules 2.2, 5.1-5.2) ===== */

void test_adc_channel_count_is_14(void)
{
    TEST_ASSERT_EQUAL_UINT(14, ADC_CHANNEL_COUNT);
}

void test_adc_vref_is_2500(void)
{
    TEST_ASSERT_EQUAL_UINT(2500, ADC_VREF_MV);
}

void test_adc_resolution_is_4096(void)
{
    TEST_ASSERT_EQUAL_UINT(4096, ADC_RESOLUTION);
}

void test_adc_index_order_matches_contract(void)
{
    /* Rules 5.2: fixed DMA scan order Rank 1..14 = index 0..13 */
    TEST_ASSERT_EQUAL(0,  ADC_IDX_LCD_CURRENT);
    TEST_ASSERT_EQUAL(1,  ADC_IDX_BL_CURRENT);
    TEST_ASSERT_EQUAL(2,  ADC_IDX_SCALER_CURRENT);
    TEST_ASSERT_EQUAL(3,  ADC_IDX_AUDIO_L_CURRENT);
    TEST_ASSERT_EQUAL(4,  ADC_IDX_AUDIO_R_CURRENT);
    TEST_ASSERT_EQUAL(5,  ADC_IDX_LCD_POWER);
    TEST_ASSERT_EQUAL(6,  ADC_IDX_BL_POWER);
    TEST_ASSERT_EQUAL(7,  ADC_IDX_SCALER_POWER);
    TEST_ASSERT_EQUAL(8,  ADC_IDX_V24);
    TEST_ASSERT_EQUAL(9,  ADC_IDX_V12);
    TEST_ASSERT_EQUAL(10, ADC_IDX_V5);
    TEST_ASSERT_EQUAL(11, ADC_IDX_V3V3);
    TEST_ASSERT_EQUAL(12, ADC_IDX_TEMP0);
    TEST_ASSERT_EQUAL(13, ADC_IDX_TEMP1);
}

void test_adc_window_size_is_8(void)
{
    TEST_ASSERT_EQUAL_UINT(8, ADC_WINDOW_SIZE);
}

void test_fault_confirm_count_is_5(void)
{
    TEST_ASSERT_EQUAL_UINT(5, FAULT_CONFIRM_COUNT);
}

/* ===== UART protocol invariants (Rules 4) ===== */

void test_proto_stx_is_0x02(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x02, PROTO_STX);
}

void test_proto_etx_is_0x03(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x03, PROTO_ETX);
}

void test_get_status_data_len_is_26(void)
{
    TEST_ASSERT_EQUAL_UINT(26, GET_STATUS_DATA_LEN);
}

void test_ping_response_is_0xAA(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xAA, PING_RESPONSE);
}

/* ===== Command codes (Rules 4.3) ===== */

void test_cmd_ping(void)             { TEST_ASSERT_EQUAL_HEX8(0x01, CMD_PING); }
void test_cmd_power_ctrl(void)       { TEST_ASSERT_EQUAL_HEX8(0x02, CMD_POWER_CTRL); }
void test_cmd_set_brightness(void)   { TEST_ASSERT_EQUAL_HEX8(0x03, CMD_SET_BRIGHTNESS); }
void test_cmd_get_status(void)       { TEST_ASSERT_EQUAL_HEX8(0x04, CMD_GET_STATUS); }
void test_cmd_reset_fault(void)      { TEST_ASSERT_EQUAL_HEX8(0x05, CMD_RESET_FAULT); }
void test_cmd_reset_bridge(void)     { TEST_ASSERT_EQUAL_HEX8(0x06, CMD_RESET_BRIDGE); }
void test_cmd_set_thresholds(void)   { TEST_ASSERT_EQUAL_HEX8(0x07, CMD_SET_THRESHOLDS); }
void test_cmd_bootloader_enter(void) { TEST_ASSERT_EQUAL_HEX8(0x08, CMD_BOOTLOADER_ENTER); }
void test_cmd_calibrate_offset(void) { TEST_ASSERT_EQUAL_HEX8(0x09, CMD_CALIBRATE_OFFSET); }
void test_cmd_nack(void)             { TEST_ASSERT_EQUAL_HEX8(0xFF, CMD_NACK); }

/* ===== Domain bitmask (Rules 4.5) ===== */

void test_domain_bits(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, DOM_SCALER);
    TEST_ASSERT_EQUAL_HEX8(0x02, DOM_LCD);
    TEST_ASSERT_EQUAL_HEX8(0x04, DOM_BACKLIGHT);
    TEST_ASSERT_EQUAL_HEX8(0x08, DOM_AUDIO);
    TEST_ASSERT_EQUAL_HEX8(0x10, DOM_ETH1);
    TEST_ASSERT_EQUAL_HEX8(0x20, DOM_ETH2);
    TEST_ASSERT_EQUAL_HEX8(0x40, DOM_TOUCH);
}

/* ===== Fault flags bitmask (Rules 7.2) ===== */

void test_fault_bits(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0001, FAULT_SCALER);
    TEST_ASSERT_EQUAL_HEX16(0x0002, FAULT_LCD);
    TEST_ASSERT_EQUAL_HEX16(0x0004, FAULT_BACKLIGHT);
    TEST_ASSERT_EQUAL_HEX16(0x0008, FAULT_AUDIO);
    TEST_ASSERT_EQUAL_HEX16(0x0010, FAULT_ETH1);
    TEST_ASSERT_EQUAL_HEX16(0x0020, FAULT_ETH2);
    TEST_ASSERT_EQUAL_HEX16(0x0040, FAULT_TOUCH);
    TEST_ASSERT_EQUAL_HEX16(0x0080, FAULT_PGOOD_LOST);
    TEST_ASSERT_EQUAL_HEX16(0x0100, FAULT_AMP_FAULTZ);
    TEST_ASSERT_EQUAL_HEX16(0x0200, FAULT_V24_RANGE);
    TEST_ASSERT_EQUAL_HEX16(0x0400, FAULT_V12_RANGE);
    TEST_ASSERT_EQUAL_HEX16(0x0800, FAULT_V5_RANGE);
    TEST_ASSERT_EQUAL_HEX16(0x1000, FAULT_V3V3_RANGE);
    TEST_ASSERT_EQUAL_HEX16(0x2000, FAULT_SEQ_ABORT);
    TEST_ASSERT_EQUAL_HEX16(0x4000, FAULT_INTERNAL);
    TEST_ASSERT_EQUAL_HEX16(0x8000, FAULT_RESERVED);
}

void test_fault_reserved_bit15_must_be_zero_in_flags(void)
{
    /* All active fault flags OR'd must fit into bits 0..14 (0x7FFF),
       so no valid fault scenario can ever set bit15 in fault_get_flags(). */
    const uint16_t active_flags =
        FAULT_SCALER | FAULT_LCD | FAULT_BACKLIGHT | FAULT_AUDIO |
        FAULT_ETH1 | FAULT_ETH2 | FAULT_TOUCH | FAULT_PGOOD_LOST |
        FAULT_AMP_FAULTZ | FAULT_V24_RANGE | FAULT_V12_RANGE |
        FAULT_V5_RANGE | FAULT_V3V3_RANGE | FAULT_SEQ_ABORT | FAULT_INTERNAL;

    TEST_ASSERT_EQUAL_HEX16(0x7FFFU, active_flags);
    TEST_ASSERT_EQUAL_HEX16(0x0000U, active_flags & FAULT_RESERVED);
    TEST_ASSERT_EQUAL_HEX16(0x8000U, FAULT_RESERVED);
}

/* ===== Bootloader constants (Rules 10) ===== */

void test_sram_magic_value(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, SRAM_MAGIC_VALUE);
}

void test_rom_bootloader_address(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x1FFF0000, ROM_BOOTLOADER_ADDR);
}

/* ===== Flash calibration constants (Rules 11) ===== */

void test_flash_cal_address(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x0800FC00, FLASH_CAL_ADDR);
}

void test_flash_cal_magic(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x43414C49, FLASH_CAL_MAGIC);
}

/* ===== Sequencing timings sanity (Rules 6) ===== */

void test_sequencing_timing_values(void)
{
    TEST_ASSERT_EQUAL_UINT(50,  SEQ_DELAY_SCALER_ON);
    TEST_ASSERT_EQUAL_UINT(20,  SEQ_DELAY_RST_RELEASE);
    TEST_ASSERT_EQUAL_UINT(50,  SEQ_DELAY_LCD_ON);
    TEST_ASSERT_EQUAL_UINT(10,  SEQ_DELAY_PWM_OFF);
    TEST_ASSERT_EQUAL_UINT(20,  SEQ_DELAY_BL_OFF);
    TEST_ASSERT_EQUAL_UINT(20,  SEQ_DELAY_LCD_OFF);
    TEST_ASSERT_EQUAL_UINT(200, SEQ_VERIFY_TIMEOUT);
}

void test_proto_max_data(void)
{
    TEST_ASSERT_EQUAL_UINT(64, PROTO_MAX_DATA);
}

/* ===== Voltage divider (Rules 2.3) ===== */

void test_voltage_divider_constants(void)
{
    /* Vin = Vadc * 11616 / 1000 — drives adc_service voltage conversion. */
    TEST_ASSERT_EQUAL_UINT(11616, VDIV_MULT);
    TEST_ASSERT_EQUAL_UINT(1000,  VDIV_DIV);
}

/* ===== Current sensor (Rules 2.2) ===== */

void test_current_sensor_constants(void)
{
    /* 264 mV/A sensitivity, default Voffset = 1650 mV @ 3.3V supply.
       5 channels: LCD, BL, SCALER, AUDIO_L, AUDIO_R. */
    TEST_ASSERT_EQUAL_UINT(264U,     CURRENT_SENSITIVITY_MV_PER_A);
    TEST_ASSERT_EQUAL_UINT(264000U,  CURRENT_SENSITIVITY_UV_PER_A);
    TEST_ASSERT_EQUAL_UINT(1650U,    CURRENT_VOFFSET_MV_DEFAULT);
    TEST_ASSERT_EQUAL_UINT(5,        CURRENT_CHANNELS);
}

/* ===== Default voltage thresholds (README §8, Rules 7) ===== */

void test_default_voltage_thresholds(void)
{
    TEST_ASSERT_EQUAL_UINT(20000U, THRESH_V24_MIN);
    TEST_ASSERT_EQUAL_UINT(26000U, THRESH_V24_MAX);
    TEST_ASSERT_EQUAL_UINT(10000U, THRESH_V12_MIN);
    TEST_ASSERT_EQUAL_UINT(13000U, THRESH_V12_MAX);
    TEST_ASSERT_EQUAL_UINT(4500U,  THRESH_V5_MIN);
    TEST_ASSERT_EQUAL_UINT(5500U,  THRESH_V5_MAX);
    TEST_ASSERT_EQUAL_UINT(3000U,  THRESH_V3V3_MIN);
    TEST_ASSERT_EQUAL_UINT(3600U,  THRESH_V3V3_MAX);

    /* Sanity: min < max for every rail. */
    TEST_ASSERT_TRUE(THRESH_V24_MIN  < THRESH_V24_MAX);
    TEST_ASSERT_TRUE(THRESH_V12_MIN  < THRESH_V12_MAX);
    TEST_ASSERT_TRUE(THRESH_V5_MIN   < THRESH_V5_MAX);
    TEST_ASSERT_TRUE(THRESH_V3V3_MIN < THRESH_V3V3_MAX);
}

/* ===== Default current thresholds (Rules 7) ===== */

void test_default_current_thresholds(void)
{
    TEST_ASSERT_EQUAL_UINT(2000U, THRESH_I_LCD_MAX);
    TEST_ASSERT_EQUAL_UINT(3000U, THRESH_I_BL_MAX);
    TEST_ASSERT_EQUAL_UINT(1500U, THRESH_I_SCALER_MAX);
    TEST_ASSERT_EQUAL_UINT(800U,  THRESH_I_AUDIO_LR_MAX);
}

/* ===== SUS_S3#/PWRBTN auto-start (Rules 8) ===== */

void test_sus_s3_and_pwrbtn_timings(void)
{
    TEST_ASSERT_EQUAL_UINT(500U,  SUS_S3_THRESHOLD_MS);
    TEST_ASSERT_EQUAL_UINT(5000U, SUS_S3_COOLDOWN_MS);
    TEST_ASSERT_EQUAL_UINT(150U,  PWRBTN_PULSE_MS);

    /* Cooldown must exceed the threshold, or auto-start could fire twice. */
    TEST_ASSERT_TRUE(SUS_S3_COOLDOWN_MS > SUS_S3_THRESHOLD_MS);
}

/* ===== Audio amplifier, bridge reset, PGOOD timeout ===== */

void test_audio_bridge_and_pgood_timings(void)
{
    /* Rules 9 — TPA3118 wake/shutdown delays */
    TEST_ASSERT_EQUAL_UINT(10U, AUDIO_SDZ_DELAY_MS);
    TEST_ASSERT_EQUAL_UINT(10U, AUDIO_MUTE_DELAY_MS);

    /* Rules 13 — bridge reset pulse */
    TEST_ASSERT_EQUAL_UINT(10U, BRIDGE_RST_PULSE_MS);

    /* Rules 6.5 — PGOOD lost latches FAULT_PGOOD_LOST after this window */
    TEST_ASSERT_EQUAL_UINT(5000U, PGOOD_TIMEOUT_MS);
}

/* ===== UART timeouts, debounce, flash version ===== */

void test_uart_debounce_and_flash_version(void)
{
    /* Rules 4.5 — UART packet framing timeouts */
    TEST_ASSERT_EQUAL_UINT(10U, UART_INTERBYTE_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_UINT(50U, UART_PACKET_TIMEOUT_MS);

    /* Inter-byte timeout must be smaller than full-packet timeout. */
    TEST_ASSERT_TRUE(UART_INTERBYTE_TIMEOUT_MS < UART_PACKET_TIMEOUT_MS);

    /* Rules 16 — input debounce */
    TEST_ASSERT_EQUAL_UINT(20U, DEBOUNCE_MS);

    /* Rules 11 — flash calibration record version */
    TEST_ASSERT_EQUAL_UINT(1U, FLASH_CAL_VERSION);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    /* ADC */
    RUN_TEST(test_adc_channel_count_is_14);
    RUN_TEST(test_adc_vref_is_2500);
    RUN_TEST(test_adc_resolution_is_4096);
    RUN_TEST(test_adc_index_order_matches_contract);
    RUN_TEST(test_adc_window_size_is_8);
    RUN_TEST(test_fault_confirm_count_is_5);
    /* UART */
    RUN_TEST(test_proto_stx_is_0x02);
    RUN_TEST(test_proto_etx_is_0x03);
    RUN_TEST(test_get_status_data_len_is_26);
    RUN_TEST(test_ping_response_is_0xAA);
    /* Commands */
    RUN_TEST(test_cmd_ping);
    RUN_TEST(test_cmd_power_ctrl);
    RUN_TEST(test_cmd_set_brightness);
    RUN_TEST(test_cmd_get_status);
    RUN_TEST(test_cmd_reset_fault);
    RUN_TEST(test_cmd_reset_bridge);
    RUN_TEST(test_cmd_set_thresholds);
    RUN_TEST(test_cmd_bootloader_enter);
    RUN_TEST(test_cmd_calibrate_offset);
    RUN_TEST(test_cmd_nack);
    /* Domains */
    RUN_TEST(test_domain_bits);
    /* Faults */
    RUN_TEST(test_fault_bits);
    RUN_TEST(test_fault_reserved_bit15_must_be_zero_in_flags);
    /* Bootloader */
    RUN_TEST(test_sram_magic_value);
    RUN_TEST(test_rom_bootloader_address);
    /* Flash cal */
    RUN_TEST(test_flash_cal_address);
    RUN_TEST(test_flash_cal_magic);
    /* Timings */
    RUN_TEST(test_sequencing_timing_values);
    RUN_TEST(test_proto_max_data);
    /* Conversion math constants */
    RUN_TEST(test_voltage_divider_constants);
    RUN_TEST(test_current_sensor_constants);
    /* Default thresholds */
    RUN_TEST(test_default_voltage_thresholds);
    RUN_TEST(test_default_current_thresholds);
    /* Timings */
    RUN_TEST(test_sus_s3_and_pwrbtn_timings);
    RUN_TEST(test_audio_bridge_and_pgood_timings);
    RUN_TEST(test_uart_debounce_and_flash_version);
    return UNITY_END();
}
