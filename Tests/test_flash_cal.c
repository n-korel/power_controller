/*
 * Unit tests: flash_cal — current-offset persistence (Rules_POWER.md §11).
 *
 * Redirects FLASH_CAL_ADDR to a RAM buffer so flash_cal_load/flash_cal_calibrate
 * run on the host. HAL_FLASH_Program + HAL_FLASHEx_Erase stubs emulate write/erase.
 */
#include "unity.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* RAM-backed flash region. Must exist BEFORE config.h / flash_cal.c are pulled in. */
static uint8_t flash_cal_buf[128] __attribute__((aligned(4)));
#define FLASH_CAL_ADDR ((uintptr_t)flash_cal_buf)

#include "stm32f0xx_hal.h"
#include "main.h"
#include "config.h"
#include "flash_cal.h"

/* ===== adc_service mocks ===== */
static uint16_t mock_raw_avg[14];
static uint16_t applied_offset[5];
static uint8_t  offset_called[5];

uint16_t adc_get_raw_avg(uint8_t idx) { return (idx < 14) ? mock_raw_avg[idx] : 0; }

void adc_set_current_offset(uint8_t idx, uint16_t off)
{
    if (idx < 5) {
        applied_offset[idx] = off;
        offset_called[idx]  = 1;
    }
}

volatile uint32_t systick_ms;

#include "flash_cal.c"

/* Build a valid flash_cal_t image in the buffer with a given offset array */
static void seed_valid_cal(const uint16_t offs[5])
{
    flash_cal_t cal;
    memset(&cal, 0, sizeof(cal));
    cal.magic   = FLASH_CAL_MAGIC;
    cal.version = FLASH_CAL_VERSION;
    for (uint8_t i = 0; i < 5; i++)
        cal.offset_raw[i] = offs[i];
    uint32_t payload_len = offsetof(flash_cal_t, crc32);
    cal.crc32 = sw_crc32((const uint8_t *)&cal, payload_len);
    memcpy(flash_cal_buf, &cal, sizeof(cal));
}

void setUp(void)
{
    memset(flash_cal_buf, 0xFF, sizeof(flash_cal_buf));
    memset(mock_raw_avg, 0, sizeof(mock_raw_avg));
    memset(applied_offset, 0, sizeof(applied_offset));
    memset(offset_called, 0, sizeof(offset_called));
}

void tearDown(void) {}

/* ===== Structure layout (Cortex-M0 alignment, Rules §11) ===== */

void test_calibrate_structure_layout(void)
{
    TEST_ASSERT_EQUAL_UINT(20, offsetof(flash_cal_t, crc32));
    TEST_ASSERT_EQUAL_UINT(24, sizeof(flash_cal_t));
    /* 4-byte alignment required for crc32 on Cortex-M0 */
    TEST_ASSERT_EQUAL_UINT(0, offsetof(flash_cal_t, crc32) % 4);
}

/* ===== Load paths ===== */

void test_load_invalid_magic_keeps_default_offsets(void)
{
    /* Buffer is 0xFF → magic mismatches FLASH_CAL_MAGIC */
    flash_cal_load();

    for (uint8_t i = 0; i < 5; i++)
        TEST_ASSERT_EQUAL_UINT8(0, offset_called[i]);
}

void test_load_valid_magic_bad_crc_keeps_default_offsets(void)
{
    flash_cal_t cal;
    memset(&cal, 0, sizeof(cal));
    cal.magic   = FLASH_CAL_MAGIC;
    cal.version = FLASH_CAL_VERSION;
    cal.offset_raw[0] = 1234;
    cal.crc32 = 0xDEADBEEFU; /* deliberately wrong */
    memcpy(flash_cal_buf, &cal, sizeof(cal));

    flash_cal_load();

    for (uint8_t i = 0; i < 5; i++)
        TEST_ASSERT_EQUAL_UINT8(0, offset_called[i]);
}

void test_load_valid_crc_applies_offsets_to_adc_service(void)
{
    const uint16_t offs[5] = { 1650, 1651, 1652, 1653, 1654 };
    seed_valid_cal(offs);

    flash_cal_load();

    for (uint8_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT8(1, offset_called[i]);
        TEST_ASSERT_EQUAL_UINT16(offs[i], applied_offset[i]);
    }
}

void test_load_wrong_version_keeps_default_offsets(void)
{
    flash_cal_t cal;
    memset(&cal, 0, sizeof(cal));
    cal.magic   = FLASH_CAL_MAGIC;
    cal.version = FLASH_CAL_VERSION + 1U;
    uint32_t payload_len = offsetof(flash_cal_t, crc32);
    cal.crc32 = sw_crc32((const uint8_t *)&cal, payload_len);
    memcpy(flash_cal_buf, &cal, sizeof(cal));

    flash_cal_load();

    for (uint8_t i = 0; i < 5; i++)
        TEST_ASSERT_EQUAL_UINT8(0, offset_called[i]);
}

/* ===== Calibrate → Load round-trip ===== */

void test_calibrate_roundtrip(void)
{
    const uint16_t raws[5] = { 1600, 1640, 1680, 1700, 1720 };
    mock_raw_avg[ADC_IDX_LCD_CURRENT]     = raws[0];
    mock_raw_avg[ADC_IDX_BL_CURRENT]      = raws[1];
    mock_raw_avg[ADC_IDX_SCALER_CURRENT]  = raws[2];
    mock_raw_avg[ADC_IDX_AUDIO_L_CURRENT] = raws[3];
    mock_raw_avg[ADC_IDX_AUDIO_R_CURRENT] = raws[4];

    uint8_t r = flash_cal_calibrate();
    TEST_ASSERT_EQUAL_UINT8(0, r);

    /* calibrate() already applies offsets once, so reset spies and reload from buffer */
    memset(applied_offset, 0, sizeof(applied_offset));
    memset(offset_called,  0, sizeof(offset_called));

    flash_cal_load();

    for (uint8_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_UINT8(1, offset_called[i]);
        TEST_ASSERT_EQUAL_UINT16(raws[i], applied_offset[i]);
    }
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_calibrate_structure_layout);
    RUN_TEST(test_load_invalid_magic_keeps_default_offsets);
    RUN_TEST(test_load_valid_magic_bad_crc_keeps_default_offsets);
    RUN_TEST(test_load_valid_crc_applies_offsets_to_adc_service);
    RUN_TEST(test_load_wrong_version_keeps_default_offsets);
    RUN_TEST(test_calibrate_roundtrip);
    return UNITY_END();
}
