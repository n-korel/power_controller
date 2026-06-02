/*
 * Unit tests: BOOTLOADER_ENTER must not run safe_state / bootloader_schedule
 * while UART TX is busy (uart_protocol_process early return).
 *
 * On hardware, reset before ACK is not observable; this guards the contract
 * that dispatch is deferred until TX completes so ACK can be sent first.
 */
#include "unity.h"
#include "config.h"
#include "stm32f0xx_hal.h"
#include <string.h>

static uint8_t mock_power_safe_state_called;
static uint8_t mock_bootloader_schedule_called;

uint16_t adc_get_voltage_mv(uint8_t idx) { (void)idx; return 0; }
int16_t  adc_get_current_ma(uint8_t idx) { (void)idx; return 0; }
int16_t  adc_get_temp(uint8_t idx)       { (void)idx; return -32768; }
uint8_t  power_get_state(void)           { return 0; }
uint8_t  power_get_dseq_raw(void)        { return 0; }
uint8_t  power_get_last_power_ctrl_mask_lo(void)  { return 0; }
uint8_t  power_get_last_power_ctrl_value_lo(void) { return 0; }
uint32_t power_get_reset_flags_raw(void) { return 0; }
uint32_t power_get_boot_counter(void)    { return 0; }
uint16_t fault_get_flags(void)           { return 0; }
uint8_t  input_get_packed(void)          { return 0; }
uint8_t  power_ctrl_request(uint16_t m, uint16_t v) { (void)m; (void)v; return 0; }
void     power_set_brightness(uint16_t p) { (void)p; }
void     fault_clear_flags(void) {}
void     fault_set_flag(uint16_t flag) { (void)flag; }
uint8_t  power_reset_bridge(void) { return 0; }
void     fault_set_threshold(uint8_t i, uint16_t mn, uint16_t mx)
{
    (void)i; (void)mn; (void)mx;
}
void     power_safe_state(void)    { mock_power_safe_state_called = 1; }
void     bootloader_schedule(void) { mock_bootloader_schedule_called = 1; }
uint8_t  flash_cal_calibrate(void) { return 0; }
uint16_t adc_get_raw_avg(uint8_t idx) { (void)idx; return 0; }

volatile uint32_t systick_ms;

#include "uart_protocol.c"

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
    if (len > 0) {
        memcpy(&crc_buf[2], data, len);
    }
    out[pos++] = crc8_calc(crc_buf, (uint8_t)(2U + len));
    out[pos++] = PROTO_ETX;
    return pos;
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

static uint8_t queued_packet_count(void)
{
    return pkt_q_count;
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
    systick_ms = 1000;
    mock_power_safe_state_called = 0;
    mock_bootloader_schedule_called = 0;
    memset(tx_buf, 0, sizeof(tx_buf));
}

void tearDown(void) {}

void test_bootloader_enter_tx_busy_defers_safe_state_and_schedule(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_BOOTLOADER_ENTER, NULL, 0);
    feed_bytes(pkt, n);

    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
    tx_busy_flag = 1;

    for (uint8_t i = 0; i < 5; i++) {
        uart_protocol_process();
    }

    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
    TEST_ASSERT_EQUAL_UINT8(0, mock_power_safe_state_called);
    TEST_ASSERT_EQUAL_UINT8(0, mock_bootloader_schedule_called);
    TEST_ASSERT_EQUAL_UINT32(0, hal_count_calls(HAL_CALL_UART_TRANSMIT_IT));
}

void test_bootloader_enter_after_tx_cplt_runs_safe_state_ack_schedule(void)
{
    uint8_t pkt[8];
    uint16_t n = build_packet(pkt, CMD_BOOTLOADER_ENTER, NULL, 0);
    feed_bytes(pkt, n);

    tx_busy_flag = 1;
    uart_protocol_process();
    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
    TEST_ASSERT_EQUAL_UINT8(0, mock_power_safe_state_called);

    uart_tx_cplt_cb();
    hal_call_log_count = 0;
    uart_protocol_process();

    TEST_ASSERT_EQUAL_UINT8(0, queued_packet_count());
    TEST_ASSERT_EQUAL_UINT8(1, mock_power_safe_state_called);
    TEST_ASSERT_EQUAL_HEX8(CMD_BOOTLOADER_ENTER, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, tx_buf[3]);
    TEST_ASSERT_EQUAL_UINT8(1, mock_bootloader_schedule_called);
    TEST_ASSERT_EQUAL_UINT32(1, hal_count_calls(HAL_CALL_UART_TRANSMIT_IT));
    TEST_ASSERT_EQUAL_UINT8(1, tx_busy_flag);
}

void test_bootloader_enter_while_prior_tx_finishes_then_dispatches(void)
{
    uint8_t ping_pkt[8];
    uint8_t bl_pkt[8];
    uint16_t ping_n = build_packet(ping_pkt, CMD_PING, NULL, 0);
    uint16_t bl_n   = build_packet(bl_pkt, CMD_BOOTLOADER_ENTER, NULL, 0);

    feed_bytes(ping_pkt, ping_n);
    uart_protocol_process();
    TEST_ASSERT_EQUAL_UINT8(1, tx_busy_flag);
    TEST_ASSERT_EQUAL_HEX8(CMD_PING, tx_buf[1]);

    feed_bytes(bl_pkt, bl_n);
    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());

    uart_protocol_process();
    TEST_ASSERT_EQUAL_UINT8(1, queued_packet_count());
    TEST_ASSERT_EQUAL_UINT8(0, mock_power_safe_state_called);
    TEST_ASSERT_EQUAL_UINT8(0, mock_bootloader_schedule_called);

    uart_tx_cplt_cb();
    uart_protocol_process();

    TEST_ASSERT_EQUAL_UINT8(0, queued_packet_count());
    TEST_ASSERT_EQUAL_UINT8(1, mock_power_safe_state_called);
    TEST_ASSERT_EQUAL_HEX8(CMD_BOOTLOADER_ENTER, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, tx_buf[3]);
    TEST_ASSERT_EQUAL_UINT8(1, mock_bootloader_schedule_called);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bootloader_enter_tx_busy_defers_safe_state_and_schedule);
    RUN_TEST(test_bootloader_enter_after_tx_cplt_runs_safe_state_ack_schedule);
    RUN_TEST(test_bootloader_enter_while_prior_tx_finishes_then_dispatches);
    return UNITY_END();
}
