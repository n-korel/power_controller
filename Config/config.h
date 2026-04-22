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

#define THRESH_I_LCD_MAX     2000U
#define THRESH_I_BL_MAX      3000U
#define THRESH_I_SCALER_MAX  1500U
#define THRESH_I_AUDIO_LR_MAX 800U

/* ===== Sequencing timings (ms) ===== */
#define SEQ_DELAY_SCALER_ON  50U
#define SEQ_DELAY_RST_RELEASE 20U
#define SEQ_DELAY_LCD_ON     50U
#define SEQ_DELAY_PWM_OFF    10U
#define SEQ_DELAY_BL_OFF     20U
#define SEQ_DELAY_LCD_OFF    20U
#define SEQ_VERIFY_TIMEOUT   200U

/* Sequencing ADC verification thresholds (mV on ADC pin) */
#define SEQ_VERIFY_SCALER_MV 4000U
#define SEQ_VERIFY_LCD_MV    2800U
#define SEQ_VERIFY_BL_MV     9000U

/* PGOOD timeout (Rules 6.5) */
#define PGOOD_TIMEOUT_MS     5000U

/* ===== SUS_S3# (Rules 8) ===== */
#define SUS_S3_THRESHOLD_MS  500U
#define SUS_S3_COOLDOWN_MS   5000U
#define PWRBTN_PULSE_MS      150U

/* ===== Audio (Rules 9) ===== */
#define AUDIO_SDZ_DELAY_MS   10U
#define AUDIO_MUTE_DELAY_MS  10U

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
#define CMD_NACK             0xFFU

#define GET_STATUS_DATA_LEN  26U
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

/* ===== Bootloader (Rules 10) ===== */
#define SRAM_MAGIC_VALUE     0xDEADBEEFU
#define ROM_BOOTLOADER_ADDR  0x1FFF0000U

/* ===== Global systick (0.3) ===== */
extern volatile uint32_t systick_ms;

#endif /* CONFIG_H */
