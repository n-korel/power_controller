#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ===== ADC (Rules 2.2, 5.1-5.2) ===== */
#define ADC_VREF_MV         2500U
#define ADC_RESOLUTION       4096U
#define ADC_CHANNEL_COUNT    14

typedef enum {
    ADC_IDX_LCD_CURRENT      = 0,   /* PA0  IN0  */
    ADC_IDX_BL_CURRENT       = 1,   /* PA1  IN1  */
    ADC_IDX_SCALER_CURRENT   = 2,   /* PA4  IN4  */
    ADC_IDX_AUDIO_L_CURRENT  = 3,   /* PA5  IN5  */
    ADC_IDX_AUDIO_R_CURRENT  = 4,   /* PA6  IN6  */
    ADC_IDX_LCD_POWER        = 5,   /* PA7  IN7  */
    ADC_IDX_BL_POWER         = 6,   /* PB0  IN8  */
    ADC_IDX_SCALER_POWER     = 7,   /* PB1  IN9  */
    ADC_IDX_V24              = 8,   /* PC0  IN10 */
    ADC_IDX_V12              = 9,   /* PC1  IN11 */
    ADC_IDX_V5               = 10,  /* PC2  IN12 */
    ADC_IDX_V3V3             = 11,  /* PC3  IN13 */
    ADC_IDX_TEMP0            = 12,  /* PC4  IN14 */
    ADC_IDX_TEMP1            = 13   /* PC5  IN15 */
} adc_index_t;

/* Voltage divider: Vin = Vadc * 11616 / 1000 (Rules 2.3) */
#define VDIV_MULT            11616U
#define VDIV_DIV             1000U
#define ADC_RAIL_SCALE_MV    (ADC_VREF_MV * VDIV_MULT / VDIV_DIV)

/* Convert a raw ADC sample into rail voltage (mV at the divider input).
 * Single source of truth used by adc_service and power_manager sequencing.
 * Max intermediate value ~119M, fits uint32_t. */
#define ADC_RAIL_MV_FROM_RAW(raw) \
    ((uint32_t)(raw) * ADC_RAIL_SCALE_MV / ADC_RESOLUTION)

/* Current sensor: 264 mV/A, default Voffset = 1650 mV (Rules 2.2) */
#define CURRENT_SENSITIVITY_UV_PER_A  264000U
#define CURRENT_SENSITIVITY_MV_PER_A  264U
#define CURRENT_VOFFSET_MV_DEFAULT    1650U

/* ===== ADC filter (Rules 5.3) ===== */
#define ADC_WINDOW_SIZE      8
#define FAULT_CONFIRM_COUNT  5

/* ===== Debounce (Rules 16) ===== */
#define DEBOUNCE_MS          20U

/* ===== Default thresholds (mV / mA) ===== */
#define THRESH_V24_MIN       20000U
#define THRESH_V24_MAX       26000U
#define THRESH_V12_MIN       10000U
#define THRESH_V12_MAX       13000U
#define THRESH_V5_MIN        4500U
#define THRESH_V5_MAX        5500U
#define THRESH_V3V3_MIN      3000U
#define THRESH_V3V3_MAX      3600U

/* 1 = monitor V+24 (PC0) and latch FAULT_V24_RANGE; 0 = skip (boards without V+24 on V24_M). */
#define ENABLE_V24_FAULT_CHECK  1U

/* ===== Board revision feature switches =====
 * TOUCH is not present on this revision: keep disabled (no auto-start / POWER_CTRL). */
#define ENABLE_AUDIO_HW      1U
#define ENABLE_TOUCH_HW      0U
/* 1 = allow BACKLIGHT ON; 0 = reject BL ON in POWER_CTRL (BOR mitigation, no BL UART tests). */
#define ENABLE_BACKLIGHT_HW  1U
/* §6.1: after PGOOD=HIGH include BACKLIGHT in display auto-startup (full §13.2 UP). */
#define ENABLE_BACKLIGHT_AUTO_STARTUP  1U
#define ENABLE_PGOOD_AUTO_STARTUP  1U

/* U4 (NSM2012): enable BL overcurrent fault and report real i_backlight in GET_STATUS. */
#define ENABLE_BL_CURRENT_SENSOR  1U

/* Backlight supply: R62=+5V_A, R63=+12V_A, R64=+24V (one populated). This board: R63 (+12V). */
#define BACKLIGHT_SUPPLY_5V       0U   /* 0 → SEQ_VERIFY_BL_MV=9000 mV */
/* 1 = verify BACKLIGHT_POWER_M during BL sequence; 0 = skip ADC rail check. */
#define ENABLE_BL_POWER_VERIFY    1U
/* Internal BOR/reset breadcrumb in last_power_ctrl_value (0xE1..0xE6),
 * latched from .noinit across reset: BL/SCALER/LCD pre+on stages. */
#define ENABLE_BOR_DIAG_MARKER    1U

#define THRESH_I_LCD_MAX     2000U
#define THRESH_I_BL_MAX      3000U
#define THRESH_I_SCALER_MAX  1500U
/* Bench: with SCALER+LCD on, FAULT_AUDIO still latches at 2000; with runtime
 * SET_THRESHOLDS=5000 AUDIO+display stays on while GET_STATUS shows ~20 mA. */
#define THRESH_I_AUDIO_LR_MAX 5000U

/* ===== Sequencing timings (ms) ===== */
#define SEQ_DELAY_SCALER_ON  50U
#define SEQ_DELAY_RST_RELEASE 20U
#define SEQ_DELAY_LCD_ON     50U
#define SEQ_DELAY_BL_ON      20U
#define SEQ_DELAY_BL_RAMP_HOLD_MS 200U
#define SEQ_DELAY_PWM_OFF    10U
#define SEQ_DELAY_BL_OFF     20U
#define SEQ_DELAY_LCD_OFF    20U
#define SEQ_VERIFY_TIMEOUT   200U

/* Sequencing ADC verification thresholds (mV on ADC pin) */
#define SEQ_VERIFY_SCALER_MV 4000U
#define SEQ_VERIFY_LCD_MV    2800U
#if (BACKLIGHT_SUPPLY_5V != 0U)
#define SEQ_VERIFY_BL_MV     4000U
#else
#define SEQ_VERIFY_BL_MV     9000U
#endif

/* PWM soft-start after BACKLIGHT_ON (reduces inrush when U4 is bypassed). */
#define BL_SOFTSTART_RAMP_MS   2000U

/* Window after BACKLIGHT_ON GPIO HIGH where a PGOOD dip is ignored (BL inrush). */
#define SEQ_BL_PGOOD_GRACE_MS  1000U

/* Default backlight PWM used when BACKLIGHT is enabled before any SET_BRIGHTNESS. */
#define BACKLIGHT_DEFAULT_PWM_ON 50U

/* PGOOD timeout (Rules 6.5) */
#define PGOOD_TIMEOUT_MS     5000U

/* ===== SUS_S3# (Rules 8) ===== */
#define SUS_S3_THRESHOLD_MS  500U
#define SUS_S3_COOLDOWN_MS   5000U
#define PWRBTN_PULSE_MS      150U

/* ===== Audio (Rules 9) ===== */
#define AUDIO_SDZ_DELAY_MS   10U
#define AUDIO_MUTE_DELAY_MS  10U
/* After unmute (amp_active=1): ignore I_AUDIO / Faultz (TPA3118 startup spike).
 * Bench: with SCALER+LCD on, FAULT_AUDIO can latch after ~100 ms grace while
 * GET_STATUS still shows ~20 mA — extend blanking to cover post-unmute excursion. */
#define AUDIO_I_GRACE_MS     500U

/* ===== UART protocol (Rules 4) ===== */
#define UART_INTERBYTE_TIMEOUT_MS  10U
#define UART_PACKET_TIMEOUT_MS     50U

#define PROTO_STX            0x02U
#define PROTO_ETX            0x03U

#define CMD_PING             0x01U
#define CMD_POWER_CTRL       0x02U
#define CMD_SET_BRIGHTNESS   0x03U
#define CMD_GET_STATUS       0x04U
#define CMD_RESET_FAULT      0x05U
#define CMD_RESET_BRIDGE     0x06U
#define CMD_SET_THRESHOLDS   0x07U
#define CMD_BOOTLOADER_ENTER 0x08U
#define CMD_CALIBRATE_OFFSET 0x09U
#define CMD_GET_VERSION      0x0AU
#define CMD_NACK             0xFFU

#define NACK_ERR_UNKNOWN_CMD     0x01U
#define NACK_ERR_QUEUE_OVERFLOW  0x02U
#define NACK_ERR_CRC             0x03U
#define NACK_ERR_FRAMING         0x04U
#define NACK_ERR_TIMEOUT         0x05U
#define NACK_ERR_RX_OVERFLOW     0x06U
#define NACK_ERR_GARBAGE         0x07U

#define GET_STATUS_DATA_LEN  22U
#define GET_VERSION_DATA_LEN 13U
#define PING_RESPONSE        0xAAU

/* ===== Domain bitmask (Rules 4.5) ===== */
#define DOM_SCALER           0x01U
#define DOM_LCD              0x02U
#define DOM_BACKLIGHT        0x04U
#define DOM_AUDIO            0x08U
#define DOM_ETH1             0x10U
#define DOM_ETH2             0x20U
#define DOM_TOUCH            0x40U

/* ===== Fault flags bitmask (Rules 7.2) ===== */
#define FAULT_SCALER         0x0001U
#define FAULT_LCD            0x0002U
#define FAULT_BACKLIGHT      0x0004U
#define FAULT_AUDIO          0x0008U
#define FAULT_ETH1           0x0010U
#define FAULT_ETH2           0x0020U
#define FAULT_TOUCH          0x0040U
#define FAULT_PGOOD_LOST     0x0080U
#define FAULT_AMP_FAULTZ     0x0100U
#define FAULT_V24_RANGE      0x0200U
#define FAULT_V12_RANGE      0x0400U
#define FAULT_V5_RANGE       0x0800U
#define FAULT_V3V3_RANGE     0x1000U
#define FAULT_SEQ_ABORT      0x2000U
#define FAULT_INTERNAL       0x4000U
#define FAULT_RESERVED       0x8000U
#define FAULT_BOOT_UNCONFIRMED FAULT_RESERVED  /* OTA pending-confirm exhausted */

/* ===== Bridge reset ===== */
#define BRIDGE_RST_PULSE_MS  10U

/* ===== Flash calibration (Rules 11) ===== */
/* Host-tests may redefine FLASH_CAL_ADDR to a RAM buffer before including this header. */
#ifndef FLASH_CAL_ADDR
#define FLASH_CAL_ADDR       0x0800FC00U
#endif
#ifndef FLASH_CAL_VALID_START
#define FLASH_CAL_VALID_START 0x08000000U
#endif
#ifndef FLASH_CAL_VALID_END
#define FLASH_CAL_VALID_END   0x08010000U
#endif
#ifndef FLASH_CAL_ERASE_SIZE
#define FLASH_CAL_ERASE_SIZE  1024U
#endif
#ifndef FLASH_CAL_RUNTIME_ALIGN_CHECK
#define FLASH_CAL_RUNTIME_ALIGN_CHECK 0U
#endif
#define FLASH_CAL_MAGIC      0x43414C49U  /* "CALI" */
#define FLASH_CAL_VERSION    1U
#define CURRENT_CHANNELS     5

/* ===== OTA boot metadata (pending-confirm / safe-hold) ===== */
/* Host-tests may redefine FLASH_BOOT_META_ADDR to a RAM buffer before including this header. */
#ifndef FLASH_BOOT_META_ADDR
#define FLASH_BOOT_META_ADDR       0x0800F400U
#endif
#ifndef FLASH_BOOT_META_VALID_START
#define FLASH_BOOT_META_VALID_START 0x08000000U
#endif
#ifndef FLASH_BOOT_META_VALID_END
#define FLASH_BOOT_META_VALID_END   0x08010000U
#endif
#ifndef FLASH_BOOT_META_ERASE_SIZE
#define FLASH_BOOT_META_ERASE_SIZE  1024U
#endif
#ifndef FLASH_BOOT_META_RUNTIME_ALIGN_CHECK
#define FLASH_BOOT_META_RUNTIME_ALIGN_CHECK 0U
#endif
#define FLASH_BOOT_META_MAGIC     0x424D4554U /* "BMET" */
#define FLASH_BOOT_META_VERSION   1U
#define BOOT_META_MAX_ATTEMPTS       3U
#define BOOT_META_CONFIRM_STABLE_MS  10000U

/* ===== Bootloader (Rules 10) ===== */
#define SRAM_MAGIC_VALUE     0xDEADBEEFU
/* System memory / ROM USART bootloader (chip-specific):
 *   STM32F030x8 / chipid 0x0440 → 0x1FFFEC00, 3 KB (AN2606 §15)  ← board MCU
 *   APM32F030x8 (if used)       → 0x1FFFD800, 8 KB (SEGGER KB)
 * Not 0x1FFF0000 (other ST lines / wrong for F030). */
#ifndef ROM_BOOTLOADER_ADDR
#define ROM_BOOTLOADER_ADDR  0x1FFFEC00U
#endif
#ifndef ROM_BOOTLOADER_END
#define ROM_BOOTLOADER_END   0x1FFFEFFFU
#endif

/* ===== Global systick (0.3) ===== */
extern volatile uint32_t systick_ms;

#endif /* CONFIG_H */
