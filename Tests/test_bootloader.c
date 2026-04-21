/*
 * Unit tests: Bootloader entry via SRAM magic — Rules_POWER.md §10.
 *
 *   - bootloader_schedule() just arms a flag, never resets
 *   - bootloader_process() is a no-op unless schedule() was called
 *   - While TX is busy, process() must wait (no reset, magic not planted yet)
 *   - Once TX is idle, process() plants SRAM_MAGIC_VALUE and triggers NVIC_SystemReset
 *
 * bootloader_check() jumps into the ROM bootloader through a function pointer
 * read from an absolute address (0x1FFF0000). That can't run on the host, so
 * it is not exercised here — covered by bring-up on target hardware.
 */
#include "unity.h"
#include "config.h"
#include "stm32f0xx_hal.h"

static uint8_t mock_uart_busy;
uint8_t uart_tx_busy(void) { return mock_uart_busy; }

volatile uint32_t systick_ms;

#include "bootloader.c"

void setUp(void)
{
    hal_stub_reset();
    mock_uart_busy = 0;
    boot_magic   = 0;
    boot_pending = 0;
}

void tearDown(void) {}

/* ===== Schedule semantics ===== */

void test_schedule_sets_pending_without_reset(void)
{
    bootloader_schedule();

    TEST_ASSERT_EQUAL_UINT8(1, boot_pending);
    TEST_ASSERT_EQUAL_UINT32(0, hal_stub_nvic_reset_count);
    TEST_ASSERT_NOT_EQUAL(SRAM_MAGIC_VALUE, boot_magic);
}

/* ===== Process guards ===== */

void test_process_noop_when_not_scheduled(void)
{
    mock_uart_busy = 0;

    bootloader_process();
    bootloader_process();

    TEST_ASSERT_EQUAL_UINT32(0, hal_stub_nvic_reset_count);
    TEST_ASSERT_NOT_EQUAL(SRAM_MAGIC_VALUE, boot_magic);
}

void test_process_waits_while_tx_busy(void)
{
    bootloader_schedule();
    mock_uart_busy = 1;

    /* Multiple calls while TX busy — must not reset, must not plant magic,
       and the pending flag must persist for the next attempt. */
    bootloader_process();
    bootloader_process();
    bootloader_process();

    TEST_ASSERT_EQUAL_UINT32(0, hal_stub_nvic_reset_count);
    TEST_ASSERT_NOT_EQUAL(SRAM_MAGIC_VALUE, boot_magic);
    TEST_ASSERT_EQUAL_UINT8(1, boot_pending);
}

/* ===== Trigger semantics ===== */

void test_process_plants_magic_and_resets_when_tx_idle(void)
{
    bootloader_schedule();
    mock_uart_busy = 0;

    bootloader_process();

    TEST_ASSERT_EQUAL_UINT32(SRAM_MAGIC_VALUE, boot_magic);
    TEST_ASSERT_EQUAL_UINT32(1, hal_stub_nvic_reset_count);
}

void test_repeated_schedule_and_process_are_idempotent_until_reset_happens(void)
{
    bootloader_schedule();
    bootloader_schedule();
    mock_uart_busy = 1;

    bootloader_process();
    bootloader_process();
    TEST_ASSERT_EQUAL_UINT32(0, hal_stub_nvic_reset_count);
    TEST_ASSERT_NOT_EQUAL(SRAM_MAGIC_VALUE, boot_magic);
    TEST_ASSERT_EQUAL_UINT8(1, boot_pending);

    mock_uart_busy = 0;
    bootloader_process();
    TEST_ASSERT_EQUAL_UINT32(1, hal_stub_nvic_reset_count);

    /* Extra process() calls must not "double reset". */
    bootloader_process();
    bootloader_process();
    TEST_ASSERT_EQUAL_UINT32(1, hal_stub_nvic_reset_count);
}

/* ===== Runner ===== */
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_schedule_sets_pending_without_reset);
    RUN_TEST(test_process_noop_when_not_scheduled);
    RUN_TEST(test_process_waits_while_tx_busy);
    RUN_TEST(test_process_plants_magic_and_resets_when_tx_idle);
    RUN_TEST(test_repeated_schedule_and_process_are_idempotent_until_reset_happens);
    return UNITY_END();
}
