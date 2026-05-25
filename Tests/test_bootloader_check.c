/*
 * Unit tests: bootloader_check() ROM vector validation — Rules_POWER.md §10.
 *
 * On the MCU, bootloader_check() reads the ROM vector table at ROM_BOOTLOADER_ADDR.
 * Host tests redirect that address to a local fake vector table.
 *
 * Covered here (early return, no __set_MSP / no jump):
 *   - SRAM magic set, SP below SRAM base (< 0x20000000)
 *   - SRAM magic set, PC below ROM_BOOTLOADER_ADDR
 *
 * schedule/process semantics live in test_bootloader.c.
 */
#include "unity.h"
#include "config.h"

static uint32_t fake_rom_vec[2];

#undef ROM_BOOTLOADER_ADDR
/* uintptr_t: bootloader.c casts via (uintptr_t); uint32_t would truncate on host. */
#define ROM_BOOTLOADER_ADDR ((uintptr_t)fake_rom_vec)

#include "stm32f0xx_hal.h"

uint8_t uart_tx_busy(void) { return 0; }

volatile uint32_t systick_ms;

#include "bootloader.c"

static const uint32_t k_valid_sp = 0x20001000U;

void setUp(void)
{
    hal_stub_reset();
    boot_magic = 0;
    fake_rom_vec[0] = k_valid_sp;
    fake_rom_vec[1] = (uint32_t)(uintptr_t)&fake_rom_vec[1];
}

void tearDown(void) {}

void test_check_invalid_sp_returns_without_jump(void)
{
    boot_magic = SRAM_MAGIC_VALUE;
    fake_rom_vec[0] = 0x1FFFFFFFU; /* < 0x20000000 */
    fake_rom_vec[1] = (uint32_t)(uintptr_t)&fake_rom_vec[1];

    bootloader_check();

    TEST_ASSERT_EQUAL_UINT32(0, boot_magic);
    TEST_ASSERT_EQUAL_UINT32(0, hal_stub_set_msp_count);
}

void test_check_invalid_pc_returns_without_jump(void)
{
    boot_magic = SRAM_MAGIC_VALUE;
    fake_rom_vec[0] = k_valid_sp;
    fake_rom_vec[1] = (uint32_t)(ROM_BOOTLOADER_ADDR - 4U);

    bootloader_check();

    TEST_ASSERT_EQUAL_UINT32(0, boot_magic);
    TEST_ASSERT_EQUAL_UINT32(0, hal_stub_set_msp_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_invalid_sp_returns_without_jump);
    RUN_TEST(test_check_invalid_pc_returns_without_jump);
    return UNITY_END();
}
