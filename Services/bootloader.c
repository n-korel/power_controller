#include "bootloader.h"
#include "config.h"
#include "uart_protocol.h"
#include "stm32f0xx_hal.h"

static volatile uint8_t boot_pending;

/* ===== Check SRAM magic (called BEFORE HAL_Init) ===== */
void bootloader_check(void)
{
    volatile uint32_t *magic = (volatile uint32_t *)SRAM_MAGIC_ADDR;
    if (*magic == SRAM_MAGIC_VALUE) {
        *magic = 0;

        /* Jump to ROM bootloader */
        uint32_t boot_addr = ROM_BOOTLOADER_ADDR;
        uint32_t *vec = (uint32_t *)boot_addr;
        uint32_t sp   = vec[0];
        uint32_t pc   = vec[1];

        /* Set MSP and jump */
        __set_MSP(sp);
        void (*jump)(void) = (void (*)(void))pc;
        jump();

        /* Should never reach here */
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

    /* Wait for ACK transmission to complete */
    if (uart_tx_busy()) return;

    /* Write SRAM magic and reset */
    volatile uint32_t *magic = (volatile uint32_t *)SRAM_MAGIC_ADDR;
    *magic = SRAM_MAGIC_VALUE;
    NVIC_SystemReset();
}
