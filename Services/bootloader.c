#include "bootloader.h"
#include "boot_meta.h"
#include "config.h"
#include "uart_protocol.h"
#include "power_manager.h"
#include "stm32f0xx_hal.h"

/* Placed in .noinit — Reset_Handler does not clear/init this region,
   so the value survives NVIC_SystemReset(). */
static volatile uint32_t boot_magic __attribute__((section(".noinit")));

static volatile uint8_t boot_pending;
static uint8_t enter_shutdown_pending;

/* ===== Check SRAM magic (called BEFORE HAL_Init) ===== */
void bootloader_check(void)
{
    if (boot_magic != SRAM_MAGIC_VALUE) {
        return;
    }
    boot_magic = 0;

    /* Cast via uintptr_t: uint32_t on Cortex-M0, uint64_t on host
     * (clang-tidy static analysis). Identical binary on the MCU. */
    uint32_t *vec = (uint32_t *)(uintptr_t)ROM_BOOTLOADER_ADDR;
    uint32_t sp   = vec[0];
    uint32_t pc   = vec[1];

    /* Validate ROM vector table before jumping (ROM_BOOTLOADER_* in config.h).
     * Board MCU (chipid 0x0440 / STM32F030x8): 0x1FFFEC00 — not 0x1FFF0000. */
    if (sp < 0x20000000U || sp > 0x20002000U) {
        return;
    }
    if (pc < ROM_BOOTLOADER_ADDR || pc > ROM_BOOTLOADER_END) {
        return;
    }

    __set_MSP(sp);
    void (*jump)(void) = (void (*)(void))(uintptr_t)pc;
    jump();

    while (1) {}
}

/* ===== Schedule bootloader entry (after ACK TX completes) ===== */
void bootloader_schedule(void)
{
    boot_pending = 1;
}

/* ===== BOOTLOADER_ENTER entry point =====
 * Kicks off the graceful DN/OFF sequencers instead of cutting every domain
 * at once; bootloader_process() finishes the job (power_safe_state() + ACK +
 * schedule) once power_is_idle(). */
void bootloader_enter_request(void)
{
    power_graceful_shutdown_begin();
    enter_shutdown_pending = 1;
}

/* ===== Process (call from main loop) ===== */
void bootloader_process(void)
{
    if (enter_shutdown_pending) {
        /* cppcheck-suppress knownConditionTrueFalse ; power_is_idle() reflects
           runtime sequencer state; false positive under unit-test stubs. */
        if (!power_is_idle()) return;
        power_safe_state();
        uart_send_ack(CMD_BOOTLOADER_ENTER, 0);
        enter_shutdown_pending = 0;
        bootloader_schedule();
        return;
    }

    if (!boot_pending) return;
    /* cppcheck-suppress knownConditionTrueFalse ; uart_tx_busy() reflects runtime TX state;
       false positive when bootloader.c is #included from unit-test stubs. */
    if (uart_tx_busy()) return;

    boot_meta_arm_pending();
    boot_magic = SRAM_MAGIC_VALUE;
    __DSB();
    boot_pending = 0;
    NVIC_SystemReset();
}
