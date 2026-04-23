#include "unity.h"
#include "stm32f0xx_hal.h"
#include "config.h"
#include <string.h>

/* Provide minimal mocks required by uart_protocol.c */
uint16_t adc_get_voltage_mv(uint8_t idx) { (void)idx; return 0; }
int16_t  adc_get_current_ma(uint8_t idx) { (void)idx; return 0; }
int16_t  adc_get_temp(uint8_t idx)       { (void)idx; return -32768; }
uint8_t  power_get_state(void)           { return 0; }
uint16_t fault_get_flags(void)           { return 0; }
uint8_t  input_get_packed(void)          { return 0; }
uint8_t  power_ctrl_request(uint16_t m, uint16_t v) { (void)m; (void)v; return 1; }
void     power_set_brightness(uint16_t p) { (void)p; }
void     fault_clear_flags(void) {}
void     power_reset_bridge(void) {}
void     fault_set_threshold(uint8_t i, uint16_t mn, uint16_t mx) { (void)i; (void)mn; (void)mx; }
void     power_safe_state(void) {}
void     bootloader_schedule(void) {}
uint8_t  flash_cal_calibrate(void) { return 1; }
uint16_t adc_get_raw_avg(uint8_t idx) { (void)idx; return 0; }

volatile uint32_t systick_ms;

static volatile uint32_t error_handler_count;

void Error_Handler(void)
{
    error_handler_count++;
}

#include "uart_protocol.c"

static uint32_t hal_count_calls(hal_call_id_t id)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < hal_call_log_count; i++) {
        if (hal_call_log[i].id == id) n++;
    }
    return n;
}

void setUp(void)
{
    hal_stub_reset();
    systick_ms = 1000;
    error_handler_count = 0;
    memset(tx_buf, 0, sizeof(tx_buf));
    tx_busy_flag = 0;
    p_state = PS_WAIT_STX;
    rx_head = 0;
    rx_tail = 0;
    rx_overflow = 0;
    pkt_q_head = 0;
    pkt_q_tail = 0;
    pkt_q_count = 0;
    pkt_q_overflow_nack_pending = 0;
    p_last_byte_ts = 0;
    p_data_cnt = 0;
    p_crc_rx = 0;
}

void tearDown(void) {}

void test_uart_init_receive_it_error_calls_error_handler(void)
{
    hal_stub_ret_uart_receive_it = HAL_ERROR;
    uart_protocol_init();
    TEST_ASSERT_EQUAL_UINT32(1, error_handler_count);
}

void test_uart_rx_callback_receive_it_error_calls_error_handler(void)
{
    uart_protocol_init();
    error_handler_count = 0;
    hal_stub_ret_uart_receive_it = HAL_ERROR;

    rx_byte = 0x11;
    uart_protocol_rx_byte_cb();
    TEST_ASSERT_EQUAL_UINT32(1, error_handler_count);
}

void test_uart_tx_busy_suppresses_transmit_call(void)
{
    uart_protocol_init();
    hal_call_log_count = 0;

    tx_busy_flag = 1;
    uart_send_ack(CMD_PING, PING_RESPONSE);
    TEST_ASSERT_EQUAL_UINT32(0, hal_count_calls(HAL_CALL_UART_TRANSMIT_IT));
}

void test_uart_transmit_it_error_calls_error_handler_and_clears_busy(void)
{
    uart_protocol_init();
    hal_stub_ret_uart_transmit_it = HAL_ERROR;

    uart_send_ack(CMD_PING, PING_RESPONSE);
    TEST_ASSERT_EQUAL_UINT32(1, error_handler_count);
    TEST_ASSERT_EQUAL_UINT8(0, tx_busy_flag);

    /* After error path cleared busy, subsequent send is allowed (even if it errors again). */
    error_handler_count = 0;
    uart_send_ack(CMD_PING, PING_RESPONSE);
    TEST_ASSERT_EQUAL_UINT32(1, error_handler_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_uart_init_receive_it_error_calls_error_handler);
    RUN_TEST(test_uart_rx_callback_receive_it_error_calls_error_handler);
    RUN_TEST(test_uart_tx_busy_suppresses_transmit_call);
    RUN_TEST(test_uart_transmit_it_error_calls_error_handler_and_clears_busy);
    return UNITY_END();
}

