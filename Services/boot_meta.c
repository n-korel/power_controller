#include "boot_meta.h"
#include "config.h"
#include "bootloader.h"
#include "stm32f0xx_hal.h"
#include <string.h>
#include <stddef.h>

#ifndef BOOT_META_ADDR
#define BOOT_META_ADDR        0x0800F800U   /* page before FLASH_CAL_ADDR */
#endif
#ifndef BOOT_META_MAGIC
#define BOOT_META_MAGIC       0x544F4F42U   /* "BOOT" */
#endif
#ifndef BOOT_META_VERSION
#define BOOT_META_VERSION     1U
#endif
#ifndef BOOT_META_MAX_ATTEMPTS
#define BOOT_META_MAX_ATTEMPTS 3U
#endif

/* Patched post-link by scripts/fw_sign.py: CRC32 of [FLASH_BASE, _flash_image_end)
 * with these 4 bytes themselves treated as zero during the calculation.
 * Placed in its own linker section (.fw_crc, STM32F030XX_FLASH.ld) so both the
 * script and compute_image_crc() below agree on its exact address.
 *
 * Host tests redirect the image scan to a RAM buffer (see Tests/test_boot_meta.c). */
#if defined(UNIT_TEST) && defined(BOOT_META_TEST_IMAGE)
#define BOOT_META_IMG_START   ((const uint8_t *)(BOOT_META_TEST_IMAGE))
#define BOOT_META_IMG_END     (BOOT_META_TEST_IMAGE_END)
#define BOOT_META_CRC_SLOT    ((const uint8_t *)(BOOT_META_TEST_CRC_SLOT))
#define BOOT_META_CRC_VALUE   (*(BOOT_META_TEST_CRC_SLOT))
#else
extern uint32_t _flash_image_end;
__attribute__((used, section(".fw_crc")))
static const uint32_t g_fw_image_crc = 0xFFFFFFFFU;
#define BOOT_META_IMG_START   ((const uint8_t *)FLASH_BASE)
#define BOOT_META_IMG_END     ((const uint8_t *)&_flash_image_end)
#define BOOT_META_CRC_SLOT    ((const uint8_t *)&g_fw_image_crc)
#define BOOT_META_CRC_VALUE   (g_fw_image_crc)
#endif

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

/* Same recipe as fw_sign.py: scan the live flash image, but feed zero bytes
 * for the 4 bytes occupied by g_fw_image_crc itself. */
static uint32_t compute_image_crc(void)
{
    const uint8_t *start = BOOT_META_IMG_START;
    const uint8_t *end   = BOOT_META_IMG_END;
    const uint8_t *crc_lo = BOOT_META_CRC_SLOT;
    const uint8_t *crc_hi = crc_lo + sizeof(uint32_t);

    uint32_t crc = 0xFFFFFFFF;
    for (const uint8_t *p = start; p < end; p++) {
        uint8_t byte = (p >= crc_lo && p < crc_hi) ? 0U : *p;
        crc ^= byte;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320U;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

static uint8_t boot_meta_read(boot_meta_t *out)
{
    const boot_meta_t *m = (const boot_meta_t *)BOOT_META_ADDR;
    if (m->magic != BOOT_META_MAGIC || m->version != BOOT_META_VERSION)
        return 0;
    uint32_t payload_len = offsetof(boot_meta_t, crc32);
    if (sw_crc32((const uint8_t *)m, payload_len) != m->crc32)
        return 0;
    memcpy(out, m, sizeof(*out));
    return 1;
}

static uint8_t boot_meta_write(const boot_meta_t *in)
{
    boot_meta_t rec;
    memcpy(&rec, in, sizeof(rec));
    uint32_t payload_len = offsetof(boot_meta_t, crc32);
    rec.crc32 = sw_crc32((const uint8_t *)&rec, payload_len);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase;
    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = BOOT_META_ADDR;
    erase.NbPages     = 1;
    uint32_t page_error = 0;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return 0;
    }

    uint8_t rec_bytes[sizeof(boot_meta_t)];
    memcpy(rec_bytes, &rec, sizeof(rec_bytes));
    uint32_t words = (sizeof(boot_meta_t) + 3U) / 4U;
    for (uint32_t i = 0; i < words; i++) {
        uint32_t word;
        memcpy(&word, &rec_bytes[i * 4], sizeof(word));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              BOOT_META_ADDR + (i * 4), word) != HAL_OK) {
            HAL_FLASH_Lock();
            return 0;
        }
    }

    HAL_FLASH_Lock();
    return 1;
}

void boot_meta_on_startup(void)
{
    boot_meta_t m;
    if (!boot_meta_read(&m))
        return;

    if (m.confirmed) {
        if (m.firmware_crc == compute_image_crc())
            return;
        /* New image since last confirm — re-enter pending-confirm flow. */
        m.confirmed     = 0U;
        m.boot_attempts = BOOT_META_MAX_ATTEMPTS;
        if (!boot_meta_write(&m))
            return;
        return; /* transition boot after OTA — do not consume an attempt */
    }

    if (m.boot_attempts == 0U) {
        bootloader_schedule();
        return;
    }

    m.boot_attempts--;
    (void)boot_meta_write(&m);
}

uint8_t boot_meta_confirm(void)
{
    uint32_t computed = compute_image_crc();
    if (computed != BOOT_META_CRC_VALUE)
        return 1;

    boot_meta_t m;
    memset(&m, 0, sizeof(m));
    m.magic         = BOOT_META_MAGIC;
    m.version       = BOOT_META_VERSION;
    m.confirmed     = 1U;
    m.boot_attempts = BOOT_META_MAX_ATTEMPTS;
    m.firmware_crc  = computed;
    return boot_meta_write(&m) ? 0U : 1U;
}

uint32_t boot_meta_image_crc(void)
{
    return BOOT_META_CRC_VALUE;
}
