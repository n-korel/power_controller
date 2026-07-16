/*
 * Unit tests: CMD_SET_THRESHOLDS handler validation (handle_set_thresholds).
 *
 * Covers payload length rules, current max (mx != 0, mx <= INT16_MAX),
 * mixed voltage (bits 0-3) + current (bits 8-12) masks, and zero-mask no-op.
 */
#include "unity.h"
#include "config.h"
#include "stm32f0xx_hal.h"
#include <limits.h>
#include <string.h>

static uint16_t mock_voltage_mv[4];
static int16_t  mock_current_ma[5];
static int16_t  mock_temp[2];
static uint8_t  mock_power_state;
static uint16_t mock_fault_flags;
static uint8_t  mock_input_packed;
static uint8_t  mock_power_ctrl_result;
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
uint8_t  power_ctrl_request(uint16_t m, uint16_t v) { (void)m; (void)v; return mock_power_ctrl_result; }
void     power_set_brightness(uint16_t p) { (void)p; }
void     fault_clear_flags(void) {}
void     fault_set_flag(uint16_t flag) { (void)flag; }
uint8_t  power_reset_bridge(void) { return 0; }
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
void     power_safe_state(void) {}
void     bootloader_enter_request(void) {}
uint8_t  flash_cal_calibrate(void) { return 0; }
void     boot_meta_confirm(void) {}
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
    if (len > 0)
        memcpy(&crc_buf[2], data, len);
    uint8_t crc = crc8_calc(crc_buf, (uint8_t)(2U + len));
    out[pos++] = crc;
    out[pos++] = PROTO_ETX;
    return pos;
}

static void dispatch_set_thresholds(const uint8_t *data, uint8_t len)
{
    uint8_t pkt[PROTO_MAX_DATA + 8];
    uint16_t n = build_packet(pkt, CMD_SET_THRESHOLDS, data, len);
    p_state = PS_WAIT_STX;
    pkt_q_head = 0;
    pkt_q_tail = 0;
    pkt_q_count = 0;
    for (uint16_t i = 0; i < n; i++)
        parser_feed(pkt[i]);
    uart_protocol_process();
}

static void append_u16(uint8_t *buf, uint8_t *idx, uint16_t v)
{
    buf[(*idx)++] = (uint8_t)(v & 0xFF);
    buf[(*idx)++] = (uint8_t)(v >> 8);
}

static uint8_t ack_status(void)
{
    return tx_buf[3];
}

void setUp(void)
{
    hal_stub_reset();
    uart_protocol_init();
    p_state = PS_WAIT_STX;
    pkt_q_head = 0;
    pkt_q_tail = 0;
    pkt_q_count = 0;
    tx_busy_flag = 0;
    mock_thresh_count = 0;
    memset(mock_thresh, 0, sizeof(mock_thresh));
    memset(tx_buf, 0, sizeof(tx_buf));
}

void tearDown(void) {}

/* ===== Zero mask ===== */

void test_set_thresholds_zero_mask_is_no_op(void)
{
    uint8_t data[2];
    uint8_t pos = 0;
    append_u16(data, &pos, 0U);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(CMD_SET_THRESHOLDS, tx_buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, ack_status());
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

/* ===== Current max validation (bits 8-12) ===== */

void test_set_thresholds_rejects_current_mx_zero(void)
{
    uint16_t mask = (1U << 8);
    uint8_t data[8];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, 0U);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x01, ack_status());
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

void test_set_thresholds_rejects_current_mx_above_int16_max(void)
{
    uint16_t mask = (1U << 8);
    uint16_t mx = (uint16_t)INT16_MAX + 1U;
    uint8_t data[8];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, mx);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x01, ack_status());
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

/* ===== Voltage min < max ===== */

void test_set_thresholds_rejects_voltage_min_ge_max(void)
{
    uint16_t mask = 0x0001;
    uint8_t data[8];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, 5000U);
    append_u16(data, &pos, 4000U);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x01, ack_status());
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

/* ===== Strict payload length ===== */

void test_set_thresholds_rejects_truncated_voltage_pair(void)
{
    /* bits 0+1 selected → need 2 mask + 8 voltage bytes; only one pair provided */
    uint16_t mask = 0x0003;
    uint8_t data[16];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, THRESH_V24_MIN);
    append_u16(data, &pos, THRESH_V24_MAX);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x01, ack_status());
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

void test_set_thresholds_rejects_extra_bytes_after_valid_payload(void)
{
    uint16_t mask = (1U << 8);
    uint8_t data[16];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, THRESH_I_LCD_MAX);
    data[pos++] = 0xAA;
    data[pos++] = 0x55;

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x01, ack_status());
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

void test_set_thresholds_rejects_payload_shorter_than_mask(void)
{
    uint16_t mask = 0x0001;
    uint8_t data[4];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, THRESH_V24_MIN);
    /* missing max */

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x01, ack_status());
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

/* ===== Unknown mask bits ===== */

void test_set_thresholds_rejects_gap_bits_4_to_7(void)
{
    uint16_t mask = (1U << 4);
    uint8_t data[4];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x01, ack_status());
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

void test_set_thresholds_rejects_len_under_2(void)
{
    uint8_t data[1] = { 0x01 };

    dispatch_set_thresholds(data, sizeof(data));

    TEST_ASSERT_EQUAL_HEX8(0x01, ack_status());
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh_count);
}

/* ===== Success paths ===== */

void test_set_thresholds_single_voltage_updates_fault(void)
{
    uint16_t mask = 0x0001;
    uint16_t mn = THRESH_V24_MIN;
    uint16_t mx = THRESH_V24_MAX;
    uint8_t data[12];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, mn);
    append_u16(data, &pos, mx);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x00, ack_status());
    TEST_ASSERT_EQUAL_UINT8(1, mock_thresh_count);
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh[0].idx);
    TEST_ASSERT_EQUAL_UINT16(mn, mock_thresh[0].min_val);
    TEST_ASSERT_EQUAL_UINT16(mx, mock_thresh[0].max_val);
}

void test_set_thresholds_mixed_voltage_and_current_mask(void)
{
    uint16_t mask = 0x0001 | (1U << 8);
    uint16_t v_mn = 21000U;
    uint16_t v_mx = 25000U;
    uint16_t i_mx = 1800U;
    uint8_t data[16];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, v_mn);
    append_u16(data, &pos, v_mx);
    append_u16(data, &pos, i_mx);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x00, ack_status());
    TEST_ASSERT_EQUAL_UINT8(2, mock_thresh_count);
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh[0].idx);
    TEST_ASSERT_EQUAL_UINT16(v_mn, mock_thresh[0].min_val);
    TEST_ASSERT_EQUAL_UINT16(v_mx, mock_thresh[0].max_val);
    TEST_ASSERT_EQUAL_UINT8(4, mock_thresh[1].idx);
    TEST_ASSERT_EQUAL_UINT16(0, mock_thresh[1].min_val);
    TEST_ASSERT_EQUAL_UINT16(i_mx, mock_thresh[1].max_val);
}

void test_set_thresholds_multiple_voltage_bits_in_bit_order(void)
{
    uint16_t mask = 0x0007; /* bits 0, 1, 2 */
    uint8_t data[32];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, THRESH_V24_MIN);
    append_u16(data, &pos, THRESH_V24_MAX);
    append_u16(data, &pos, THRESH_V12_MIN);
    append_u16(data, &pos, THRESH_V12_MAX);
    append_u16(data, &pos, THRESH_V5_MIN);
    append_u16(data, &pos, THRESH_V5_MAX);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x00, ack_status());
    TEST_ASSERT_EQUAL_UINT8(3, mock_thresh_count);
    TEST_ASSERT_EQUAL_UINT8(0, mock_thresh[0].idx);
    TEST_ASSERT_EQUAL_UINT8(1, mock_thresh[1].idx);
    TEST_ASSERT_EQUAL_UINT8(2, mock_thresh[2].idx);
}

void test_set_thresholds_multiple_current_bits(void)
{
    uint16_t mask = (1U << 8) | (1U << 9);
    uint8_t data[16];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, THRESH_I_LCD_MAX);
    append_u16(data, &pos, THRESH_I_BL_MAX);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x00, ack_status());
    TEST_ASSERT_EQUAL_UINT8(2, mock_thresh_count);
    TEST_ASSERT_EQUAL_UINT8(4, mock_thresh[0].idx);
    TEST_ASSERT_EQUAL_UINT16(THRESH_I_LCD_MAX, mock_thresh[0].max_val);
    TEST_ASSERT_EQUAL_UINT8(5, mock_thresh[1].idx);
    TEST_ASSERT_EQUAL_UINT16(THRESH_I_BL_MAX, mock_thresh[1].max_val);
}

void test_set_thresholds_all_five_current_channels(void)
{
    uint16_t mask = (1U << 8) | (1U << 9) | (1U << 10) | (1U << 11) | (1U << 12);
    uint8_t data[32];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, THRESH_I_LCD_MAX);
    append_u16(data, &pos, THRESH_I_BL_MAX);
    append_u16(data, &pos, THRESH_I_SCALER_MAX);
    append_u16(data, &pos, THRESH_I_AUDIO_LR_MAX);
    append_u16(data, &pos, THRESH_I_AUDIO_LR_MAX);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x00, ack_status());
    TEST_ASSERT_EQUAL_UINT8(5, mock_thresh_count);
    TEST_ASSERT_EQUAL_UINT8(4, mock_thresh[0].idx);
    TEST_ASSERT_EQUAL_UINT16(THRESH_I_LCD_MAX, mock_thresh[0].max_val);
    TEST_ASSERT_EQUAL_UINT8(5, mock_thresh[1].idx);
    TEST_ASSERT_EQUAL_UINT16(THRESH_I_BL_MAX, mock_thresh[1].max_val);
    TEST_ASSERT_EQUAL_UINT8(6, mock_thresh[2].idx);
    TEST_ASSERT_EQUAL_UINT16(THRESH_I_SCALER_MAX, mock_thresh[2].max_val);
    TEST_ASSERT_EQUAL_UINT8(7, mock_thresh[3].idx);
    TEST_ASSERT_EQUAL_UINT16(THRESH_I_AUDIO_LR_MAX, mock_thresh[3].max_val);
    TEST_ASSERT_EQUAL_UINT8(8, mock_thresh[4].idx);
    TEST_ASSERT_EQUAL_UINT16(THRESH_I_AUDIO_LR_MAX, mock_thresh[4].max_val);
}

void test_set_thresholds_current_max_32767_accepted(void)
{
    uint16_t mask = (1U << 8);
    uint16_t mx = 32767U;
    uint8_t data[8];
    uint8_t pos = 0;
    append_u16(data, &pos, mask);
    append_u16(data, &pos, mx);

    dispatch_set_thresholds(data, pos);

    TEST_ASSERT_EQUAL_HEX8(0x00, ack_status());
    TEST_ASSERT_EQUAL_UINT8(1, mock_thresh_count);
    TEST_ASSERT_EQUAL_UINT8(4, mock_thresh[0].idx);
    TEST_ASSERT_EQUAL_UINT16(0, mock_thresh[0].min_val);
    TEST_ASSERT_EQUAL_UINT16(mx, mock_thresh[0].max_val);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_set_thresholds_zero_mask_is_no_op);
    RUN_TEST(test_set_thresholds_rejects_current_mx_zero);
    RUN_TEST(test_set_thresholds_rejects_current_mx_above_int16_max);
    RUN_TEST(test_set_thresholds_rejects_voltage_min_ge_max);
    RUN_TEST(test_set_thresholds_rejects_truncated_voltage_pair);
    RUN_TEST(test_set_thresholds_rejects_extra_bytes_after_valid_payload);
    RUN_TEST(test_set_thresholds_rejects_payload_shorter_than_mask);
    RUN_TEST(test_set_thresholds_rejects_gap_bits_4_to_7);
    RUN_TEST(test_set_thresholds_rejects_len_under_2);
    RUN_TEST(test_set_thresholds_single_voltage_updates_fault);
    RUN_TEST(test_set_thresholds_mixed_voltage_and_current_mask);
    RUN_TEST(test_set_thresholds_multiple_voltage_bits_in_bit_order);
    RUN_TEST(test_set_thresholds_multiple_current_bits);
    RUN_TEST(test_set_thresholds_all_five_current_channels);
    RUN_TEST(test_set_thresholds_current_max_32767_accepted);
    return UNITY_END();
}
