#include "bootloader.h"
#include "config.h"
#include "uart_protocol.h"
#include "stm32f0xx_hal.h"

/* Placed in .noinit — Reset_Handler does not clear/init this region,
   so the value survives NVIC_SystemReset(). */
static volatile uint32_t boot_magic __attribute__((section(".noinit")));

static volatile uint8_t boot_pending;

/* ===== Check SRAM magic (called BEFORE HAL_Init) ===== */
void bootloader_check(void)
{
    if (boot_magic == SRAM_MAGIC_VALUE) {
        boot_magic = 0;

        /* Cast via uintptr_t: uint32_t on Cortex-M0, uint64_t on host
         * (clang-tidy static analysis). Identical binary on the MCU. */
        uint32_t *vec = (uint32_t *)(uintptr_t)ROM_BOOTLOADER_ADDR;
        uint32_t sp   = vec[0];

        __set_MSP(sp);
        void (*jump)(void) = (void (*)(void))(uintptr_t)vec[1];
        jump();

        while (1) {}
    }
}

/* ===== Schedule bootloader entry (after ACK TX completes) ===== */
void bootloader_schedule(void)
{
    boot_pending = 1;
}

/* ===== Process (call from main loop) ===== */
void bootloader_process(void)
{
    if (!boot_pending) return;
    if (uart_tx_busy()) return;

    boot_magic = SRAM_MAGIC_VALUE;
    __DSB();
    NVIC_SystemReset();
}
