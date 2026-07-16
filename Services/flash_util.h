#ifndef FLASH_UTIL_H
#define FLASH_UTIL_H

#include <stdint.h>

/* CRC-32 (Ethernet/ZIP): poly reflected 0xEDB88320, init/xorout 0xFFFFFFFF. */
uint32_t sw_crc32(const uint8_t *data, uint32_t len);

#endif /* FLASH_UTIL_H */
