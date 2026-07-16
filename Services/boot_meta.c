#include "boot_meta.h"
#include "flash_util.h"
#include "config.h"
#include "version_gen.h"
#include "stm32f0xx_hal.h"
#include <string.h>
#include <stddef.h>

static uint8_t s_safe_hold;
static uint8_t s_pending;
static uint8_t s_confirmed_this_boot;

static uint8_t boot_meta_addr_valid_for_load(void)
{
    uintptr_t start = (uintptr_t)FLASH_BOOT_META_ADDR;
    uintptr_t end_excl;

#if FLASH_BOOT_META_RUNTIME_ALIGN_CHECK
    if ((start & 0x3U) != 0U)
        return 0;
#endif
    if (sizeof(boot_meta_t) > ((uintptr_t)-1 - start))
        return 0;

    end_excl = start + sizeof(boot_meta_t);
    if (start < (uintptr_t)FLASH_BOOT_META_VALID_START)
        return 0;
    if (end_excl > (uintptr_t)FLASH_BOOT_META_VALID_END)
        return 0;

    return 1;
}

static uint8_t boot_meta_addr_valid_for_erase(void)
{
    uintptr_t start = (uintptr_t)FLASH_BOOT_META_ADDR;
    uintptr_t end_excl;

    if (FLASH_BOOT_META_ERASE_SIZE == 0U)
        return 0;
#if FLASH_BOOT_META_RUNTIME_ALIGN_CHECK
    if ((start & 0x3U) != 0U)
        return 0;
#endif
    if ((uintptr_t)FLASH_BOOT_META_ERASE_SIZE > ((uintptr_t)-1 - start))
        return 0;

    end_excl = start + (uintptr_t)FLASH_BOOT_META_ERASE_SIZE;
    if (start < (uintptr_t)FLASH_BOOT_META_VALID_START)
        return 0;
    if (end_excl > (uintptr_t)FLASH_BOOT_META_VALID_END)
        return 0;

    return 1;
}

static uint8_t boot_meta_write(const boot_meta_t *meta)
{
    boot_meta_t out;
    uint8_t bytes[sizeof(boot_meta_t)];
    uint32_t words;
    uint32_t i;
    uint32_t page_error = 0;
    FLASH_EraseInitTypeDef erase;

    if (!boot_meta_addr_valid_for_load() || !boot_meta_addr_valid_for_erase())
        return 1;

    memcpy(&out, meta, sizeof(out));
    out.crc32 = sw_crc32((const uint8_t *)&out, offsetof(boot_meta_t, crc32));

    HAL_FLASH_Unlock();

    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = FLASH_BOOT_META_ADDR;
    erase.NbPages     = 1;
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return 1;
    }

    memcpy(bytes, &out, sizeof(bytes));
    words = (sizeof(boot_meta_t) + 3U) / 4U;
    for (i = 0; i < words; i++) {
        uint32_t word;
        memcpy(&word, &bytes[i * 4U], sizeof(word));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              FLASH_BOOT_META_ADDR + (i * 4U), word) != HAL_OK) {
            HAL_FLASH_Lock();
            return 1;
        }
    }

    HAL_FLASH_Lock();
    return 0;
}

static void copy_fw_hash(uint8_t out[8])
{
    const char *hash = FW_GIT_HASH_STR;
    uint8_t i;

    for (i = 0; i < 8U; i++)
        out[i] = (uint8_t)hash[i];
}

static uint8_t hash_matches_fw(const uint8_t tracked[8])
{
    uint8_t fw[8];

    copy_fw_hash(fw);
    return (memcmp(tracked, fw, 8) == 0) ? 1U : 0U;
}

void boot_meta_init(void)
{
    boot_meta_t meta;

    s_safe_hold = 0;
    s_pending = 0;
    s_confirmed_this_boot = 0;

    if (!boot_meta_addr_valid_for_load())
        return;

    memcpy(&meta, (const void *)FLASH_BOOT_META_ADDR, sizeof(meta));

    if (meta.magic != FLASH_BOOT_META_MAGIC || meta.version != FLASH_BOOT_META_VERSION)
        return;

    if (sw_crc32((const uint8_t *)&meta, offsetof(boot_meta_t, crc32)) != meta.crc32)
        return;

    if (meta.pending_confirm == 0U)
        return;

    s_pending = 1;

    if (!hash_matches_fw(meta.tracked_hash)) {
        copy_fw_hash(meta.tracked_hash);
        meta.boot_attempts = 1U;
    } else {
        if (meta.boot_attempts < 0xFFU)
            meta.boot_attempts++;
    }

    (void)boot_meta_write(&meta);

    if (meta.boot_attempts >= meta.max_attempts)
        s_safe_hold = 1;
}

void boot_meta_process(void)
{
    if (s_pending == 0U || s_confirmed_this_boot != 0U)
        return;

    if (systick_ms >= BOOT_META_CONFIRM_STABLE_MS)
        boot_meta_confirm();
}

uint8_t boot_meta_safe_hold(void)
{
    return s_safe_hold;
}

void boot_meta_confirm(void)
{
    boot_meta_t meta;

    /* No flash write when already confirmed — avoid wear on every RESET_FAULT. */
    if (s_pending == 0U) {
        s_safe_hold = 0;
        s_confirmed_this_boot = 1;
        return;
    }

    memset(&meta, 0, sizeof(meta));
    meta.magic   = FLASH_BOOT_META_MAGIC;
    meta.version = FLASH_BOOT_META_VERSION;

    (void)boot_meta_write(&meta);

    s_pending = 0;
    s_safe_hold = 0;
    s_confirmed_this_boot = 1;
}

void boot_meta_arm_pending(void)
{
    boot_meta_t meta;

    memset(&meta, 0, sizeof(meta));
    meta.magic           = FLASH_BOOT_META_MAGIC;
    meta.version         = FLASH_BOOT_META_VERSION;
    meta.pending_confirm = 1U;
    meta.boot_attempts   = 0U;
    meta.max_attempts    = (uint8_t)BOOT_META_MAX_ATTEMPTS;
    /* tracked_hash left zero: first boot of the new image sets it in init() */

    (void)boot_meta_write(&meta);
}
