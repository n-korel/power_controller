#include "flash_cal.h"
#include "config.h"
#include "adc_service.h"
#include "stm32f0xx_hal.h"
#include <string.h>
#include <stddef.h>

/* ===== Software CRC32 (no float, simple implementation) ===== */
static uint32_t sw_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320U;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

/* ===== Load calibration from Flash at startup ===== */
void flash_cal_load(void)
{
    const flash_cal_t *cal = (const flash_cal_t *)FLASH_CAL_ADDR;

    if (cal->magic != FLASH_CAL_MAGIC || cal->version != FLASH_CAL_VERSION) {
        /* Invalid — use defaults */
        return;
    }

    uint32_t payload_len = offsetof(flash_cal_t, crc32);
    uint32_t crc = sw_crc32((const uint8_t *)cal, payload_len);
    if (crc != cal->crc32)
        return;

    for (uint8_t i = 0; i < CURRENT_CHANNELS; i++)
        adc_set_current_offset(i, cal->offset_raw[i]);
}

/* ===== Calibrate: snapshot current channel raw values and write to Flash ===== */
uint8_t flash_cal_calibrate(void)
{
    flash_cal_t cal;
    memset(&cal, 0, sizeof(cal));
    cal.magic    = FLASH_CAL_MAGIC;
    cal.version  = FLASH_CAL_VERSION;

    /* Current channel ADC indices */
    static const uint8_t c_adc_idx[CURRENT_CHANNELS] = {
        ADC_IDX_LCD_CURRENT, ADC_IDX_BL_CURRENT, ADC_IDX_SCALER_CURRENT,
        ADC_IDX_AUDIO_L_CURRENT, ADC_IDX_AUDIO_R_CURRENT
    };

    for (uint8_t i = 0; i < CURRENT_CHANNELS; i++)
        cal.offset_raw[i] = adc_get_raw_avg(c_adc_idx[i]);

    uint32_t payload_len = offsetof(flash_cal_t, crc32);
    cal.crc32 = sw_crc32((const uint8_t *)&cal, payload_len);

    /* Erase page */
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase;
    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = FLASH_CAL_ADDR;
    erase.NbPages     = 1;
    uint32_t page_error = 0;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return 1;
    }

    /* Write 32-bit words (memcpy to avoid packed alignment warning) */
    uint8_t cal_bytes[sizeof(flash_cal_t)];
    memcpy(cal_bytes, &cal, sizeof(cal_bytes));
    uint32_t words = (sizeof(flash_cal_t) + 3) / 4;
    for (uint32_t i = 0; i < words; i++) {
        uint32_t word;
        memcpy(&word, &cal_bytes[i * 4], sizeof(word));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              FLASH_CAL_ADDR + (i * 4), word) != HAL_OK) {
            HAL_FLASH_Lock();
            return 1;
        }
    }

    HAL_FLASH_Lock();

    /* Apply new offsets immediately */
    for (uint8_t i = 0; i < CURRENT_CHANNELS; i++)
        adc_set_current_offset(i, cal.offset_raw[i]);

    return 0;
}
