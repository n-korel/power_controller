#include "uart_protocol.h"
#include "config.h"
#include "main.h"
#include "usart.h"
#include "adc_service.h"
#include "input_service.h"
#include "power_manager.h"
#include "fault_manager.h"
#include "flash_cal.h"
#include "bootloader.h"
#include <string.h>

/* ===== CRC-8/ATM table (poly=0x07, init=0x00) ===== */
static const uint8_t crc8_table[256] = {
    0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,0x24,0x23,0x2A,0x2D,
    0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,
    0xE0,0xE7,0xEE,0xE9,0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,
    0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,0xB4,0xB3,0xBA,0xBD,
    0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,
    0xB7,0xB0,0xB9,0xBE,0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,
    0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,0x03,0x04,0x0D,0x0A,
    0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,
    0x89,0x8E,0x87,0x80,0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,
    0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,0xDD,0xDA,0xD3,0xD4,
    0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,
    0x19,0x1E,0x17,0x10,0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,
    0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,0x6A,0x6D,0x64,0x63,
    0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,
    0xAE,0xA9,0xA0,0xA7,0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,
    0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,0xFA,0xFD,0xF4,0xF3,
};

static uint8_t crc8_calc(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++)
        crc = crc8_table[crc ^ data[i]];
    return crc;
}

/* ===== Parser state machine ===== */
typedef enum {
    PS_WAIT_STX,
    PS_READ_CMD,
    PS_READ_LEN,
    PS_READ_DATA,
    PS_READ_CRC,
    PS_WAIT_ETX
} parser_state_t;

static parser_state_t p_state;
static proto_packet_t p_pkt;
static uint8_t  p_data_cnt;
static uint8_t  p_crc_rx;
static uint32_t p_last_byte_ts;
static volatile uint8_t p_ready;

/* ===== RX ring buffer (Rules §1.1, invariant 8: keep ISR minimal) =====
 * ISR only pushes one byte here and re-arms HAL_UART_Receive_IT.
 * Parser state machine + CRC verification run in main-loop context
 * (uart_protocol_process). Size = 128 (power of two → mask on index,
 * avoids div on Cortex-M0 which has no hardware divider). */
#define UART_RX_RING_SIZE  128U
#define UART_RX_RING_MASK  (UART_RX_RING_SIZE - 1U)

static volatile uint8_t  rx_ring[UART_RX_RING_SIZE];
static volatile uint16_t rx_head;        /* written by ISR only */
static volatile uint16_t rx_tail;        /* written by main loop only */
static volatile uint8_t  rx_overflow;    /* set by ISR when ring is full */
static volatile uint8_t  rx_byte;        /* HAL_UART_Receive_IT target */

/* TX buffer: STX + CMD + LEN + DATA(max64) + CRC + ETX = 69 max */
static uint8_t  tx_buf[PROTO_MAX_DATA + 5];
static volatile uint8_t tx_busy_flag;

/* ===== Init ===== */
void uart_protocol_init(void)
{
    p_state = PS_WAIT_STX;
    p_ready = 0;
    tx_busy_flag = 0;
    rx_head = 0;
    rx_tail = 0;
    rx_overflow = 0;
    HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_byte, 1);
}

/* ===== RX byte callback (from ISR) =====
 * Minimal: push one byte into ring and re-arm RX. All parsing/CRC runs
 * in main loop. On ring-full, byte is dropped and overflow flag is set;
 * main loop will flush the ring and reset the parser to re-sync on STX. */
void uart_protocol_rx_byte_cb(void)
{
    uint16_t head = rx_head;
    uint16_t next = (uint16_t)((head + 1U) & UART_RX_RING_MASK);

    if (next == rx_tail) {
        rx_overflow = 1;
    } else {
        rx_ring[head] = rx_byte;
        rx_head = next;
    }

    HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_byte, 1);
}

/* ===== Parser step (main-loop context) ===== */
static void parser_feed(uint8_t b)
{
    uint32_t now = systick_ms;

    /* Interbyte timeout (10 ms) — reset parser if too long since last byte */
    if (p_state != PS_WAIT_STX) {
        if ((now - p_last_byte_ts) > UART_INTERBYTE_TIMEOUT_MS)
            p_state = PS_WAIT_STX;
    }
    p_last_byte_ts = now;

    switch (p_state) {
    case PS_WAIT_STX:
        if (b == PROTO_STX) {
            p_state    = PS_READ_CMD;
            p_data_cnt = 0;
        }
        break;

    case PS_READ_CMD:
        p_pkt.cmd = b;
        p_state   = PS_READ_LEN;
        break;

    case PS_READ_LEN:
        p_pkt.len = b;
        if (p_pkt.len == 0)
            p_state = PS_READ_CRC;
        else if (p_pkt.len > PROTO_MAX_DATA)
            p_state = PS_WAIT_STX;
        else
            p_state = PS_READ_DATA;
        break;

    case PS_READ_DATA:
        p_pkt.data[p_data_cnt++] = b;
        if (p_data_cnt >= p_pkt.len)
            p_state = PS_READ_CRC;
        break;

    case PS_READ_CRC:
        p_crc_rx = b;
        p_state  = PS_WAIT_ETX;
        break;

    case PS_WAIT_ETX:
        if (b == PROTO_ETX) {
            uint8_t crc_buf[PROTO_MAX_DATA + 2];
            crc_buf[0] = p_pkt.cmd;
            crc_buf[1] = p_pkt.len;
            if (p_pkt.len > 0)
                memcpy(&crc_buf[2], p_pkt.data, p_pkt.len);
            uint8_t crc_calc = crc8_calc(crc_buf, (uint8_t)(2 + p_pkt.len));

            if (crc_calc == p_crc_rx)
                p_ready = 1;
        }
        p_state = PS_WAIT_STX;
        break;
    }
}

/* ===== TX ===== */
static void tx_send(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    uint8_t pos = 0;
    tx_buf[pos++] = PROTO_STX;
    tx_buf[pos++] = cmd;
    tx_buf[pos++] = len;
    if (len > 0)
        memcpy(&tx_buf[pos], data, len);
    pos += len;

    /* CRC over [CMD][LEN][DATA] */
    tx_buf[pos] = crc8_calc(&tx_buf[1], (uint8_t)(2 + len));
    pos++;
    tx_buf[pos++] = PROTO_ETX;

    tx_busy_flag = 1;
    HAL_UART_Transmit_IT(&huart1, tx_buf, pos);
}

void uart_send_ack(uint8_t cmd, uint8_t status)
{
    uart_send_response(cmd, &status, 1);
}

void uart_send_response(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    tx_send(cmd, data, len);
}

uint8_t uart_tx_busy(void)
{
    return tx_busy_flag;
}

/* Called from HAL_UART_TxCpltCallback */
void uart_tx_cplt_cb(void)
{
    tx_busy_flag = 0;
}

/* ===== Command dispatch ===== */
static void handle_ping(void)
{
    uint8_t d = PING_RESPONSE;
    tx_send(CMD_PING, &d, 1);
}

static void handle_get_status(void)
{
    uint8_t buf[GET_STATUS_DATA_LEN];
    uint8_t pos = 0;

    /* 4 voltages (uint16 LE) */
    for (uint8_t i = 0; i < 4; i++) {
        uint16_t v = adc_get_voltage_mv(i);
        buf[pos++] = (uint8_t)(v & 0xFF);
        buf[pos++] = (uint8_t)(v >> 8);
    }
    /* 5 currents (uint16 LE — sent as unsigned representation of signed) */
    for (uint8_t i = 0; i < 5; i++) {
        uint16_t c = (uint16_t)adc_get_current_ma(i);
        buf[pos++] = (uint8_t)(c & 0xFF);
        buf[pos++] = (uint8_t)(c >> 8);
    }
    /* 2 temperatures (int16 LE) */
    for (uint8_t i = 0; i < 2; i++) {
        int16_t t = adc_get_temp(i);
        buf[pos++] = (uint8_t)((uint16_t)t & 0xFF);
        buf[pos++] = (uint8_t)((uint16_t)t >> 8);
    }
    /* state (uint8) */
    buf[pos++] = power_get_state();
    /* fault_flags (uint16 LE) */
    uint16_t ff = fault_get_flags();
    buf[pos++] = (uint8_t)(ff & 0xFF);
    buf[pos++] = (uint8_t)(ff >> 8);
    /* inputs (uint8) */
    buf[pos++] = input_get_packed();

    tx_send(CMD_GET_STATUS, buf, GET_STATUS_DATA_LEN);
}

static void handle_power_ctrl(void)
{
    if (p_pkt.len != 4) {
        uart_send_ack(CMD_POWER_CTRL, 1);
        return;
    }
    uint16_t mask  = (uint16_t)p_pkt.data[0] | ((uint16_t)p_pkt.data[1] << 8);
    uint16_t value = (uint16_t)p_pkt.data[2] | ((uint16_t)p_pkt.data[3] << 8);

    uint8_t result = power_ctrl_request(mask, value);
    uart_send_ack(CMD_POWER_CTRL, result);
}

static void handle_set_brightness(void)
{
    if (p_pkt.len != 2) {
        uart_send_ack(CMD_SET_BRIGHTNESS, 1);
        return;
    }
    uint16_t pwm = (uint16_t)p_pkt.data[0] | ((uint16_t)p_pkt.data[1] << 8);
    if (pwm > 1000) {
        uart_send_ack(CMD_SET_BRIGHTNESS, 1);
        return;
    }
    power_set_brightness(pwm);
    uart_send_ack(CMD_SET_BRIGHTNESS, 0);
}

static void handle_reset_fault(void)
{
    fault_clear_flags();
    uart_send_ack(CMD_RESET_FAULT, 0);
}

static void handle_reset_bridge(void)
{
    power_reset_bridge();
    uart_send_ack(CMD_RESET_BRIDGE, 0);
}

static void handle_set_thresholds(void)
{
    if (p_pkt.len < 2) {
        uart_send_ack(CMD_SET_THRESHOLDS, 1);
        return;
    }
    uint16_t mask = (uint16_t)p_pkt.data[0] | ((uint16_t)p_pkt.data[1] << 8);

    /* Reject unknown bits (only 0-3 for voltages, 8-12 for currents) */
    const uint16_t valid_mask = 0x1F0FU;
    if (mask & ~valid_mask) {
        uart_send_ack(CMD_SET_THRESHOLDS, 1);
        return;
    }

    uint8_t idx = 2;
    uint8_t ok  = 1;

    /* Bits 0-3: voltage thresholds (pair min+max, 4 bytes each) */
    for (uint8_t bit = 0; bit < 4; bit++) {
        if (!(mask & (1U << bit))) continue;
        if ((idx + 4) > p_pkt.len) { ok = 0; break; }
        uint16_t mn = (uint16_t)p_pkt.data[idx]   | ((uint16_t)p_pkt.data[idx+1] << 8);
        uint16_t mx = (uint16_t)p_pkt.data[idx+2] | ((uint16_t)p_pkt.data[idx+3] << 8);
        idx += 4;
        if (mn >= mx) { ok = 0; break; }
        fault_set_threshold(bit, mn, mx);
    }

    /* Bits 8-12: current thresholds (single max value, 2 bytes each) */
    if (ok) {
        for (uint8_t bit = 8; bit <= 12; bit++) {
            if (!(mask & (1U << bit))) continue;
            if ((idx + 2) > p_pkt.len) { ok = 0; break; }
            uint16_t mx = (uint16_t)p_pkt.data[idx] | ((uint16_t)p_pkt.data[idx+1] << 8);
            idx += 2;
            if (mx == 0) { ok = 0; break; }
            fault_set_threshold(bit - 8 + 4, 0, mx);
        }
    }

    uart_send_ack(CMD_SET_THRESHOLDS, ok ? 0 : 1);
}

static void handle_bootloader_enter(void)
{
    power_safe_state();
    uart_send_ack(CMD_BOOTLOADER_ENTER, 0);
    bootloader_schedule();
}

static void handle_calibrate_offset(void)
{
    uint8_t result = flash_cal_calibrate();
    uart_send_ack(CMD_CALIBRATE_OFFSET, result);
}

/* ===== Main-loop process ===== */
void uart_protocol_process(void)
{
    /* Drain RX ring buffer and run parser. If ISR signalled overflow,
     * drop pending bytes and reset parser to re-sync on next STX. */
    if (rx_overflow) {
        rx_overflow = 0;
        rx_tail = rx_head;
        p_state = PS_WAIT_STX;
    } else {
        while (rx_tail != rx_head) {
            uint8_t b = rx_ring[rx_tail];
            rx_tail = (uint16_t)((rx_tail + 1U) & UART_RX_RING_MASK);
            parser_feed(b);
        }
    }

    /* Packet timeout (50 ms) — drop half-parsed packet after idle period */
    if (p_state != PS_WAIT_STX) {
        if ((systick_ms - p_last_byte_ts) > UART_PACKET_TIMEOUT_MS)
            p_state = PS_WAIT_STX;
    }

    if (!p_ready) return;
    p_ready = 0;

    switch (p_pkt.cmd) {
    case CMD_PING:             handle_ping();             break;
    case CMD_GET_STATUS:       handle_get_status();       break;
    case CMD_POWER_CTRL:       handle_power_ctrl();       break;
    case CMD_SET_BRIGHTNESS:   handle_set_brightness();   break;
    case CMD_RESET_FAULT:      handle_reset_fault();      break;
    case CMD_RESET_BRIDGE:     handle_reset_bridge();     break;
    case CMD_SET_THRESHOLDS:   handle_set_thresholds();   break;
    case CMD_BOOTLOADER_ENTER: handle_bootloader_enter(); break;
    case CMD_CALIBRATE_OFFSET: handle_calibrate_offset(); break;
    default: {
        uint8_t ec = 0x01;
        tx_send(CMD_NACK, &ec, 1);
        break;
    }
    }
}
