/*
 * Unit tests: CMD_READ_FLASH handler validation (handle_read_flash).
 */
#define _DEFAULT_SOURCE
#include "unity.h"
#include "config.h"
#include "stm32f0xx_hal.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#define TEST_FLASH_MAP_SIZE 0x10000U
#define TEST_FLASH_MAP_BASE 0x08000000U

static uint8_t *flash_mapped;
static uint8_t  flash_buf[TEST_FLASH_MAP_SIZE] __attribute__((aligned(4)));

static uint8_t *test_flash_base(void)
{
    if (flash_mapped != NULL)
        return flash_mapped;

    flash_mapped = (uint8_t *)mmap(
        (void *)(uintptr_t)TEST_FLASH_MAP_BASE,
        TEST_FLASH_MAP_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1,
        0);
    if (flash_mapped == MAP_FAILED) {
        flash_mapped = flash_buf;
    }

    return flash_mapped;
}

static uint16_t mock_voltage_mv[4];
static int16_t  mock_current_ma[5];
static uint8_t  mock_power_state;
static uint16_t mock_fault_flags;
static uint8_t  mock_input_packed;

uint16_t adc_get_voltage_mv(uint8_t idx) { return (idx < 4) ? mock_voltage_mv[idx] : 0; }
int16_t  adc_get_current_ma(uint8_t idx) { return (idx < 5) ? mock_current_ma[idx] : 0; }
uint8_t  power_get_state(void)           { return mock_power_state; }
uint16_t fault_get_flags(void)           { return mock_fault_flags; }
uint8_t  input_get_packed(void)          { return mock_input_packed; }
uint8_t  power_ctrl_request(uint16_t m, uint16_t v) { (void)m; (void)v; return 0; }
void     power_set_brightness(uint16_t p) { (void)p; }
void     fault_clear_flags(void) {}
void     fault_set_flag(uint16_t flag) { (void)flag; }
uint8_t  power_reset_bridge(void) { return 0; }
void     fault_set_threshold(uint8_t i, uint16_t mn, uint16_t mx) { (void)i; (void)mn; (void)mx; }
void     power_safe_state(void) {}
void     bootloader_schedule(void) {}
uint8_t  flash_cal_calibrate(void) { return 0; }
uint32_t boot_meta_image_crc(void) { return 0; }

volatile uint32_t systick_ms;

#include "uart_protocol.c"

typedef struct {
    const char *name;
    uint32_t    addr;
    uint8_t     len;
    uint8_t     req_len;
    uint8_t     expect_status;
    uint8_t     check_data;
} read_flash_case_t;

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

static void dispatch_read_flash(const uint8_t *data, uint8_t len)
{
    uint8_t pkt[PROTO_MAX_DATA + 8];
    uint16_t n = build_packet(pkt, CMD_READ_FLASH, data, len);
    p_state = PS_WAIT_STX;
    pkt_q_head = 0;
    pkt_q_tail = 0;
    pkt_q_count = 0;
    tx_busy_flag = 0;
    memset(tx_buf, 0, sizeof(tx_buf));
    for (uint16_t i = 0; i < n; i++)
        parser_feed(pkt[i]);
    uart_protocol_process();
}

static void build_req(uint8_t *out, uint32_t addr, uint8_t len)
{
    out[0] = (uint8_t)(addr & 0xFF);
    out[1] = (uint8_t)((addr >> 8) & 0xFF);
    out[2] = (uint8_t)((addr >> 16) & 0xFF);
    out[3] = (uint8_t)((addr >> 24) & 0xFF);
    out[4] = len;
}

static uint8_t resp_status(void)
{
    return tx_buf[3];
}

static const uint8_t *resp_data(void)
{
    return &tx_buf[4];
}

void setUp(void)
{
    uint8_t *base = test_flash_base();

    hal_stub_reset();
    uart_protocol_init();
    p_state = PS_WAIT_STX;
    pkt_q_head = 0;
    pkt_q_tail = 0;
    pkt_q_count = 0;
    tx_busy_flag = 0;
    memset(tx_buf, 0, sizeof(tx_buf));
    memset(base, 0, TEST_FLASH_MAP_SIZE);
    for (size_t i = 0; i < TEST_FLASH_MAP_SIZE; i++)
        base[i] = (uint8_t)(0xA0U + i);
}

void tearDown(void) {}

static void run_read_flash_case(const read_flash_case_t *tc)
{
    uint8_t req[6];
    build_req(req, tc->addr, tc->len);

    if (tc->req_len != 0U)
        dispatch_read_flash(req, tc->req_len);
    else
        dispatch_read_flash(req, 5);

    TEST_ASSERT_EQUAL_HEX8_MESSAGE(CMD_READ_FLASH, tx_buf[1], tc->name);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(tc->expect_status, resp_status(), tc->name);

    if (tc->expect_status == 0U) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)(1U + tc->len), tx_buf[2], tc->name);
        if (tc->check_data) {
            const uint8_t *base = test_flash_base();
            const uint8_t *expected = &base[tc->addr - TEST_FLASH_MAP_BASE];

            TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(
                expected,
                resp_data(),
                tc->len,
                tc->name);
        }
    } else {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1U, tx_buf[2], tc->name);
    }
}

void test_read_flash_table(void)
{
    const read_flash_case_t cases[] = {
        {
            "valid start of flash",
            TEST_FLASH_MAP_BASE,
            16,
            5,
            0,
            1,
        },
        {
            "len zero",
            TEST_FLASH_MAP_BASE,
            0,
            5,
            1,
            0,
        },
        {
            "len above max",
            TEST_FLASH_MAP_BASE,
            64,
            5,
            1,
            0,
        },
        {
            "addr below valid start",
            TEST_FLASH_MAP_BASE - 1U,
            1,
            5,
            1,
            0,
        },
        {
            "addr+len past valid end",
            (uint32_t)FLASH_CAL_VALID_END - 1U,
            2,
            5,
            1,
            0,
        },
        {
            "addr overflow near 0xFFFFFFFF",
            0xFFFFFFFEU,
            2,
            5,
            1,
            0,
        },
        {
            "boundary addr+len == valid end",
            (uint32_t)FLASH_CAL_VALID_END - 1U,
            1,
            5,
            0,
            1,
        },
        {
            "short request",
            TEST_FLASH_MAP_BASE,
            4,
            4,
            1,
            0,
        },
        {
            "long request",
            TEST_FLASH_MAP_BASE,
            4,
            6,
            1,
            0,
        },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run_read_flash_case(&cases[i]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_read_flash_table);
    return UNITY_END();
}
