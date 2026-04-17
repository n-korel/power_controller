/*
 * Unit tests: CRC-8/ATM and CRC-32
 *
 * CRC-8/ATM (implementation: poly=0x07, init=0x00, refin/out=false, xorout=0x00)
 *   Used in UART protocol (Rules 4.2).
 *   Standard check value for "123456789" = 0xF4 (CRC-8/SMBus).
 *
 * CRC-32: standard Ethernet/ZIP (poly reflected 0xEDB88320,
 *   init=0xFFFFFFFF, xorout=0xFFFFFFFF, refin/out=true).
 *   Used in flash calibration data (Rules 11).
 */
#include "unity.h"
#include "config.h"
#include "flash_cal.h"
#include <string.h>
#include <stddef.h>

/* --- Mocks for uart_protocol.c dependencies --- */
static uint16_t mock_voltage_mv[4];
static int16_t  mock_current_ma[5];
static uint8_t  mock_power_state;
static uint16_t mock_fault_flags;
static uint8_t  mock_input_packed;

uint16_t adc_get_voltage_mv(uint8_t idx) { return (idx < 4) ? mock_voltage_mv[idx] : 0; }
int16_t  adc_get_current_ma(uint8_t idx) { return (idx < 5) ? mock_current_ma[idx] : 0; }
int16_t  adc_get_temp(uint8_t idx)       { (void)idx; return -32768; }
uint8_t  power_get_state(void)           { return mock_power_state; }
uint16_t fault_get_flags(void)           { return mock_fault_flags; }
uint8_t  input_get_packed(void)          { return mock_input_packed; }
uint8_t  power_ctrl_request(uint16_t m, uint16_t v) { (void)m; (void)v; return 0; }
void     power_set_brightness(uint16_t p) { (void)p; }
void     fault_clear_flags(void) {}
void     power_reset_bridge(void) {}
void     fault_set_threshold(uint8_t i, uint16_t mn, uint16_t mx) { (void)i; (void)mn; (void)mx; }
void     power_safe_state(void) {}
void     bootloader_schedule(void) {}
uint8_t  flash_cal_calibrate(void) { return 0; }

volatile uint32_t systick_ms;

#include "uart_protocol.c"

/* Reference CRC-32 (same algorithm as flash_cal.c sw_crc32) */
static uint32_t sw_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320U;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

void setUp(void) {}
void tearDown(void) {}

/* ===== CRC-8/ATM tests ===== */

void test_crc8_known_vector_123456789(void)
{
    const uint8_t v[] = "123456789";
    TEST_ASSERT_EQUAL_HEX8(0xF4, crc8_calc(v, 9));
}

void test_crc8_empty_input(void)
{
    const uint8_t v[] = { 0x00 };
    TEST_ASSERT_EQUAL_HEX8(0x00, crc8_calc(v, 0));
}

void test_crc8_single_byte(void)
{
    for (uint16_t b = 0; b < 256; b++) {
        uint8_t byte = (uint8_t)b;
        uint8_t got = crc8_calc(&byte, 1);
        TEST_ASSERT_EQUAL_HEX8(crc8_table[byte], got);
    }
}

void test_crc8_ping_packet(void)
{
    const uint8_t buf[] = { CMD_PING, 0x00 };
    uint8_t expected = crc8_table[crc8_table[0 ^ CMD_PING] ^ 0x00];
    TEST_ASSERT_EQUAL_HEX8(expected, crc8_calc(buf, 2));
}

void test_crc8_get_status_response(void)
{
    uint8_t buf[2 + GET_STATUS_DATA_LEN];
    buf[0] = CMD_GET_STATUS;
    buf[1] = GET_STATUS_DATA_LEN;
    for (uint8_t i = 0; i < GET_STATUS_DATA_LEN; i++)
        buf[2 + i] = i;

    uint8_t ref = 0;
    for (size_t i = 0; i < sizeof(buf); i++)
        ref = crc8_table[ref ^ buf[i]];

    TEST_ASSERT_EQUAL_HEX8(ref, crc8_calc(buf, sizeof(buf)));
}

void test_crc8_max_length_data(void)
{
    uint8_t buf[2 + PROTO_MAX_DATA];
    buf[0] = CMD_SET_THRESHOLDS;
    buf[1] = PROTO_MAX_DATA;
    for (uint8_t i = 0; i < PROTO_MAX_DATA; i++)
        buf[2 + i] = (uint8_t)(0xA5 ^ i);

    uint8_t ref = 0;
    for (size_t i = 0; i < sizeof(buf); i++)
        ref = crc8_table[ref ^ buf[i]];

    TEST_ASSERT_EQUAL_HEX8(ref, crc8_calc(buf, (uint8_t)sizeof(buf)));
}

/* ===== CRC-32 tests ===== */

void test_crc32_known_vector_123456789(void)
{
    const uint8_t v[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926U, sw_crc32(v, 9));
}

void test_crc32_empty_input(void)
{
    const uint8_t v[] = { 0x00 };
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, sw_crc32(v, 0));
}

void test_crc32_single_zero_byte(void)
{
    /* CRC-32 of single 0x00 byte = 0xD202EF8D (standard value) */
    const uint8_t v[] = { 0x00 };
    TEST_ASSERT_EQUAL_HEX32(0xD202EF8DU, sw_crc32(v, 1));
}

void test_crc32_flash_cal_structure(void)
{
    flash_cal_t cal;
    memset(&cal, 0, sizeof(cal));
    cal.magic   = FLASH_CAL_MAGIC;
    cal.version = FLASH_CAL_VERSION;
    for (uint8_t i = 0; i < 5; i++)
        cal.offset_raw[i] = (uint16_t)(2000 + i * 10);

    uint32_t payload_len = offsetof(flash_cal_t, crc32);
    cal.crc32 = sw_crc32((const uint8_t *)&cal, payload_len);

    uint32_t recomputed = sw_crc32((const uint8_t *)&cal, payload_len);
    TEST_ASSERT_EQUAL_HEX32(cal.crc32, recomputed);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc8_known_vector_123456789);
    RUN_TEST(test_crc8_empty_input);
    RUN_TEST(test_crc8_single_byte);
    RUN_TEST(test_crc8_ping_packet);
    RUN_TEST(test_crc8_get_status_response);
    RUN_TEST(test_crc8_max_length_data);
    RUN_TEST(test_crc32_known_vector_123456789);
    RUN_TEST(test_crc32_empty_input);
    RUN_TEST(test_crc32_single_zero_byte);
    RUN_TEST(test_crc32_flash_cal_structure);
    return UNITY_END();
}
