# Настройки натурных UART-тестов (пустая плата + USB-UART 3.3 В).
# Переопределение: UART_DEVICE=/dev/ttyACM0 ./run_all_bare_board.sh

UART_DEVICE="${UART_DEVICE:-/dev/ttyUSB0}"
UART_BAUD=115200

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
