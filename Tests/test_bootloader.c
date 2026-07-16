/*
 * Unit tests: Bootloader entry via SRAM magic — Rules_POWER.md §10.
 *
 *   - bootloader_schedule() just arms a flag, never resets
 *   - bootloader_process() is a no-op unless schedule() was called
 *   - While TX is busy, process() must wait (no reset, magic not planted yet)
 *   - Once TX is idle, process() plants SRAM_MAGIC_VALUE and triggers NVIC_SystemReset
 *
 *   - bootloader_enter_request() starts the graceful shutdown (via
 *     power_graceful_shutdown_begin()) and defers power_safe_state()/ACK/
 *     bootloader_schedule() until power_is_idle() (bootloader_process()).
 *     This spreads the SCALER+LCD+BACKLIGHT+AUDIO load-step over time
 *     instead of cutting it all in one shot.
 *
 * bootloader_check() jumps into the ROM bootloader through a function pointer
 * read from an absolute address (ROM_BOOTLOADER_ADDR). That can't run on the host, so
 * it is not exercised here — covered by bring-up on target hardware.
 */
#include "unity.h"
#include "config.h"
#include "stm32f0xx_hal.h"

static uint8_t mock_uart_busy;
uint8_t uart_tx_busy(void) { return mock_uart_busy; }

static uint8_t mock_arm_pending_called;
void boot_meta_arm_pending(void) { mock_arm_pending_called = 1; }

static uint8_t mock_power_is_idle;
uint8_t power_is_idle(void) { return mock_power_is_idle; }

static uint8_t mock_power_safe_state_called;
void power_safe_state(void) { mock_power_safe_state_called = 1; }

static uint8_t mock_graceful_shutdown_begin_called;
void power_graceful_shutdown_begin(void) { mock_graceful_shutdown_begin_called = 1; }

static uint8_t mock_ack_cmd;
static uint8_t mock_ack_status;
static uint8_t mock_ack_called;
void uart_send_ack(uint8_t cmd, uint8_t status)
{
    mock_ack_cmd = cmd;
    mock_ack_status = status;
    mock_ack_called = 1;
}

volatile uint32_t systick_ms;

#include "bootloader.c"

void setUp(void)
{
    hal_stub_reset();
    mock_uart_busy = 0;
    mock_arm_pending_called = 0;
    mock_power_is_idle = 0;
    mock_power_safe_state_called = 0;
    mock_graceful_shutdown_begin_called = 0;
    mock_ack_cmd = 0;
    mock_ack_status = 0xFFU;
    mock_ack_called = 0;
    boot_magic   = 0;
    boot_pending = 0;
    enter_shutdown_pending = 0;
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

    TEST_ASSERT_EQUAL_UINT8(1, mock_arm_pending_called);
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

/* ===== bootloader_enter_request() / graceful-shutdown gate ===== */

void test_enter_request_starts_graceful_shutdown_without_safe_state_yet(void)
{
    bootloader_enter_request();

    TEST_ASSERT_EQUAL_UINT8(1, mock_graceful_shutdown_begin_called);
    TEST_ASSERT_EQUAL_UINT8(0, mock_power_safe_state_called);
    TEST_ASSERT_EQUAL_UINT8(0, mock_ack_called);
    TEST_ASSERT_EQUAL_UINT8(0, boot_pending);
}

void test_process_waits_for_idle_before_safe_state_and_ack(void)
{
    bootloader_enter_request();
    mock_power_is_idle = 0;

    bootloader_process();
    bootloader_process();

    TEST_ASSERT_EQUAL_UINT8(0, mock_power_safe_state_called);
    TEST_ASSERT_EQUAL_UINT8(0, mock_ack_called);
    TEST_ASSERT_EQUAL_UINT8(0, boot_pending);
}

void test_process_runs_safe_state_ack_and_schedule_once_idle(void)
{
    bootloader_enter_request();
    mock_power_is_idle = 1;

    bootloader_process();

    TEST_ASSERT_EQUAL_UINT8(1, mock_power_safe_state_called);
    TEST_ASSERT_EQUAL_UINT8(1, mock_ack_called);
    TEST_ASSERT_EQUAL_HEX8(CMD_BOOTLOADER_ENTER, mock_ack_cmd);
    TEST_ASSERT_EQUAL_UINT8(0, mock_ack_status);
    TEST_ASSERT_EQUAL_UINT8(1, boot_pending);
    /* Reset dispatch itself still gated on uart_tx_busy(), as before. */
    TEST_ASSERT_EQUAL_UINT32(0, hal_stub_nvic_reset_count);
}

void test_process_does_not_rerun_safe_state_ack_after_idle_phase_done(void)
{
    bootloader_enter_request();
    mock_power_is_idle = 1;
    bootloader_process();

    mock_power_safe_state_called = 0;
    mock_ack_called = 0;
    mock_uart_busy = 1;

    bootloader_process();
    bootloader_process();

    TEST_ASSERT_EQUAL_UINT8(0, mock_power_safe_state_called);
    TEST_ASSERT_EQUAL_UINT8(0, mock_ack_called);
    TEST_ASSERT_EQUAL_UINT8(1, boot_pending);
    TEST_ASSERT_EQUAL_UINT32(0, hal_stub_nvic_reset_count);
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
    RUN_TEST(test_enter_request_starts_graceful_shutdown_without_safe_state_yet);
    RUN_TEST(test_process_waits_for_idle_before_safe_state_and_ack);
    RUN_TEST(test_process_runs_safe_state_ack_and_schedule_once_idle);
    RUN_TEST(test_process_does_not_rerun_safe_state_ack_after_idle_phase_done);
    return UNITY_END();
}
