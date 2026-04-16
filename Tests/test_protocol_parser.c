/*
 * Unit tests: UART protocol parser state machine (Rules 4.1-4.5)
 *
 * The parser (uart_protocol_rx_byte_cb) processes one byte at a time
 * from the ISR callback, building a packet [STX][CMD][LEN][DATA][CRC][ETX].
 *
 * Source included directly for access to static parser state.
 */
#include "unity.h"
#include "config.h"

/* --- Mocks for uart_protocol.c dependencies --- */
static uint16_t mock_voltage_mv[4];
static int16_t  mock_current_ma[5];
static uint8_t  mock_power_state;
static uint16_t mock_fault_flags;
static uint8_t  mock_input_packed;
static uint8_t  mock_power_ctrl_result;

uint16_t adc_get_voltage_mv(uint8_t idx) { return (idx < 4) ? mock_voltage_mv[idx] : 0; }
int16_t  adc_get_current_ma(uint8_t idx) { return (idx < 5) ? mock_current_ma[idx] : 0; }
int16_t  adc_get_temp(uint8_t idx)       { (void)idx; return -32768; }
uint8_t  power_get_state(void)           { return mock_power_state; }
uint16_t fault_get_flags(void)           { return mock_fault_flags; }
uint8_t  input_get_packed(void)          { return mock_input_packed; }
uint8_t  power_ctrl_request(uint16_t m, uint16_t v) { (void)m; (void)v; return mock_power_ctrl_result; }
void     power_set_brightness(uint16_t p) { (void)p; }
void     fault_clear_flags(void) {}
void     power_reset_bridge(void) {}
void     fault_set_threshold(uint8_t i, uint16_t mn, uint16_t mx) { (void)i; (void)mn; (void)mx; }
void     power_safe_state(void) {}
void     bootloader_schedule(void) {}
uint8_t  flash_cal_calibrate(void) { return 0; }
uint16_t adc_get_raw_avg(uint8_t idx) { (void)idx; return 0; }

volatile uint32_t systick_ms;

#include "uart_protocol.c"

/* Helper: feed a sequence of bytes into the parser */
static void feed_bytes(const uint8_t *data, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        rx_byte = data[i];
        uart_protocol_rx_byte_cb();
    }
}

void setUp(void)
{
    p_state = PS_WAIT_STX;
    p_ready = 0;
    tx_busy_flag = 0;
    systick_ms = 1000;
    mock_power_ctrl_result = 0;
}

void tearDown(void) {}

/* ===== Basic parsing ===== */

void test_parser_valid_ping_packet(void)
{
    /* [STX=0x02][CMD=0x01][LEN=0x00][CRC][ETX=0x03] → p_ready=1, cmd=PING */
    TEST_IGNORE_MESSAGE("TODO: build valid PING packet with correct CRC, feed, assert p_ready && p_pkt.cmd==0x01");
}

void test_parser_valid_get_status_request(void)
{
    /* [STX][CMD=0x04][LEN=0x00][CRC][ETX] */
    TEST_IGNORE_MESSAGE("TODO: build GET_STATUS request, feed, assert p_ready && p_pkt.cmd==0x04");
}

void test_parser_valid_power_ctrl(void)
{
    /* [STX][CMD=0x02][LEN=0x04][mask_lo,mask_hi,val_lo,val_hi][CRC][ETX] */
    TEST_IGNORE_MESSAGE("TODO: build POWER_CTRL with LEN=4, correct CRC, assert p_ready");
}

void test_parser_zero_length_data(void)
{
    /* LEN=0 means no DATA bytes, jump straight to CRC */
    TEST_IGNORE_MESSAGE("TODO: verify parser handles LEN=0 (PING, GET_STATUS, etc.) correctly");
}

/* ===== Error handling ===== */

void test_parser_bad_crc_rejected(void)
{
    /* Correct framing but wrong CRC byte → p_ready stays 0 */
    TEST_IGNORE_MESSAGE("TODO: send packet with CRC+1, verify p_ready==0");
}

void test_parser_bad_etx_rejected(void)
{
    /* All correct except ETX != 0x03 → p_ready stays 0 */
    TEST_IGNORE_MESSAGE("TODO: replace ETX with 0xFF, verify p_ready==0");
}

void test_parser_garbage_before_stx_ignored(void)
{
    /* Random bytes before STX should not corrupt parser */
    TEST_IGNORE_MESSAGE("TODO: feed [0xAA, 0x55, 0xFF], then valid packet, assert p_ready");
}

void test_parser_oversized_data_rejected(void)
{
    /* LEN > PROTO_MAX_DATA (64) → parser resets */
    TEST_IGNORE_MESSAGE("TODO: send LEN=65, verify parser resets to WAIT_STX");
}

void test_parser_interbyte_timeout_resets(void)
{
    /* Advance systick_ms by >10ms between bytes → reset (Rules 4, config UART_INTERBYTE_TIMEOUT_MS) */
    TEST_IGNORE_MESSAGE("TODO: feed STX+CMD, advance time by 15ms, feed next byte, verify parser reset");
}

void test_parser_packet_timeout_resets(void)
{
    /* uart_protocol_process() checks >50ms since last byte → reset (UART_PACKET_TIMEOUT_MS) */
    TEST_IGNORE_MESSAGE("TODO: partial packet, advance 60ms, call uart_protocol_process(), verify reset");
}

/* ===== Command dispatch (via uart_protocol_process) ===== */

void test_dispatch_ping_responds_0xAA(void)
{
    /* PING → response DATA = [0xAA] (Rules 4.4: PING_RESPONSE) */
    TEST_IGNORE_MESSAGE("TODO: feed PING, call process, verify tx_buf contains 0xAA response");
}

void test_dispatch_unknown_cmd_nack(void)
{
    /* Unknown CMD=0xFE → respond CMD=0xFF, DATA=[0x01] */
    TEST_IGNORE_MESSAGE("TODO: feed unknown command, process, verify NACK response");
}

void test_dispatch_get_status_layout_26_bytes(void)
{
    /* GET_STATUS response must be exactly 26 bytes in contract order (Rules 4.5) */
    TEST_IGNORE_MESSAGE("TODO: set mock values, process GET_STATUS, verify tx_buf layout matches contract");
}

void test_dispatch_power_ctrl_bad_len_nack(void)
{
    /* POWER_CTRL with LEN!=4 → status=1 */
    TEST_IGNORE_MESSAGE("TODO: send POWER_CTRL with LEN=2, verify ACK with status=1");
}

void test_dispatch_set_brightness_over_1000_rejected(void)
{
    /* Brightness > 1000 → status=1 (Rules 4.5) */
    TEST_IGNORE_MESSAGE("TODO: send SET_BRIGHTNESS with value=1001, verify status=1");
}

void test_dispatch_set_brightness_valid(void)
{
    /* Brightness 0..1000 → status=0 */
    TEST_IGNORE_MESSAGE("TODO: send SET_BRIGHTNESS with value=500, verify status=0");
}

void test_dispatch_set_thresholds_validates_min_lt_max(void)
{
    /* Voltage threshold with min >= max → status=1 */
    TEST_IGNORE_MESSAGE("TODO: send SET_THRESHOLDS with min=5000, max=4000, verify status=1");
}

void test_dispatch_set_thresholds_rejects_unknown_bits(void)
{
    /* Mask with bit outside 0x1F0F → status=1 */
    TEST_IGNORE_MESSAGE("TODO: send SET_THRESHOLDS with mask=0x8000, verify status=1");
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    /* Parsing */
    RUN_TEST(test_parser_valid_ping_packet);
    RUN_TEST(test_parser_valid_get_status_request);
    RUN_TEST(test_parser_valid_power_ctrl);
    RUN_TEST(test_parser_zero_length_data);
    /* Error handling */
    RUN_TEST(test_parser_bad_crc_rejected);
    RUN_TEST(test_parser_bad_etx_rejected);
    RUN_TEST(test_parser_garbage_before_stx_ignored);
    RUN_TEST(test_parser_oversized_data_rejected);
    RUN_TEST(test_parser_interbyte_timeout_resets);
    RUN_TEST(test_parser_packet_timeout_resets);
    /* Dispatch */
    RUN_TEST(test_dispatch_ping_responds_0xAA);
    RUN_TEST(test_dispatch_unknown_cmd_nack);
    RUN_TEST(test_dispatch_get_status_layout_26_bytes);
    RUN_TEST(test_dispatch_power_ctrl_bad_len_nack);
    RUN_TEST(test_dispatch_set_brightness_over_1000_rejected);
    RUN_TEST(test_dispatch_set_brightness_valid);
    RUN_TEST(test_dispatch_set_thresholds_validates_min_lt_max);
    RUN_TEST(test_dispatch_set_thresholds_rejects_unknown_bits);
    return UNITY_END();
}
