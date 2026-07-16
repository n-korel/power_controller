#ifndef BOOT_META_H
#define BOOT_META_H

#include <stdint.h>

/* Cortex-M0 does not support unaligned access, so crc32 must be 4-byte aligned.
   Layout (24 bytes):
     0..3   magic
     4..5   version
     6      pending_confirm
     7      boot_attempts
     8      max_attempts
     9..16  tracked_hash[8]
     17     reserved
     18..19 reserved2 (padding for crc32 alignment)
     20..23 crc32
   CRC32 is computed over bytes 0..19 (offsetof(crc32)). */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  pending_confirm;
    uint8_t  boot_attempts;
    uint8_t  max_attempts;
    uint8_t  tracked_hash[8];
    uint8_t  reserved;
    uint16_t reserved2;
    uint32_t crc32;
} boot_meta_t;

void    boot_meta_init(void);
void    boot_meta_process(void);
uint8_t boot_meta_safe_hold(void);
void    boot_meta_confirm(void);
void    boot_meta_arm_pending(void);

#endif /* BOOT_META_H */
