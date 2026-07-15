# UART serial settings for bench tools (USB-UART 3.3 V on UART0).
# Override: UART_DEVICE=/dev/ttyACM0 make bl-ping

UART_DEVICE="${UART_DEVICE:-/dev/ttyUSB0}"
UART_BAUD=115200

# Preflight: ждём первый успешный PING после cold boot (вместо фиксированного sleep).
BOOT_PING_RETRIES="${BOOT_PING_RETRIES:-60}"
BOOT_PING_INTERVAL_SEC="${BOOT_PING_INTERVAL_SEC:-0.5}"

# Пауза после exec 3<> (USB-UART может дёрнуть DTR → reset MCU).
UART_POST_OPEN_DELAY_SEC="${UART_POST_OPEN_DELAY_SEC:-0.5}"

# Таймауты чтения (секунды)
ACK_TIMEOUT_SEC=0.5
GET_STATUS_TIMEOUT_SEC=1.0
GET_STATUS_TX_DELAY_SEC=0.35
POWER_CTRL_TX_DELAY_SEC=0.25

# Длины кадров ответа: ACK=6, GET_STATUS=3+22+1+1=27, GET_VERSION=3+13+1+1=18
GET_STATUS_FRAME_LEN=27
GET_VERSION_FRAME_LEN=18
ACK_FRAME_LEN=6

# OTA dump (scripts/uart/ota_dump.sh): 64 KiB starting at flash base
OTA_DUMP_SIZE="${OTA_DUMP_SIZE:-65536}"

# K.1: число опросов GET_STATUS и пауза между ними (сек)
STRESS_GET_STATUS_COUNT=20
STRESS_GET_STATUS_INTERVAL_SEC=0.05

# C.18: пауза после неполного кадра (мс), должна быть > UART_PACKET_TIMEOUT_MS (50)
PACKET_TIMEOUT_PAUSE_MS=60

# C.19: пауза между байтами «битого» кадра (мс), > UART_INTERBYTE_TIMEOUT_MS (10)
INTERBYTE_GAP_MS=15

# Пороги по умолчанию (Config/config.h, мВ) — для восстановления после fault-тестов
THRESH_V12_MIN_MV=10000
THRESH_V12_MAX_MV=13000
THRESH_V5_MIN_MV=4500
THRESH_V5_MAX_MV=5500
THRESH_V3V3_MIN_MV=3000
THRESH_V3V3_MAX_MV=3600
THRESH_I_LCD_MAX_MA=2000
THRESH_I_BL_MAX_MA=3000
THRESH_I_SCALER_MAX_MA=1500
THRESH_I_AUDIO_LR_MAX_MA=800

# Ф.1: узкий диапазон V12, чтобы реальная шина оказалась «вне» порога
FAULT_V12_TRAP_MIN_MV=13000
FAULT_V12_TRAP_MAX_MV=14000
# V5 / V3V3 (типичная плата ~5.0 В / ~3.3 В — ловушка выше реального)
FAULT_V5_TRAP_MIN_MV=5600
FAULT_V5_TRAP_MAX_MV=5800
FAULT_V3V3_TRAP_MIN_MV=3600
FAULT_V3V3_TRAP_MAX_MV=3700

SET_THRESH_TX_DELAY_SEC=0.2
CALIBRATE_OFFSET_TIMEOUT_SEC=3.0
# FAULT_CONFIRM_COUNT=5 в прошивке; монитор шин только при state!=0
FAULT_MONITOR_ARM_SEC=0.15
FAULT_POLL_INTERVAL_SEC=0.1
FAULT_WAIT_TRIES=30

# И.1: опрос без reset MCU (пауза > IWDG timeout ~1 с не нужна между быстрыми GET_STATUS)
IWDG_STRESS_COUNT=20
IWDG_STRESS_INTERVAL_SEC=0.1

# OTA (scripts/uart/ota_flash.sh)
OTA_MAX_RETRIES="${OTA_MAX_RETRIES:-3}"
OTA_RESET_DELAY_SEC="${OTA_RESET_DELAY_SEC:-0.5}"
OTA_HARDWARE_RESET_DELAY_SEC="${OTA_HARDWARE_RESET_DELAY_SEC:-0.5}"
OTA_FLASH_ADDR="${OTA_FLASH_ADDR:-0x08000000}"
OTA_STM32FLASH="${OTA_STM32FLASH:-stm32flash}"
# Optional: shell command for IC17 BOOT0+NRST (Q7-side); unset = log and skip
OTA_IC17_RECOVERY_CMD="${OTA_IC17_RECOVERY_CMD:-}"
OTA_VERIFY_GET_STATUS="${OTA_VERIFY_GET_STATUS:-1}"
# Optional backup before write (same ROM session): OTA_BACKUP=1
OTA_BACKUP="${OTA_BACKUP:-0}"
# Empty → mktemp. Keep file after OTA for rollback if needed.
OTA_BACKUP_PATH="${OTA_BACKUP_PATH:-}"

# А.2: токи при нулевой нагрузке (мА)
TELEMETRY_I_MIN_MA=-200
TELEMETRY_I_MAX_MA=200
