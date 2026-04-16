#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>

/* Check SRAM magic before HAL_Init — call from USER CODE BEGIN 1 */
void bootloader_check(void);

/* Schedule jump after TX completes (called from BOOTLOADER_ENTER handler) */
void bootloader_schedule(void);

/* Call from main loop to execute pending jump */
void bootloader_process(void);

#endif /* BOOTLOADER_H */
