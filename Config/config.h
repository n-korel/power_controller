#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ===== ADC (Rules 2.2, 5.1-5.2) ===== */
#define ADC_VREF_MV         2500u
#define ADC_RESOLUTION       4096u
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
#define VDIV_MULT            11616u
#define VDIV_DIV             1000u

/* Current sensor: 264 mV/A, default Voffset = 1650 mV (Rules 2.2) */
#define CURRENT_SENSITIVITY_UV_PER_A  264000u
#define CURRENT_SENSITIVITY_MV_PER_A  264u
#define CURRENT_VOFFSET_MV_DEFAULT    1650u

/* ===== ADC filter (Rules 5.3) ===== */
#define ADC_WINDOW_SIZE      8
#define FAULT_CONFIRM_COUNT  5

/* ===== Debounce (Rules 16) ===== */
#define DEBOUNCE_MS          20u

/* ===== Default thresholds (mV / mA) ===== */
#define THRESH_V24_MIN       20000u
#define THRESH_V24_MAX       26000u
#define THRESH_V12_MIN       10000u
#define THRESH_V12_MAX       13000u
#define THRESH_V5_MIN        4500u
#define THRESH_V5_MAX        5500u
#define THRESH_V3V3_MIN      3000u
#define THRESH_V3V3_MAX      3600u

#define THRESH_I_LCD_MAX     2000u
#define THRESH_I_BL_MAX      3000u
#define THRESH_I_SCALER_MAX  1500u
#define THRESH_I_AUDIO_LR_MAX 800u

/* ===== Sequencing timings (ms) ===== */
#define SEQ_DELAY_SCALER_ON  50u
#define SEQ_DELAY_RST_RELEASE 20u
#define SEQ_DELAY_LCD_ON     50u
#define SEQ_DELAY_PWM_OFF    10u
#define SEQ_DELAY_BL_OFF     20u
#define SEQ_DELAY_LCD_OFF    20u
#define SEQ_VERIFY_TIMEOUT   200u

/* Sequencing ADC verification thresholds (mV on ADC pin) */
#define SEQ_VERIFY_SCALER_MV 4000u
#define SEQ_VERIFY_LCD_MV    2800u
#define SEQ_VERIFY_BL_MV     9000u

/* PGOOD timeout (Rules 6.5) */
#define PGOOD_TIMEOUT_MS     5000u

/* ===== SUS_S3# (Rules 8) ===== */
#define SUS_S3_THRESHOLD_MS  500u
#define SUS_S3_COOLDOWN_MS   5000u
#define PWRBTN_PULSE_MS      150u

/* ===== Audio (Rules 9) ===== */
#define AUDIO_SDZ_DELAY_MS   10u
#define AUDIO_MUTE_DELAY_MS  10u

/* ===== UART protocol (Rules 4) ===== */
#define UART_INTERBYTE_TIMEOUT_MS  10u
#define UART_PACKET_TIMEOUT_MS     50u

#define PROTO_STX            0x02u
#define PROTO_ETX            0x03u

#define CMD_PING             0x01u
#define CMD_POWER_CTRL       0x02u
#define CMD_SET_BRIGHTNESS   0x03u
#define CMD_GET_STATUS       0x04u
#define CMD_RESET_FAULT      0x05u
#define CMD_RESET_BRIDGE     0x06u
#define CMD_SET_THRESHOLDS   0x07u
#define CMD_BOOTLOADER_ENTER 0x08u
#define CMD_CALIBRATE_OFFSET 0x09u
#define CMD_NACK             0xFFu

#define GET_STATUS_DATA_LEN  26u
#define PING_RESPONSE        0xAAu

/* ===== Domain bitmask (Rules 4.5) ===== */
#define DOM_SCALER           0x01u
#define DOM_LCD              0x02u
#define DOM_BACKLIGHT        0x04u
#define DOM_AUDIO            0x08u
#define DOM_ETH1             0x10u
#define DOM_ETH2             0x20u
#define DOM_TOUCH            0x40u

/* ===== Fault flags bitmask (Rules 7.2) ===== */
#define FAULT_SCALER         0x0001u
#define FAULT_LCD            0x0002u
#define FAULT_BACKLIGHT      0x0004u
#define FAULT_AUDIO          0x0008u
#define FAULT_ETH1           0x0010u
#define FAULT_ETH2           0x0020u
#define FAULT_TOUCH          0x0040u
#define FAULT_PGOOD_LOST     0x0080u
#define FAULT_AMP_FAULTZ     0x0100u
#define FAULT_V24_RANGE      0x0200u
#define FAULT_V12_RANGE      0x0400u
#define FAULT_V5_RANGE       0x0800u
#define FAULT_V3V3_RANGE     0x1000u
#define FAULT_SEQ_ABORT      0x2000u
#define FAULT_INTERNAL       0x4000u
#define FAULT_RESERVED       0x8000u

/* ===== Bridge reset ===== */
#define BRIDGE_RST_PULSE_MS  10u

/* ===== Flash calibration (Rules 11) ===== */
#define FLASH_CAL_ADDR       0x0800FC00u
#define FLASH_CAL_MAGIC      0x43414C49u  /* "CALI" */
#define FLASH_CAL_VERSION    1u
#define CURRENT_CHANNELS     5

/* ===== Bootloader (Rules 10) ===== */
#define SRAM_MAGIC_VALUE     0xDEADBEEFu
#define ROM_BOOTLOADER_ADDR  0x1FFF0000u

/* ===== Global systick (0.3) ===== */
extern volatile uint32_t systick_ms;

#endif /* CONFIG_H */
