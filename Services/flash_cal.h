#ifndef FLASH_CAL_H
#define FLASH_CAL_H

#include <stdint.h>

/* Cortex-M0 does not support unaligned access, so crc32 must be 4-byte aligned.
   Layout (24 bytes, naturally aligned):
     0..3   magic
     4..5   version
     6..7   reserved
     8..17  offset_raw[5]
     18..19 reserved2 (explicit padding for crc32 alignment)
     20..23 crc32
   CRC32 is computed over bytes 0..19 (offsetof(crc32)). */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint16_t offset_raw[5];
    uint16_t reserved2;
    uint32_t crc32;
} flash_cal_t;

void    flash_cal_load(void);
uint8_t flash_cal_calibrate(void);

#endif /* FLASH_CAL_H */
