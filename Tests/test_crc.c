/*
 * Unit tests: CRC-8/ATM and CRC-32
 *
 * CRC-8/ATM: poly=0x07, init=0x00, refin/refout=false, xorout=0x00
 *   Used in UART protocol (Rules 4.2).
 *
 * CRC-32: standard Ethernet/ZIP (poly=0xEDB88320 reflected)
 *   Used in flash calibration data (Rules 11).
 *
 * Both functions are static in their source files, so we include the
 * source directly to get access.
 */
#include "unity.h"
#include "config.h"

/* Pull static crc8_calc + table from uart_protocol.c */
/* We need mocks for all external functions it calls */

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

/* Pull static sw_crc32 from flash_cal.c (only the function, not the whole file) */
static uint32_t sw_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
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
    /* Standard CRC-8/ATM check value for ASCII "123456789" is 0xBC */
    TEST_IGNORE_MESSAGE("TODO: verify crc8_calc(\"123456789\", 9) == 0xBC (check value per spec)");
}

void test_crc8_empty_input(void)
{
    TEST_IGNORE_MESSAGE("TODO: crc8_calc(data, 0) should return init value 0x00");
}

void test_crc8_single_byte(void)
{
    TEST_IGNORE_MESSAGE("TODO: single byte CRC matches table lookup");
}

void test_crc8_ping_packet(void)
{
    /* Real PING packet: CMD=0x01, LEN=0x00 → CRC over [0x01, 0x00] */
    TEST_IGNORE_MESSAGE("TODO: CRC of [CMD_PING, 0x00] matches expected value");
}

void test_crc8_get_status_response(void)
{
    /* Verify CRC for a known GET_STATUS response (26 bytes of data) */
    TEST_IGNORE_MESSAGE("TODO: CRC of [CMD_GET_STATUS, 26, <known data>] is correct");
}

void test_crc8_max_length_data(void)
{
    /* CRC over PROTO_MAX_DATA (64) bytes + CMD + LEN */
    TEST_IGNORE_MESSAGE("TODO: CRC of maximum-length packet is correct");
}

/* ===== CRC-32 tests ===== */

void test_crc32_known_vector_123456789(void)
{
    /* Standard CRC-32 check value for "123456789" is 0xCBF43926 */
    TEST_IGNORE_MESSAGE("TODO: sw_crc32(\"123456789\", 9) == 0xCBF43926");
}

void test_crc32_empty_input(void)
{
    TEST_IGNORE_MESSAGE("TODO: sw_crc32(data, 0) == 0x00000000 (empty = all bits flipped)");
}

void test_crc32_single_zero_byte(void)
{
    TEST_IGNORE_MESSAGE("TODO: sw_crc32({0x00}, 1) matches reference implementation");
}

void test_crc32_flash_cal_structure(void)
{
    /* Build a valid flash_cal_t, compute CRC, verify it validates correctly */
    TEST_IGNORE_MESSAGE("TODO: construct flash_cal_t, compute sw_crc32 over payload, verify round-trip");
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    /* CRC-8 */
    RUN_TEST(test_crc8_known_vector_123456789);
    RUN_TEST(test_crc8_empty_input);
    RUN_TEST(test_crc8_single_byte);
    RUN_TEST(test_crc8_ping_packet);
    RUN_TEST(test_crc8_get_status_response);
    RUN_TEST(test_crc8_max_length_data);
    /* CRC-32 */
    RUN_TEST(test_crc32_known_vector_123456789);
    RUN_TEST(test_crc32_empty_input);
    RUN_TEST(test_crc32_single_zero_byte);
    RUN_TEST(test_crc32_flash_cal_structure);
    return UNITY_END();
}
