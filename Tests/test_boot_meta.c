/*
 * Unit tests: boot_meta — pending-confirm / auto-recovery metadata.
 *
 * RAM-backed BOOT_META_ADDR and firmware image (BOOT_META_TEST_IMAGE*) so
 * boot_meta_on_startup / boot_meta_confirm run on the host.
 */
#include "unity.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t boot_meta_buf[128] __attribute__((aligned(4)));
#define BOOT_META_ADDR ((uintptr_t)boot_meta_buf)

#define BOOT_META_TEST_IMAGE mock_fw_image
static uint8_t mock_fw_image[64] __attribute__((aligned(4)));
static const uint8_t *mock_fw_image_end;
static uint32_t *mock_crc_slot;
#define BOOT_META_TEST_IMAGE_END mock_fw_image_end
#define BOOT_META_TEST_CRC_SLOT mock_crc_slot

#include "stm32f0xx_hal.h"
#include "config.h"
#include "boot_meta.h"

static uint8_t mock_bootloader_scheduled;

void bootloader_schedule(void) { mock_bootloader_scheduled = 1; }

volatile uint32_t systick_ms;

#include "boot_meta.c"

static uint32_t sign_test_image(void)
{
    const uint8_t *start = BOOT_META_IMG_START;
    const uint8_t *end   = BOOT_META_IMG_END;
    const uint8_t *crc_lo = BOOT_META_CRC_SLOT;
    const uint8_t *crc_hi = crc_lo + sizeof(uint32_t);

    uint32_t crc = 0xFFFFFFFF;
    for (const uint8_t *p = start; p < end; p++) {
        uint8_t byte = (p >= crc_lo && p < crc_hi) ? 0U : *p;
        crc ^= byte;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320U;
            else
                crc >>= 1;
        }
    }
    crc = ~crc;
    *mock_crc_slot = crc;
    return crc;
}

static void seed_meta(const boot_meta_t *fields)
{
    boot_meta_t m;
    memset(&m, 0, sizeof(m));
    m.magic         = BOOT_META_MAGIC;
    m.version       = BOOT_META_VERSION;
    m.confirmed     = fields->confirmed;
    m.boot_attempts = fields->boot_attempts;
    m.firmware_crc  = fields->firmware_crc;
    uint32_t payload_len = offsetof(boot_meta_t, crc32);
    m.crc32 = sw_crc32((const uint8_t *)&m, payload_len);
    memcpy(boot_meta_buf, &m, sizeof(m));
}

static boot_meta_t read_meta_buf(void)
{
    boot_meta_t out;
    memcpy(&out, boot_meta_buf, sizeof(out));
    return out;
}

void setUp(void)
{
    mock_bootloader_scheduled = 0;
    memset(boot_meta_buf, 0xFF, sizeof(boot_meta_buf));
    memset(mock_fw_image, 0xA5, sizeof(mock_fw_image));
    mock_fw_image_end = mock_fw_image + sizeof(mock_fw_image);
    mock_crc_slot =
        (uint32_t *)(mock_fw_image + sizeof(mock_fw_image) - sizeof(uint32_t));
    *mock_crc_slot = 0xFFFFFFFFU;
    hal_stub_reset();
}

void tearDown(void) {}

typedef struct {
    uint8_t confirmed;
    uint8_t boot_attempts;
    uint32_t firmware_crc;
    uint8_t expect_schedule;
    uint8_t expect_attempts_after;
} startup_case_t;

void test_boot_meta_structure_layout(void)
{
    TEST_ASSERT_EQUAL_UINT(12, offsetof(boot_meta_t, crc32));
    TEST_ASSERT_EQUAL_UINT(16, sizeof(boot_meta_t));
    TEST_ASSERT_EQUAL_UINT(0, offsetof(boot_meta_t, crc32) % 4);
}

void test_on_startup_no_meta_is_legacy(void)
{
    boot_meta_on_startup();
    TEST_ASSERT_EQUAL_UINT8(0, mock_bootloader_scheduled);
}

void test_on_startup_confirmed_matching_crc_is_noop(void)
{
    uint32_t crc = sign_test_image();
    boot_meta_t seed = {
        .confirmed     = 1U,
        .boot_attempts = 1U,
        .firmware_crc  = crc,
    };
    seed_meta(&seed);

    boot_meta_on_startup();

    TEST_ASSERT_EQUAL_UINT8(0, mock_bootloader_scheduled);
    TEST_ASSERT_EQUAL_UINT8(1U, read_meta_buf().boot_attempts);
}

void test_on_startup_zero_attempts_schedules_bootloader(void)
{
    boot_meta_t seed = {
        .confirmed     = 0U,
        .boot_attempts = 0U,
        .firmware_crc  = 0U,
    };
    seed_meta(&seed);

    boot_meta_on_startup();

    TEST_ASSERT_EQUAL_UINT8(1, mock_bootloader_scheduled);
}

void test_on_startup_unconfirmed_decrements_attempts(void)
{
    boot_meta_t seed = {
        .confirmed     = 0U,
        .boot_attempts = 3U,
        .firmware_crc  = 0U,
    };
    seed_meta(&seed);

    boot_meta_on_startup();

    TEST_ASSERT_EQUAL_UINT8(0, mock_bootloader_scheduled);
    TEST_ASSERT_EQUAL_UINT8(2U, read_meta_buf().boot_attempts);
}

void test_on_startup_stale_confirmed_crc_rearms_pending(void)
{
    uint32_t crc = sign_test_image();
    boot_meta_t seed = {
        .confirmed     = 1U,
        .boot_attempts = 1U,
        .firmware_crc  = crc ^ 1U,
    };
    seed_meta(&seed);

    boot_meta_on_startup();

    boot_meta_t m = read_meta_buf();
    TEST_ASSERT_EQUAL_UINT8(0, m.confirmed);
    TEST_ASSERT_EQUAL_UINT8(3U, m.boot_attempts);
    TEST_ASSERT_EQUAL_UINT8(0, mock_bootloader_scheduled);
}

void test_confirm_rejects_unsigned_image(void)
{
    boot_meta_on_startup();

    uint8_t r = boot_meta_confirm();

    TEST_ASSERT_EQUAL_UINT8(1, r);
    TEST_ASSERT_EQUAL_UINT8(0xFF, boot_meta_buf[0]);
}

void test_confirm_writes_confirmed_record(void)
{
    uint32_t crc = sign_test_image();

    uint8_t r = boot_meta_confirm();

    TEST_ASSERT_EQUAL_UINT8(0, r);
    boot_meta_t m = read_meta_buf();
    TEST_ASSERT_EQUAL_HEX32(BOOT_META_MAGIC, m.magic);
    TEST_ASSERT_EQUAL_UINT16(BOOT_META_VERSION, m.version);
    TEST_ASSERT_EQUAL_UINT8(1U, m.confirmed);
    TEST_ASSERT_EQUAL_UINT8(3U, m.boot_attempts);
    TEST_ASSERT_EQUAL_HEX32(crc, m.firmware_crc);
    TEST_ASSERT_EQUAL_HEX32(sw_crc32((const uint8_t *)&m, offsetof(boot_meta_t, crc32)),
                            m.crc32);
}

void test_startup_cases_table(void)
{
    static const startup_case_t cases[] = {
        { 0U, 2U, 0U, 0U, 1U },
        { 0U, 1U, 0U, 0U, 0U },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        setUp();
        boot_meta_t seed = {
            .confirmed     = cases[i].confirmed,
            .boot_attempts = cases[i].boot_attempts,
            .firmware_crc  = cases[i].firmware_crc,
        };
        seed_meta(&seed);

        boot_meta_on_startup();

        TEST_ASSERT_EQUAL_UINT8(cases[i].expect_schedule, mock_bootloader_scheduled);
        TEST_ASSERT_EQUAL_UINT8(cases[i].expect_attempts_after, read_meta_buf().boot_attempts);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_meta_structure_layout);
    RUN_TEST(test_on_startup_no_meta_is_legacy);
    RUN_TEST(test_on_startup_confirmed_matching_crc_is_noop);
    RUN_TEST(test_on_startup_zero_attempts_schedules_bootloader);
    RUN_TEST(test_on_startup_unconfirmed_decrements_attempts);
    RUN_TEST(test_on_startup_stale_confirmed_crc_rearms_pending);
    RUN_TEST(test_confirm_rejects_unsigned_image);
    RUN_TEST(test_confirm_writes_confirmed_record);
    RUN_TEST(test_startup_cases_table);
    return UNITY_END();
}
