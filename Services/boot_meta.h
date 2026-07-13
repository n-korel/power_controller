#ifndef BOOT_META_H
#define BOOT_META_H

#include <stdint.h>

/* Cortex-M0: crc32 must be 4-byte aligned.
   Layout (16 bytes):
     0..3   magic
     4..5   version
     6      confirmed
     7      boot_attempts
     8..11  firmware_crc
     12..15 crc32
   CRC32 is computed over bytes 0..11 (offsetof(crc32)). */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  confirmed;
    uint8_t  boot_attempts;
    uint32_t firmware_crc;
    uint32_t crc32;
} boot_meta_t;

void     boot_meta_on_startup(void);
uint8_t  boot_meta_confirm(void);
uint32_t boot_meta_image_crc(void);

#endif /* BOOT_META_H */
