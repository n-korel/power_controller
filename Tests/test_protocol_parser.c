/*
 * Unit tests: UART protocol parser state machine (Rules 4.1-4.5)
 *
 * Packet format: [STX=0x02][CMD][LEN][DATA...][CRC-8][ETX=0x03]
 * CRC is computed over [CMD][LEN][DATA...].
 */
#include "unity.h"
#include "config.h"
#include "stm32f0xx_hal.h"
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
static uint8_t  mock_power_ctrl_called;
static uint16_t mock_set_brightness_pwm;
static uint8_t  mock_set_brightness_called;
static uint8_t  mock_fault_clear_called;
static uint8_t  mock_power_reset_bridge_called;
static uint8_t  mock_power_safe_state_called;
static uint8_t  mock_bootloader_schedule_called;
static uint8_t  mock_flash_cal_calibrate_result;
static struct {
    uint16_t min_val;
    uint16_t max_val;
    uint8_t  idx;
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
    mock_power_ctrl_called = 1;
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

/* ===== Test helpers =====
 * After the RX-ISR refactor (Rules §1.1, invariant 8) the ISR only pushes
 * bytes into the ring buffer; the parser state machine runs in
 * uart_protocol_process(). For parser-state tests we push bytes through
 * the ring and manually drain them into parser_feed(), which matches the
 * first half of uart_protocol_process() but leaves dispatching to the
 * test itself (so parser queue state is observable). */

static uint8_t queued_packet_count(void)
{
    return pkt_q_count;
}

static const proto_packet_t *queued_packet_peek(void)
{
    if (pkt_q_count == 0U) {
        return NULL;
    }
    return &pkt_queue[pkt_q_tail];
}

static void drain_ring_into_parser(void)
{
    if (rx_overflow) {
        rx_overflow = 0;
        rx_tail = rx_head;
        p_state = PS_WAIT_STX;
        return;
    }
    while (rx_tail != rx_head) {
        uint8_t b = rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1U) & UART_RX_RING_MASK);
        parser_feed(b);
    }
}

static void feed_bytes(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        rx_byte = data[i];
        uart_protocol_rx_byte_cb();
    }
    drain_ring_into_parser();
}

static void parse_bytes_direct(const uint8_t *data, uint16_t len)
{
    p_state = PS_WAIT_STX;
    pkt_q_head = 0;
    pkt_q_tail = 0;
    pkt_q_count = 0;
    p_last_byte_ts = 0;
    p_data_cnt = 0;
    for (uint16_t i = 0; i < len; i++)
        parser_feed(data[i]);
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
    uint8_t crc_buf[PROTO_MAX_DATA + 2];
    crc_buf[0] = cmd;
    crc_buf[1] = len;
    if (len > 0)
        memcpy(&crc_buf[2], data, len);
    uint8_t crc = crc8_calc(crc_buf, (uint8_t)(2U + len));
    out[pos++] = crc;
    out[pos++] = PROTO_ETX;
    return pos;
}

static uint32_t hal_count_calls(hal_call_id_t id)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < hal_call_log_count; i++) {
        if (hal_call_log[i].id == id) {
            n++;
        }
    }
    return n;
}

void setUp(void)
{
    hal_stub_reset();
    uart_protocol_init();
    /* uart_protocol_init() does not touch these internals */
    rx_head = 0;
    rx_tail = 0;
    rx_overflow = 0;
    p_state = PS_WAIT_STX;
    pkt_q_head = 0;
    pkt_q_tail = 0;
    pkt_q_count = 0;
    pkt_q_overflow_nack_pending = 0;
    tx_busy_flag = 0;
    p_last_byte_ts = 0;
    p_data_cnt = 0;
    p_crc_rx = 0;
    systick_ms = 1000;
    mock_power_ctrl_result = 0;
    mock_power_ctrl_mask   = 0;
    mock_power_ctrl_value  = 0;
    mock_power_ctrl_called = 0;
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

    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
    const proto_packet_t *pkt0 = queued_packet_peek();
    TEST_ASSERT_NOT_NULL(pkt0);
    TEST_ASSERT_EQUAL_HEX8(CMD_PING, pkt0->cmd);
    TEST_ASSERT_EQUAL_UINT8(0, pkt0->len);
}

void test_hal_uart_init_arms_rx_once(void)
{
    TEST_ASSERT_EQUAL_UINT32(1, hal_count_calls(HAL_CALL_UART_RECEIVE_IT));
    TEST_ASSERT_TRUE(hal_call_log_count >= 1);
    TEST_ASSERT_EQUAL_INT(HAL_CALL_UART_RECEIVE_IT, hal_call_log[0].id);
    TEST_ASSERT_EQUAL_PTR(&huart1, (void *)hal_call_log[0].arg0);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)hal_call_log[0].arg2);
}

void test_parser_valid_get_status_request(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_GET_STATUS, NULL, 0);

    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
    const proto_packet_t *pkt0 = queued_packet_peek();
    TEST_ASSERT_NOT_NULL(pkt0);
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_STATUS, pkt0->cmd);
}

void test_parser_valid_power_ctrl(void)
{
    uint8_t data[4] = { 0x07, 0x00, 0x03, 0x00 }; /* mask=0x0007, value=0x0003 */
    uint8_t pkt[16];
    uint16_t n = build_packet(pkt, CMD_POWER_CTRL, data, 4);

    /* Direct-feed the parser to isolate from RX ring plumbing. */
    p_state = PS_WAIT_STX;
    pkt_q_head = 0;
    pkt_q_tail = 0;
    pkt_q_count = 0;
    p_last_byte_ts = 0;
    for (uint16_t i = 0; i < n; i++)
        parser_feed(pkt[i]);

    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
    const proto_packet_t *pkt0 = queued_packet_peek();
    TEST_ASSERT_NOT_NULL(pkt0);
    TEST_ASSERT_EQUAL_HEX8(CMD_POWER_CTRL, pkt0->cmd);
    TEST_ASSERT_EQUAL_UINT8(4, pkt0->len);
    TEST_ASSERT_EQUAL_MEMORY(data, pkt0->data, 4);
}

void test_parser_state_progression_len4_packet(void)
{
    uint8_t data[4] = { 0x07, 0x00, 0x03, 0x00 };
    uint8_t pkt[16];
    uint16_t n = build_packet(pkt, CMD_POWER_CTRL, data, 4);
    TEST_ASSERT_EQUAL_UINT16(9, n);

    p_state = PS_WAIT_STX;
    pkt_q_head = 0;
    pkt_q_tail = 0;
    pkt_q_count = 0;
    p_last_byte_ts = 0;
    p_data_cnt = 0;

    parser_feed(pkt[0]); /* STX */
    TEST_ASSERT_EQUAL_INT(PS_READ_CMD, p_state);
    parser_feed(pkt[1]); /* CMD */
    TEST_ASSERT_EQUAL_INT(PS_READ_LEN, p_state);
    parser_feed(pkt[2]); /* LEN */
    TEST_ASSERT_EQUAL_INT(PS_READ_DATA, p_state);
    TEST_ASSERT_EQUAL_UINT8(4, p_rx_pkt.len);

    parser_feed(pkt[3]);
    TEST_ASSERT_EQUAL_INT(PS_READ_DATA, p_state);
    TEST_ASSERT_EQUAL_UINT8(1, p_data_cnt);
    parser_feed(pkt[4]);
    TEST_ASSERT_EQUAL_UINT8(2, p_data_cnt);
    parser_feed(pkt[5]);
    TEST_ASSERT_EQUAL_UINT8(3, p_data_cnt);
    parser_feed(pkt[6]);
    TEST_ASSERT_EQUAL_INT(PS_READ_CRC, p_state);

    parser_feed(pkt[7]); /* CRC */
    TEST_ASSERT_EQUAL_INT(PS_WAIT_ETX, p_state);
    parser_feed(pkt[8]); /* ETX */
    TEST_ASSERT_EQUAL_INT(PS_WAIT_STX, p_state);
    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
}

void test_parser_zero_length_data(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_RESET_FAULT, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(5, n); /* STX+CMD+LEN+CRC+ETX */

    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
    const proto_packet_t *pkt0 = queued_packet_peek();
    TEST_ASSERT_NOT_NULL(pkt0);
    TEST_ASSERT_EQUAL_HEX8(CMD_RESET_FAULT, pkt0->cmd);
}

/* ===== Error handling ===== */

void test_parser_bad_crc_rejected(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_PING, NULL, 0);
    pkt[n - 2] ^= 0xFF;

    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(0, queued_packet_count());
}

void test_parser_bad_etx_rejected(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_PING, NULL, 0);
    pkt[n - 1] = 0xFF;

    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(0, queued_packet_count());
}

void test_parser_garbage_before_stx_ignored(void)
{
    uint8_t garbage[] = { 0xAA, 0x55, 0xFF };
    feed_bytes(garbage, sizeof(garbage));

    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_PING, NULL, 0);
    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
}

void test_parser_oversized_data_rejected(void)
{
    uint8_t prefix[] = { PROTO_STX, CMD_POWER_CTRL, 0x41 }; /* LEN=65 > 64 */
    feed_bytes(prefix, sizeof(prefix));

    TEST_ASSERT_EQUAL_INT(PS_WAIT_STX, p_state);
    TEST_ASSERT_EQUAL_UINT8(0, queued_packet_count());
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
    TEST_ASSERT_EQUAL_UINT8(0, queued_packet_count());
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

void test_hal_uart_rx_callback_rearms_receive_every_byte(void)
{
    hal_call_log_count = 0;

    rx_byte = 0x11; uart_protocol_rx_byte_cb();
    rx_byte = 0x22; uart_protocol_rx_byte_cb();
    rx_byte = 0x33; uart_protocol_rx_byte_cb();

    TEST_ASSERT_EQUAL_UINT32(3, hal_count_calls(HAL_CALL_UART_RECEIVE_IT));
    TEST_ASSERT_EQUAL_UINT32(0, hal_count_calls(HAL_CALL_UART_TRANSMIT_IT));
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

void test_hal_uart_dispatch_ping_calls_single_transmit_with_len6(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_PING, NULL, 0);
    hal_call_log_count = 0;

    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_UINT32(1, hal_count_calls(HAL_CALL_UART_TRANSMIT_IT));
    TEST_ASSERT_TRUE(hal_call_log_count >= 1);
    TEST_ASSERT_EQUAL_INT(HAL_CALL_UART_TRANSMIT_IT, hal_call_log[hal_call_log_count - 1].id);
    TEST_ASSERT_EQUAL_UINT32(6U, (uint32_t)hal_call_log[hal_call_log_count - 1].arg2);
}

void test_dispatch_unknown_cmd_nack(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, 0xFEU, NULL, 0);
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

    parse_bytes_direct(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(CMD_POWER_CTRL, tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx_buf[3]);
}

void test_dispatch_power_ctrl_rejects_unknown_bits_in_mask_or_value(void)
{
    /* Unknown bits must be rejected (Rules invariant #19: state bits 0..6 only). */
    mock_power_ctrl_result = 1; /* reject */

    uint16_t mask  = 0x0080; /* bit7 is unknown */
    uint16_t value = 0x0080;
    uint8_t data[4] = {
        (uint8_t)(mask & 0xFF), (uint8_t)(mask >> 8),
        (uint8_t)(value & 0xFF), (uint8_t)(value >> 8),
    };
    uint8_t pkt[16];
    uint16_t n = build_packet(pkt, CMD_POWER_CTRL, data, 4);

    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(CMD_POWER_CTRL, tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx_buf[3]);
    /* Protocol should still dispatch (power_ctrl_request called) but must reject. */
    TEST_ASSERT_EQUAL_UINT8(1, mock_power_ctrl_called);
}

void test_dispatch_set_brightness_over_1000_rejected(void)
{
    uint16_t pwm = 1001;
    uint8_t data[2] = { (uint8_t)(pwm & 0xFF), (uint8_t)(pwm >> 8) };
    uint8_t pkt[16];
    uint16_t n = build_packet(pkt, CMD_SET_BRIGHTNESS, data, 2);

    parse_bytes_direct(pkt, n);
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

    parse_bytes_direct(pkt, n);
    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
    const proto_packet_t *pkt0 = queued_packet_peek();
    TEST_ASSERT_NOT_NULL(pkt0);
    TEST_ASSERT_EQUAL_HEX8(CMD_SET_BRIGHTNESS, pkt0->cmd);
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

    parse_bytes_direct(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(CMD_SET_THRESHOLDS, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx_buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

void test_dispatch_set_thresholds_rejects_truncated_payload(void)
{
    /* mask selects V24 thresholds, but provides only min (missing max) */
    uint16_t mask = 0x0001;
    uint16_t mn = 20000;
    uint8_t data[] = {
        (uint8_t)(mask & 0xFF), (uint8_t)(mask >> 8),
        (uint8_t)(mn & 0xFF),   (uint8_t)(mn >> 8),
        /* missing max */
    };
    uint8_t pkt[32];
    uint16_t n = build_packet(pkt, CMD_SET_THRESHOLDS, data, sizeof(data));

    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(CMD_SET_THRESHOLDS, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx_buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

void test_dispatch_set_thresholds_rejects_extra_bytes(void)
{
    /* mask selects one current threshold (I_LCD max) = 2 bytes, but payload has extra tail */
    uint16_t mask = (1U << 8); /* current bit8 */
    uint16_t mx = 1000;
    uint8_t data[] = {
        (uint8_t)(mask & 0xFF), (uint8_t)(mask >> 8),
        (uint8_t)(mx & 0xFF),   (uint8_t)(mx >> 8),
        0xAA, 0x55, /* extra bytes must be rejected */
    };
    uint8_t pkt[32];
    uint16_t n = build_packet(pkt, CMD_SET_THRESHOLDS, data, sizeof(data));

    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_HEX8(CMD_SET_THRESHOLDS, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx_buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

void test_parser_resync_on_stx_after_bad_packet(void)
{
    /* Feed a bad packet (CRC mismatch), then a valid packet starting at an in-stream STX.
     * Parser must re-sync and accept the second packet. */
    uint8_t bad[16];
    uint16_t nb = build_packet(bad, CMD_PING, NULL, 0);
    bad[nb - 2] ^= 0xFF; /* corrupt CRC */

    uint8_t good[16];
    uint16_t ng = build_packet(good, CMD_GET_STATUS, NULL, 0);

    uint8_t stream[64];
    uint16_t pos = 0;
    memcpy(&stream[pos], bad, nb); pos += nb;
    memcpy(&stream[pos], good, ng); pos += ng;

    feed_bytes(stream, pos);

    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
    const proto_packet_t *pkt0 = queued_packet_peek();
    TEST_ASSERT_NOT_NULL(pkt0);
    TEST_ASSERT_EQUAL_HEX8(CMD_GET_STATUS, pkt0->cmd);
    TEST_ASSERT_EQUAL_UINT8(0, pkt0->len);
}

void test_dispatch_reset_fault_clears_flags_and_returns_ack(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_RESET_FAULT, NULL, 0);
    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_UINT8(1, mock_fault_clear_called);
    TEST_ASSERT_EQUAL_HEX8(PROTO_STX,       tx_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(CMD_RESET_FAULT, tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1,              tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00,            tx_buf[3]);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ETX,       tx_buf[5]);
}

void test_dispatch_reset_bridge_returns_ack_and_calls_power_reset_bridge(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_RESET_BRIDGE, NULL, 0);
    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_UINT8(1, mock_power_reset_bridge_called);
    TEST_ASSERT_EQUAL_HEX8(CMD_RESET_BRIDGE, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00,             tx_buf[3]);
}

void test_dispatch_calibrate_offset_forwards_result_code(void)
{
    /* Success path */
    mock_flash_cal_calibrate_result = 0;
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_CALIBRATE_OFFSET, NULL, 0);
    feed_bytes(pkt, n);
    uart_protocol_process();
    TEST_ASSERT_EQUAL_HEX8(CMD_CALIBRATE_OFFSET, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, tx_buf[3]);

    /* In the real system TX-busy is cleared by HAL_UART_TxCpltCallback. */
    uart_tx_cplt_cb();

    /* Failure path (e.g. PGOOD gate / flash error) */
    memset(tx_buf, 0, sizeof(tx_buf));
    mock_flash_cal_calibrate_result = 1;
    n = build_packet(pkt, CMD_CALIBRATE_OFFSET, NULL, 0);
    feed_bytes(pkt, n);
    uart_protocol_process();
    TEST_ASSERT_EQUAL_HEX8(CMD_CALIBRATE_OFFSET, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, tx_buf[3]);
}

void test_dispatch_bootloader_enter_safe_state_acks_and_schedules(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_BOOTLOADER_ENTER, NULL, 0);
    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_UINT8(1, mock_power_safe_state_called);
    TEST_ASSERT_EQUAL_HEX8(CMD_BOOTLOADER_ENTER, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, tx_buf[3]);
    TEST_ASSERT_EQUAL_UINT8(1, mock_bootloader_schedule_called);
}

/* ===== Frame layout (Rules §4.2) ===== */

void test_send_response_frame_layout(void)
{
    /* Verify exact byte-by-byte layout of [STX][CMD][LEN][DATA][CRC][ETX] */
    const uint8_t cmd = 0x42;
    const uint8_t payload[3] = { 0x11, 0x22, 0x33 };

    uart_send_response(cmd, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_HEX8(PROTO_STX, tx_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(cmd,       tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(3,        tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x11,      tx_buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x22,      tx_buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x33,      tx_buf[5]);

    uint8_t expected_crc = 0;
    expected_crc = crc8_table[expected_crc ^ cmd];
    expected_crc = crc8_table[expected_crc ^ 3];
    for (uint8_t i = 0; i < 3; i++)
        expected_crc = crc8_table[expected_crc ^ payload[i]];
    TEST_ASSERT_EQUAL_HEX8(expected_crc, tx_buf[6]);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ETX,    tx_buf[7]);

    TEST_ASSERT_EQUAL_UINT8(1, tx_busy_flag);
}

void test_send_ack_frame_is_fixed_6_bytes(void)
{
    const uint8_t cmd    = CMD_POWER_CTRL;
    const uint8_t status = 0x01;

    uart_send_ack(cmd, status);

    TEST_ASSERT_EQUAL_HEX8(PROTO_STX, tx_buf[0]);
    TEST_ASSERT_EQUAL_HEX8(cmd,       tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1,        tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(status,    tx_buf[3]);

    uint8_t expected_crc = 0;
    expected_crc = crc8_table[expected_crc ^ cmd];
    expected_crc = crc8_table[expected_crc ^ 1];
    expected_crc = crc8_table[expected_crc ^ status];
    TEST_ASSERT_EQUAL_HEX8(expected_crc, tx_buf[4]);
    TEST_ASSERT_EQUAL_HEX8(PROTO_ETX,    tx_buf[5]);
}

void test_ping_response_data_is_single_0xAA_byte(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_PING, NULL, 0);
    feed_bytes(pkt, n);
    uart_protocol_process();

    TEST_ASSERT_EQUAL_UINT8(1, tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(PING_RESPONSE, tx_buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0xAAU,         tx_buf[3]);

    uint8_t expected_crc = 0;
    expected_crc = crc8_table[expected_crc ^ CMD_PING];
    expected_crc = crc8_table[expected_crc ^ 1];
    expected_crc = crc8_table[expected_crc ^ PING_RESPONSE];
    TEST_ASSERT_EQUAL_HEX8(expected_crc, tx_buf[4]);
}

/* ===== RX ring buffer (Rules §1.1, invariant 8) ===== */

void test_rx_isr_does_not_touch_parser_state(void)
{
    /* After the refactor, the ISR only pushes into the ring.
     * Parser state must stay idle until uart_protocol_process() runs. */
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_PING, NULL, 0);

    for (uint16_t i = 0; i < n; i++) {
        rx_byte = pkt[i];
        uart_protocol_rx_byte_cb();
    }

    TEST_ASSERT_EQUAL_INT(PS_WAIT_STX, p_state);
    TEST_ASSERT_EQUAL_UINT8(0, queued_packet_count());

    uart_protocol_process();
    TEST_ASSERT_EQUAL_HEX8(CMD_PING, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(PING_RESPONSE, tx_buf[3]);
}

void test_rx_ring_overflow_sets_flag_and_resets_parser(void)
{
    /* Start a packet so parser is mid-state, then flood ring beyond capacity. */
    uint8_t prefix[] = { PROTO_STX, CMD_PING };
    feed_bytes(prefix, sizeof(prefix));
    TEST_ASSERT_EQUAL_INT(PS_READ_LEN, p_state);

    /* Stop draining: push more than UART_RX_RING_SIZE raw bytes directly. */
    for (uint16_t i = 0; i < UART_RX_RING_SIZE + 4; i++) {
        rx_byte = 0x55;
        uart_protocol_rx_byte_cb();
    }
    TEST_ASSERT_EQUAL_UINT8(1, rx_overflow);

    /* main loop: overflow path must clear flag, empty the ring and reset parser. */
    uart_protocol_process();
    TEST_ASSERT_EQUAL_UINT8(0, rx_overflow);
    TEST_ASSERT_EQUAL_INT(PS_WAIT_STX, p_state);
    TEST_ASSERT_EQUAL_UINT16(rx_head, rx_tail);
}

void test_packet_queue_overflow_sets_pending_nack(void)
{
    proto_packet_t pkt = { .cmd = CMD_PING, .len = 0 };

    pkt_q_head = 0;
    pkt_q_tail = 0;
    pkt_q_count = 0;
    pkt_q_overflow_nack_pending = 0;

    for (uint8_t i = 0; i < UART_PKT_QUEUE_SIZE; i++) {
        TEST_ASSERT_EQUAL_UINT8(1, packet_queue_push(&pkt));
    }
    TEST_ASSERT_EQUAL_UINT8(UART_PKT_QUEUE_SIZE, pkt_q_count);
    TEST_ASSERT_EQUAL_UINT8(0, pkt_q_overflow_nack_pending);

    TEST_ASSERT_EQUAL_UINT8(0, packet_queue_push(&pkt));
    TEST_ASSERT_EQUAL_UINT8(UART_PKT_QUEUE_SIZE, pkt_q_count);
    TEST_ASSERT_EQUAL_UINT8(1, pkt_q_overflow_nack_pending);
}

void test_dispatch_sends_nack_after_queue_overflow_when_queue_drained(void)
{
    proto_packet_t pkt = { .cmd = CMD_PING, .len = 0 };

    pkt_q_head = 0;
    pkt_q_tail = 0;
    pkt_q_count = 0;
    pkt_q_overflow_nack_pending = 0;
    tx_busy_flag = 0;
    memset(tx_buf, 0, sizeof(tx_buf));

    for (uint8_t i = 0; i < UART_PKT_QUEUE_SIZE; i++) {
        TEST_ASSERT_EQUAL_UINT8(1, packet_queue_push(&pkt));
    }
    TEST_ASSERT_EQUAL_UINT8(0, packet_queue_push(&pkt));
    TEST_ASSERT_EQUAL_UINT8(1, pkt_q_overflow_nack_pending);

    for (uint8_t i = 0; i < UART_PKT_QUEUE_SIZE; i++) {
        uart_protocol_process(); /* handles one queued packet (PING) per call */
        TEST_ASSERT_EQUAL_HEX8(CMD_PING, tx_buf[1]);
        uart_tx_cplt_cb();
    }

    memset(tx_buf, 0, sizeof(tx_buf));
    uart_protocol_process();
    TEST_ASSERT_EQUAL_HEX8(CMD_NACK, tx_buf[1]);
    TEST_ASSERT_EQUAL_UINT8(1, tx_buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x02, tx_buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0, pkt_q_overflow_nack_pending);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hal_uart_init_arms_rx_once);
    RUN_TEST(test_parser_valid_ping_packet);
    RUN_TEST(test_parser_valid_get_status_request);
    RUN_TEST(test_parser_valid_power_ctrl);
    RUN_TEST(test_parser_state_progression_len4_packet);
    RUN_TEST(test_parser_zero_length_data);
    RUN_TEST(test_parser_bad_crc_rejected);
    RUN_TEST(test_parser_bad_etx_rejected);
    RUN_TEST(test_parser_garbage_before_stx_ignored);
    RUN_TEST(test_parser_oversized_data_rejected);
    RUN_TEST(test_parser_interbyte_timeout_resets);
    RUN_TEST(test_parser_packet_timeout_resets);
    RUN_TEST(test_hal_uart_rx_callback_rearms_receive_every_byte);
    RUN_TEST(test_dispatch_ping_responds_0xAA);
    RUN_TEST(test_hal_uart_dispatch_ping_calls_single_transmit_with_len6);
    RUN_TEST(test_dispatch_unknown_cmd_nack);
    RUN_TEST(test_dispatch_get_status_layout_26_bytes);
    RUN_TEST(test_dispatch_power_ctrl_bad_len_nack);
    RUN_TEST(test_dispatch_power_ctrl_rejects_unknown_bits_in_mask_or_value);
    RUN_TEST(test_dispatch_set_brightness_over_1000_rejected);
    RUN_TEST(test_dispatch_set_brightness_valid);
    RUN_TEST(test_dispatch_set_thresholds_validates_min_lt_max);
    RUN_TEST(test_dispatch_set_thresholds_rejects_unknown_bits);
    RUN_TEST(test_dispatch_set_thresholds_rejects_truncated_payload);
    RUN_TEST(test_dispatch_set_thresholds_rejects_extra_bytes);
    RUN_TEST(test_dispatch_reset_fault_clears_flags_and_returns_ack);
    RUN_TEST(test_dispatch_reset_bridge_returns_ack_and_calls_power_reset_bridge);
    RUN_TEST(test_dispatch_calibrate_offset_forwards_result_code);
    RUN_TEST(test_dispatch_bootloader_enter_safe_state_acks_and_schedules);
    RUN_TEST(test_send_response_frame_layout);
    RUN_TEST(test_send_ack_frame_is_fixed_6_bytes);
    RUN_TEST(test_ping_response_data_is_single_0xAA_byte);
    RUN_TEST(test_rx_isr_does_not_touch_parser_state);
    RUN_TEST(test_rx_ring_overflow_sets_flag_and_resets_parser);
    RUN_TEST(test_packet_queue_overflow_sets_pending_nack);
    RUN_TEST(test_dispatch_sends_nack_after_queue_overflow_when_queue_drained);
    RUN_TEST(test_parser_resync_on_stx_after_bad_packet);
    return UNITY_END();
}
