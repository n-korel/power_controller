#ifndef FLASH_CAL_H
#define FLASH_CAL_H

#include <stdint.h>

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint16_t offset_raw[5];   /* current channel offsets (raw ADC) */
    uint32_t crc32;
} __attribute__((packed)) flash_cal_t;

void    flash_cal_load(void);
uint8_t flash_cal_calibrate(void);

#endif /* FLASH_CAL_H */
