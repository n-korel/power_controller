/*
 * Unit tests: boot_meta — OTA pending-confirm / safe-hold.
 *
 * Redirects FLASH_BOOT_META_ADDR to a RAM buffer. Confirm does not depend on
 * image CRC / .fw_crc — only boot_meta_t contents and FW_GIT_HASH_STR.
 */
#include "unity.h"
#include <string.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t boot_meta_buf[128] __attribute__((aligned(4)));
#define FLASH_BOOT_META_ADDR ((uintptr_t)boot_meta_buf)
#define FLASH_BOOT_META_VALID_START ((uintptr_t)boot_meta_buf)
#define FLASH_BOOT_META_VALID_END   ((uintptr_t)boot_meta_buf + sizeof(boot_meta_buf))
#define FLASH_BOOT_META_ERASE_SIZE  128U

#include "stm32f0xx_hal.h"
#include "config.h"
#include "boot_meta.h"
#include "version_gen.h"

volatile uint32_t systick_ms;

static uint32_t mock_bootloader_schedule_count;
static uint32_t mock_nvic_reset_at_init;

void bootloader_schedule(void)
{
    mock_bootloader_schedule_count++;
}

#include "flash_util.c"
#include "boot_meta.c"

static void seed_meta(uint8_t pending, uint8_t attempts, uint8_t max_att,
                      const uint8_t hash[8])
{
    boot_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.magic           = FLASH_BOOT_META_MAGIC;
    meta.version         = FLASH_BOOT_META_VERSION;
    meta.pending_confirm = pending;
    meta.boot_attempts   = attempts;
    meta.max_attempts    = max_att;
    if (hash != NULL)
        memcpy(meta.tracked_hash, hash, 8);
    meta.crc32 = sw_crc32((const uint8_t *)&meta, offsetof(boot_meta_t, crc32));
    memcpy(boot_meta_buf, &meta, sizeof(meta));
}

static void read_meta(boot_meta_t *out)
{
    memcpy(out, boot_meta_buf, sizeof(*out));
}

static void fw_hash(uint8_t out[8])
{
    const char *h = FW_GIT_HASH_STR;
    for (uint8_t i = 0; i < 8U; i++)
        out[i] = (uint8_t)h[i];
}

void setUp(void)
{
    memset(boot_meta_buf, 0xFF, sizeof(boot_meta_buf));
    systick_ms = 0;
    mock_bootloader_schedule_count = 0;
    mock_nvic_reset_at_init = 0;
    hal_stub_reset();
}

void tearDown(void) {}

void test_structure_layout(void)
{
    TEST_ASSERT_EQUAL_UINT(20, offsetof(boot_meta_t, crc32));
    TEST_ASSERT_EQUAL_UINT(24, sizeof(boot_meta_t));
    TEST_ASSERT_EQUAL_UINT(0, offsetof(boot_meta_t, crc32) % 4);
}

void test_invalid_magic_defaults_confirmed(void)
{
    boot_meta_init();

    TEST_ASSERT_EQUAL_UINT8(0, boot_meta_safe_hold());
    TEST_ASSERT_EQUAL_UINT32(0, mock_bootloader_schedule_count);
    TEST_ASSERT_EQUAL_UINT32(0, hal_stub_nvic_reset_count);
}

void test_bad_crc_defaults_confirmed(void)
{
    boot_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.magic           = FLASH_BOOT_META_MAGIC;
    meta.version         = FLASH_BOOT_META_VERSION;
    meta.pending_confirm = 1;
    meta.boot_attempts   = 2;
    meta.max_attempts    = 3;
    meta.crc32           = 0xDEADBEEFU;
    memcpy(boot_meta_buf, &meta, sizeof(meta));

    boot_meta_init();

    TEST_ASSERT_EQUAL_UINT8(0, boot_meta_safe_hold());
}

void test_first_boot_sets_hash_and_attempts_one(void)
{
    uint8_t zeros[8] = {0};
    seed_meta(1, 0, BOOT_META_MAX_ATTEMPTS, zeros);

    boot_meta_init();

    boot_meta_t meta;
    read_meta(&meta);
    uint8_t expect[8];
    fw_hash(expect);

    TEST_ASSERT_EQUAL_UINT8(1, meta.pending_confirm);
    TEST_ASSERT_EQUAL_UINT8(1, meta.boot_attempts);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, meta.tracked_hash, 8);
    TEST_ASSERT_EQUAL_UINT8(0, boot_meta_safe_hold());
}

void test_same_hash_increments_attempts(void)
{
    uint8_t hash[8];
    fw_hash(hash);
    seed_meta(1, 1, BOOT_META_MAX_ATTEMPTS, hash);

    boot_meta_init();

    boot_meta_t meta;
    read_meta(&meta);
    TEST_ASSERT_EQUAL_UINT8(2, meta.boot_attempts);
    TEST_ASSERT_EQUAL_UINT8(0, boot_meta_safe_hold());
}

void test_attempts_reach_max_sets_safe_hold(void)
{
    uint8_t hash[8];
    fw_hash(hash);
    seed_meta(1, 2, BOOT_META_MAX_ATTEMPTS, hash);

    boot_meta_init();

    boot_meta_t meta;
    read_meta(&meta);
    TEST_ASSERT_EQUAL_UINT8(3, meta.boot_attempts);
    TEST_ASSERT_EQUAL_UINT8(1, boot_meta_safe_hold());
    TEST_ASSERT_EQUAL_UINT32(0, mock_bootloader_schedule_count);
    TEST_ASSERT_EQUAL_UINT32(0, hal_stub_nvic_reset_count);
}

void test_foreign_hash_resets_attempts_not_safe_hold(void)
{
    uint8_t foreign[8] = {'d', 'e', 'a', 'd', 'b', 'e', 'e', 'f'};
    seed_meta(1, 2, BOOT_META_MAX_ATTEMPTS, foreign);

    boot_meta_init();

    boot_meta_t meta;
    read_meta(&meta);
    uint8_t expect[8];
    fw_hash(expect);

    TEST_ASSERT_EQUAL_UINT8(1, meta.boot_attempts);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, meta.tracked_hash, 8);
    TEST_ASSERT_EQUAL_UINT8(0, boot_meta_safe_hold());

    /* Second init = reboot of the same image → attempts become 2. */
    boot_meta_init();
    read_meta(&meta);
    TEST_ASSERT_EQUAL_UINT8(2, meta.boot_attempts);
    TEST_ASSERT_EQUAL_UINT8(0, boot_meta_safe_hold());
}

void test_confirm_clears_pending_and_attempts(void)
{
    uint8_t hash[8];
    fw_hash(hash);
    seed_meta(1, 2, BOOT_META_MAX_ATTEMPTS, hash);
    boot_meta_init();
    TEST_ASSERT_EQUAL_UINT8(1, boot_meta_safe_hold());

    boot_meta_confirm();

    boot_meta_t meta;
    read_meta(&meta);
    TEST_ASSERT_EQUAL_UINT8(0, meta.pending_confirm);
    TEST_ASSERT_EQUAL_UINT8(0, meta.boot_attempts);
    TEST_ASSERT_EQUAL_UINT8(0, boot_meta_safe_hold());
}

void test_auto_confirm_writes_once_even_with_faults(void)
{
    uint8_t zeros[8] = {0};
    seed_meta(1, 0, BOOT_META_MAX_ATTEMPTS, zeros);
    boot_meta_init();
    TEST_ASSERT_EQUAL_UINT8(0, boot_meta_safe_hold());

    uint32_t programs_before = 0;
    for (uint32_t i = 0; i < hal_call_log_count; i++) {
        if (hal_call_log[i].id == HAL_CALL_FLASH_PROGRAM)
            programs_before++;
    }

    systick_ms = BOOT_META_CONFIRM_STABLE_MS;
    boot_meta_process();
    boot_meta_process();
    boot_meta_process();

    uint32_t programs_after = 0;
    for (uint32_t i = 0; i < hal_call_log_count; i++) {
        if (hal_call_log[i].id == HAL_CALL_FLASH_PROGRAM)
            programs_after++;
    }

    /* One confirm write = ceil(24/4) = 6 WORD programs. */
    TEST_ASSERT_EQUAL_UINT32(programs_before + 6U, programs_after);

    boot_meta_t meta;
    read_meta(&meta);
    TEST_ASSERT_EQUAL_UINT8(0, meta.pending_confirm);
}

void test_arm_pending_clears_hash_and_attempts(void)
{
    boot_meta_arm_pending();

    boot_meta_t meta;
    read_meta(&meta);
    TEST_ASSERT_EQUAL_UINT8(1, meta.pending_confirm);
    TEST_ASSERT_EQUAL_UINT8(0, meta.boot_attempts);
    TEST_ASSERT_EQUAL_UINT8(BOOT_META_MAX_ATTEMPTS, meta.max_attempts);
    for (uint8_t i = 0; i < 8U; i++)
        TEST_ASSERT_EQUAL_UINT8(0, meta.tracked_hash[i]);
}

void test_safe_hold_never_schedules_bootloader_or_reset(void)
{
    uint8_t hash[8];
    fw_hash(hash);
    seed_meta(1, 2, BOOT_META_MAX_ATTEMPTS, hash);

    mock_nvic_reset_at_init = hal_stub_nvic_reset_count;
    boot_meta_init();
    boot_meta_process();
    boot_meta_confirm();

    TEST_ASSERT_EQUAL_UINT8(0, boot_meta_safe_hold());
    TEST_ASSERT_EQUAL_UINT32(0, mock_bootloader_schedule_count);
    TEST_ASSERT_EQUAL_UINT32(mock_nvic_reset_at_init, hal_stub_nvic_reset_count);
}

void test_pending_zero_is_noop(void)
{
    uint8_t hash[8];
    fw_hash(hash);
    seed_meta(0, 0, BOOT_META_MAX_ATTEMPTS, hash);

    boot_meta_init();
    systick_ms = BOOT_META_CONFIRM_STABLE_MS + 1U;
    boot_meta_process();

    TEST_ASSERT_EQUAL_UINT8(0, boot_meta_safe_hold());
    boot_meta_t meta;
    read_meta(&meta);
    TEST_ASSERT_EQUAL_UINT8(0, meta.pending_confirm);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_structure_layout);
    RUN_TEST(test_invalid_magic_defaults_confirmed);
    RUN_TEST(test_bad_crc_defaults_confirmed);
    RUN_TEST(test_first_boot_sets_hash_and_attempts_one);
    RUN_TEST(test_same_hash_increments_attempts);
    RUN_TEST(test_attempts_reach_max_sets_safe_hold);
    RUN_TEST(test_foreign_hash_resets_attempts_not_safe_hold);
    RUN_TEST(test_confirm_clears_pending_and_attempts);
    RUN_TEST(test_auto_confirm_writes_once_even_with_faults);
    RUN_TEST(test_arm_pending_clears_hash_and_attempts);
    RUN_TEST(test_safe_hold_never_schedules_bootloader_or_reset);
    RUN_TEST(test_pending_zero_is_noop);
    return UNITY_END();
}
