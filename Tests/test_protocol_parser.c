/*
 * Unit tests: UART protocol parser state machine (Rules 4.1-4.5)
 *
 * Packet format: [STX=0x02][CMD][LEN][DATA...][CRC-8][ETX=0x03]
 * CRC is computed over [CMD][LEN][DATA...].
 */
#include "unity.h"
#include "config.h"
#include <string.h>

/* --- Mocks for uart_protocol.c dependencies --- */
static uint16_t mock_voltage_mv[4];
static int16_t  mock_current_ma[5];
static int16_t  mock_temp[2];
static uint8_t  mock_power_state;
static uint16_t mock_fault_flags;
static uint8_t  mock_input_packed;
static uint8_t  mock_power_ctrl_result;
static uint16_t mock_power_ctrl_mask;
static uint16_t mock_power_ctrl_value;
static uint16_t mock_set_brightness_pwm;
static uint8_t  mock_set_brightness_called;
static uint8_t  mock_fault_clear_called;
static uint8_t  mock_power_reset_bridge_called;
static uint8_t  mock_power_safe_state_called;
static uint8_t  mock_bootloader_schedule_called;
static uint8_t  mock_flash_cal_calibrate_result;
static struct {
    uint8_t  idx;
    uint16_t min_val;
    uint16_t max_val;
    uint8_t  called;
} mock_thresh[16];
static uint8_t mock_thresh_count;

uint16_t adc_get_voltage_mv(uint8_t idx) { return (idx < 4) ? mock_voltage_mv[idx] : 0; }
int16_t  adc_get_current_ma(uint8_t idx) { return (idx < 5) ? mock_current_ma[idx] : 0; }
int16_t  adc_get_temp(uint8_t idx)       { return (idx < 2) ? mock_temp[idx] : -32768; }
uint8_t  power_get_state(void)           { return mock_power_state; }
uint16_t fault_get_flags(void)           { return mock_fault_flags; }
uint8_t  input_get_packed(void)          { return mock_input_packed; }

uint8_t  power_ctrl_request(uint16_t m, uint16_t v)
{
    mock_power_ctrl_mask  = m;
    mock_power_ctrl_value = v;
    return mock_power_ctrl_result;
}
void     power_set_brightness(uint16_t p)
{
    mock_set_brightness_pwm    = p;
    mock_set_brightness_called = 1;
}
void     fault_clear_flags(void)         { mock_fault_clear_called = 1; }
void     power_reset_bridge(void)        { mock_power_reset_bridge_called = 1; }
void     fault_set_threshold(uint8_t i, uint16_t mn, uint16_t mx)
{
    if (mock_thresh_count < 16) {
        mock_thresh[mock_thresh_count].idx     = i;
        mock_thresh[mock_thresh_count].min_val = mn;
        mock_thresh[mock_thresh_count].max_val = mx;
        mock_thresh[mock_thresh_count].called  = 1;
        mock_thresh_count++;
    }
}
void     power_safe_state(void)          { mock_power_safe_state_called = 1; }
void     bootloader_schedule(void)       { mock_bootloader_schedule_called = 1; }
uint8_t  flash_cal_calibrate(void)       { return mock_flash_cal_calibrate_result; }
uint16_t adc_get_raw_avg(uint8_t idx)    { (void)idx; return 0; }

volatile uint32_t systick_ms;

#include "uart_protocol.c"

/* ===== Test helpers ===== */

static void feed_bytes(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        rx_byte = data[i];
        uart_protocol_rx_byte_cb();
    }
}

/* Build a full packet [STX][CMD][LEN][DATA][CRC][ETX] into out, return total length */
static uint16_t build_packet(uint8_t *out, uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint16_t pos = 0;
    out[pos++] = PROTO_STX;
    out[pos++] = cmd;
    out[pos++] = len;
    if (len > 0) {
        memcpy(&out[pos], data, len);
        pos += len;
    }
    uint8_t crc = 0;
    crc = crc8_table[crc ^ cmd];
    crc = crc8_table[crc ^ len];
    for (uint8_t i = 0; i < len; i++)
        crc = crc8_table[crc ^ data[i]];
    out[pos++] = crc;
    out[pos++] = PROTO_ETX;
    return pos;
}

void setUp(void)
{
    p_state = PS_WAIT_STX;
    p_ready = 0;
    tx_busy_flag = 0;
    systick_ms = 1000;
    mock_power_ctrl_result = 0;
    mock_power_ctrl_mask   = 0;
    mock_power_ctrl_value  = 0;
    mock_set_brightness_pwm    = 0xFFFF;
    mock_set_brightness_called = 0;
    mock_fault_clear_called       = 0;
    mock_power_reset_bridge_called = 0;
    mock_power_safe_state_called   = 0;
    mock_bootloader_schedule_called = 0;
    mock_flash_cal_calibrate_result = 0;
    mock_thresh_count = 0;
    memset(mock_thresh, 0, sizeof(mock_thresh));
    memset(tx_buf, 0, sizeof(tx_buf));
    for (uint8_t i = 0; i < 4; i++) mock_voltage_mv[i] = 0;
    for (uint8_t i = 0; i < 5; i++) mock_current_ma[i] = 0;
    mock_temp[0] = -32768; mock_temp[1] = -32768;
    mock_power_state  = 0;
    mock_fault_flags  = 0;
    mock_input_packed = 0;
}

void tearDown(void) {}

/* ===== Basic parsing ===== */

void test_parser_valid_ping_packet(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_PING, NULL, 0);

    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(1, p_ready);
    TEST_ASSERT_EQUAL_HEX8(CMD_PING, p_pkt.cmd);
    TEST_ASSERT_EQUAL_UINT8(0, p_pkt.len);
}

void test_parser_valid_get_status_request(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_GET_STATUS, NULL, 0);

    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(1, p_ready);
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_STATUS, p_pkt.cmd);
}

void test_parser_valid_power_ctrl(void)
{
    uint8_t data[4] = { 0x07, 0x00, 0x03, 0x00 }; /* mask=0x0007, value=0x0003 */
    uint8_t pkt[16];
    uint16_t n = build_packet(pkt, CMD_POWER_CTRL, data, 4);

    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(1, p_ready);
    TEST_ASSERT_EQUAL_HEX8(CMD_POWER_CTRL, p_pkt.cmd);
    TEST_ASSERT_EQUAL_UINT8(4, p_pkt.len);
    TEST_ASSERT_EQUAL_MEMORY(data, p_pkt.data, 4);
}

void test_parser_zero_length_data(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_RESET_FAULT, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(5, n); /* STX+CMD+LEN+CRC+ETX */

    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(1, p_ready);
    TEST_ASSERT_EQUAL_HEX8(CMD_RESET_FAULT, p_pkt.cmd);
}

/* ===== Error handling ===== */

void test_parser_bad_crc_rejected(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_PING, NULL, 0);
    pkt[n - 2] ^= 0xFF;

    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(0, p_ready);
}

void test_parser_bad_etx_rejected(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_PING, NULL, 0);
    pkt[n - 1] = 0xFF;

    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(0, p_ready);
}

void test_parser_garbage_before_stx_ignored(void)
{
    uint8_t garbage[] = { 0xAA, 0x55, 0xFF };
    feed_bytes(garbage, sizeof(garbage));

    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_PING, NULL, 0);
    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(1, p_ready);
}

void test_parser_oversized_data_rejected(void)
{
    uint8_t prefix[] = { PROTO_STX, CMD_POWER_CTRL, 0x41 }; /* LEN=65 > 64 */
    feed_bytes(prefix, sizeof(prefix));

    TEST_ASSERT_EQUAL_INT(PS_WAIT_STX, p_state);
    TEST_ASSERT_EQUAL_UINT8(0, p_ready);
}

void test_parser_interbyte_timeout_resets(void)
{
    uint8_t first[] = { PROTO_STX, CMD_PING };
    feed_bytes(first, sizeof(first));
    TEST_ASSERT_EQUAL_INT(PS_READ_LEN, p_state);

    systick_ms += UART_INTERBYTE_TIMEOUT_MS + 5;

    uint8_t next[] = { 0x00 };
    feed_bytes(next, 1);

    /* Timeout aborted the prior packet; this byte becomes a non-STX → parser stays idle */
    TEST_ASSERT_EQUAL_INT(PS_WAIT_STX, p_state);
    TEST_ASSERT_EQUAL_UINT8(0, p_ready);
}

void test_parser_packet_timeout_resets(void)
{
    uint8_t partial[] = { PROTO_STX, CMD_PING };
    feed_bytes(partial, sizeof(partial));
    TEST_ASSERT_NOT_EQUAL(PS_WAIT_STX, p_state);

    systick_ms += UART_PACKET_TIMEOUT_MS + 10;
    uart_protocol_process();

    TEST_ASSERT_EQUAL_INT(PS_WAIT_STX, p_state);
}

/* ===== Command dispatch ===== */

void test_dispatch_ping_responds_0xAA(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_PING, NULL, 0);
    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(PROTO_STX, tx_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(CMD_PING,  tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1,        tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(PING_RESPONSE, tx_buf[3]);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ETX, tx_buf[5]);
}

void test_dispatch_unknown_cmd_nack(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, 0xFEu, NULL, 0);
    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(PROTO_STX, tx_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(CMD_NACK,  tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1,        tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01,      tx_buf[3]);
}

void test_dispatch_get_status_layout_26_bytes(void)
{
    mock_voltage_mv[0] = 24000;
    mock_voltage_mv[1] = 12000;
    mock_voltage_mv[2] = 5000;
    mock_voltage_mv[3] = 3300;
    mock_current_ma[0] = 500;
    mock_current_ma[1] = -10;
    mock_current_ma[2] = 1200;
    mock_current_ma[3] = 50;
    mock_current_ma[4] = 60;
    mock_temp[0] = -32768;
    mock_temp[1] = -32768;
    mock_power_state  = 0x47;
    mock_fault_flags  = 0x1234;
    mock_input_packed = 0xA5;

    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_GET_STATUS, NULL, 0);
    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(PROTO_STX, tx_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_STATUS, tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(GET_STATUS_DATA_LEN, tx_buf[2]);

    const uint8_t *d = &tx_buf[3];
    TEST_ASSERT_EQUAL_HEX16(24000, (uint16_t)d[0] | ((uint16_t)d[1] << 8));
    TEST_ASSERT_EQUAL_HEX16(12000, (uint16_t)d[2] | ((uint16_t)d[3] << 8));
    TEST_ASSERT_EQUAL_HEX16(5000,  (uint16_t)d[4] | ((uint16_t)d[5] << 8));
    TEST_ASSERT_EQUAL_HEX16(3300,  (uint16_t)d[6] | ((uint16_t)d[7] << 8));
    TEST_ASSERT_EQUAL_INT16(500,   (int16_t)((uint16_t)d[8]  | ((uint16_t)d[9] << 8)));
    TEST_ASSERT_EQUAL_INT16(-10,   (int16_t)((uint16_t)d[10] | ((uint16_t)d[11] << 8)));
    TEST_ASSERT_EQUAL_INT16(1200,  (int16_t)((uint16_t)d[12] | ((uint16_t)d[13] << 8)));
    TEST_ASSERT_EQUAL_INT16(50,    (int16_t)((uint16_t)d[14] | ((uint16_t)d[15] << 8)));
    TEST_ASSERT_EQUAL_INT16(60,    (int16_t)((uint16_t)d[16] | ((uint16_t)d[17] << 8)));
    TEST_ASSERT_EQUAL_INT16(-32768, (int16_t)((uint16_t)d[18] | ((uint16_t)d[19] << 8)));
    TEST_ASSERT_EQUAL_INT16(-32768, (int16_t)((uint16_t)d[20] | ((uint16_t)d[21] << 8)));
    TEST_ASSERT_EQUAL_HEX8(0x47, d[22]);
    TEST_ASSERT_EQUAL_HEX16(0x1234, (uint16_t)d[23] | ((uint16_t)d[24] << 8));
    TEST_ASSERT_EQUAL_HEX8(0xA5, d[25]);

    TEST_ASSERT_EQUAL_HEX8(PROTO_ETX, tx_buf[3 + GET_STATUS_DATA_LEN + 1]);
}

void test_dispatch_power_ctrl_bad_len_nack(void)
{
    uint8_t data[2] = { 0x00, 0x00 };
    uint8_t pkt[16];
    uint16_t n = build_packet(pkt, CMD_POWER_CTRL, data, 2);

    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(CMD_POWER_CTRL, tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx_buf[3]);
}

void test_dispatch_set_brightness_over_1000_rejected(void)
{
    uint16_t pwm = 1001;
    uint8_t data[2] = { (uint8_t)(pwm & 0xFF), (uint8_t)(pwm >> 8) };
    uint8_t pkt[16];
    uint16_t n = build_packet(pkt, CMD_SET_BRIGHTNESS, data, 2);

    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(CMD_SET_BRIGHTNESS, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx_buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0, mock_set_brightness_called);
}

void test_dispatch_set_brightness_valid(void)
{
    uint16_t pwm = 500;
    uint8_t data[2] = { (uint8_t)(pwm & 0xFF), (uint8_t)(pwm >> 8) };
    uint8_t pkt[16];
    uint16_t n = build_packet(pkt, CMD_SET_BRIGHTNESS, data, 2);

    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(CMD_SET_BRIGHTNESS, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, tx_buf[3]);
    TEST_ASSERT_EQUAL_UINT8(1, mock_set_brightness_called);
    TEST_ASSERT_EQUAL_UINT16(500, mock_set_brightness_pwm);
}

void test_dispatch_set_thresholds_validates_min_lt_max(void)
{
    /* mask=0x0001 (bit0 = V24), min=5000, max=4000 → invalid */
    uint16_t mask = 0x0001;
    uint16_t mn = 5000, mx = 4000;
    uint8_t data[] = {
        (uint8_t)(mask & 0xFF), (uint8_t)(mask >> 8),
        (uint8_t)(mn & 0xFF),   (uint8_t)(mn >> 8),
        (uint8_t)(mx & 0xFF),   (uint8_t)(mx >> 8),
    };
    uint8_t pkt[32];
    uint16_t n = build_packet(pkt, CMD_SET_THRESHOLDS, data, sizeof(data));

    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(CMD_SET_THRESHOLDS, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx_buf[3]);
}

void test_dispatch_set_thresholds_rejects_unknown_bits(void)
{
    uint16_t mask = 0x8000;
    uint8_t data[] = { (uint8_t)(mask & 0xFF), (uint8_t)(mask >> 8) };
    uint8_t pkt[16];
    uint16_t n = build_packet(pkt, CMD_SET_THRESHOLDS, data, sizeof(data));

    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(CMD_SET_THRESHOLDS, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx_buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parser_valid_ping_packet);
    RUN_TEST(test_parser_valid_get_status_request);
    RUN_TEST(test_parser_valid_power_ctrl);
    RUN_TEST(test_parser_zero_length_data);
    RUN_TEST(test_parser_bad_crc_rejected);
    RUN_TEST(test_parser_bad_etx_rejected);
    RUN_TEST(test_parser_garbage_before_stx_ignored);
    RUN_TEST(test_parser_oversized_data_rejected);
    RUN_TEST(test_parser_interbyte_timeout_resets);
    RUN_TEST(test_parser_packet_timeout_resets);
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
