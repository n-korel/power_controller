# Настройки натурных UART-тестов (пустая плата + USB-UART 3.3 В).
# Переопределение: UART_DEVICE=/dev/ttyACM0 ./run_all_bare_board.sh

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

# Длины кадров ответа: ACK=6, GET_STATUS=3+26+1+1=31 (не 32)
GET_STATUS_FRAME_LEN=31
ACK_FRAME_LEN=6
NO_RESPONSE_TIMEOUT_SEC=0.25

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

# А.2: токи при нулевой нагрузке (мА)
TELEMETRY_I_MIN_MA=-200
TELEMETRY_I_MAX_MA=200
