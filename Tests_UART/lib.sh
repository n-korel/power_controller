# Общие функции UART-тестов POWER_Controller.
# shellcheck shell=bash

_TESTS_UART_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${_TESTS_UART_DIR}/config.sh"

_fd_open=0

_red()   { printf '\033[31m'; }
_green() { printf '\033[32m'; }
_yellow(){ printf '\033[33m'; }
_nc()    { printf '\033[0m'; }

log_info()  { printf '%s[INFO]%s %s\n' "$(_yellow)" "$(_nc)" "$*"; }
log_pass()  { printf '%s[PASS]%s %s\n' "$(_green)" "$(_nc)" "$*"; }
log_fail()  { printf '%s[FAIL]%s %s\n' "$(_red)" "$(_nc)" "$*"; }
log_skip()  { printf '%s[SKIP]%s %s\n' "$(_yellow)" "$(_nc)" "$*"; }

die() { log_fail "$*"; exit 1; }

require_tty() {
  [[ -e "$UART_DEVICE" ]] || die "Serial port not found: ${UART_DEVICE} (set UART_DEVICE=...)"
  command -v xxd >/dev/null 2>&1 || die "xxd is required"
}

# Сборка кадра [STX][CMD][LEN][DATA][CRC][ETX], CRC-8/ATM как в Protocol/uart_protocol.c
uart_build_frame() {
  local cmd=$1
  shift
  python3 - "$cmd" "$@" <<'PY'
import sys
crc8_table = bytes([
    0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,0x24,0x23,0x2A,0x2D,
    0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,
    0xE0,0xE7,0xEE,0xE9,0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,
    0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,0xB4,0xB3,0xBA,0xBD,
    0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,
    0xB7,0xB0,0xB9,0xBE,0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,
    0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,0x03,0x04,0x0D,0x0A,
    0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,
    0x89,0x8E,0x87,0x80,0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,
    0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,0xDD,0xDA,0xD3,0xD4,
    0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,
    0x19,0x1E,0x17,0x10,0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,
    0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,0x6A,0x6D,0x64,0x63,
    0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,
    0xAE,0xA9,0xA0,0xA7,0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,
    0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,0xFA,0xFD,0xF4,0xF3,
])
def crc8_calc(data: bytes) -> int:
    crc = 0
    for b in data:
        crc = crc8_table[crc ^ b]
    return crc
cmd = int(sys.argv[1], 0)
data = bytes(int(x, 0) for x in sys.argv[2:])
body = bytes([cmd, len(data)]) + data
frame = bytes([0x02]) + body + bytes([crc8_calc(body), 0x03])
sys.stdout.buffer.write(frame)
PY
}

uart_stty() {
  stty -F "$UART_DEVICE" "$UART_BAUD" cs8 -cstopb -parenb \
    -icanon -echo min 0 time 0
}

uart_flush() {
  timeout 0.15 dd if="$UART_DEVICE" bs=256 count=1 status=none 2>/dev/null || true
}

# Сброс RX на открытом fd (между командами в одной сессии)
uart_drain_fd() {
  [[ "$_fd_open" -eq 1 ]] || return 0
  python3 3<&3 <<'PY' || true
import os, select, time
fd = 3
end = time.time() + 0.2
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.05)
    if not r:
        break
    chunk = os.read(fd, 4096)
    if not chunk:
        break
PY
}

uart_open() {
  require_tty
  uart_stty
  uart_flush
  exec 3<>"$UART_DEVICE"
  _fd_open=1
  uart_drain_fd
  if [[ "${UART_POST_OPEN_DELAY_SEC:-0}" != 0 ]]; then
    sleep "${UART_POST_OPEN_DELAY_SEC}"
  fi
}

# Preflight после cold boot: повторять PING, пока MCU не ответит.
uart_wait_mcu_ready() {
  local attempt=1
  local max="${BOOT_PING_RETRIES:-60}"
  local interval="${BOOT_PING_INTERVAL_SEC:-0.5}"
  local hex=""

  log_info "Waiting for MCU UART (up to ${max} PING, ${interval}s interval)..."
  require_tty
  uart_stty
  exec 3<>"$UART_DEVICE"
  _fd_open=1
  if [[ "${UART_POST_OPEN_DELAY_SEC:-0}" != 0 ]]; then
    sleep "${UART_POST_OPEN_DELAY_SEC}"
  fi
  uart_drain_fd

  while (( attempt <= max )); do
    if (( attempt == 1 || attempt % 5 == 0 )); then
      log_info "MCU UART probe attempt ${attempt}/${max}"
    fi
    hex=""
    if hex="$(cmd_ping_probe_ready 2>/dev/null || true)" && [[ -n "$hex" ]] && expect_ping_aa "$hex"; then
      log_pass "MCU UART ready (attempt ${attempt}/${max})"
      uart_close
      return 0
    fi
    if (( attempt < max )); then
      sleep "$interval"
    fi
    attempt=$((attempt + 1))
  done
  uart_close
  log_fail "MCU UART not ready after ${max} PING attempts"
  return 1
}

uart_close() {
  if [[ "$_fd_open" -eq 1 ]]; then
    exec 3<&-
    _fd_open=0
  fi
}

uart_tx_frame() {
  local cmd=$1
  shift
  # Бинарный кадр (STX=0x02, нули в DATA) нельзя класть в $(...) — обрезается по \0.
  uart_build_frame "$cmd" "$@" >&3
}

uart_tx_raw() {
  # shellcheck disable=SC2064
  printf '%b' "$1" >&3
}

# Читает ровно $1 байт за $2 с (через select, без блокирующего dd)
uart_rx() {
  local nbytes=$1
  local tsec=$2
  python3 - "$nbytes" "$tsec" 3<&3 <<'PY'
import binascii, os, select, sys, time

need = int(sys.argv[1])
limit = float(sys.argv[2])
fd = 3
buf = b""
end = time.time() + limit
while len(buf) < need and time.time() < end:
    wait = max(0.0, end - time.time())
    r, _, _ = select.select([fd], [], [], min(wait, 0.1))
    if not r:
        continue
    chunk = os.read(fd, need - len(buf))
    if chunk:
        buf += chunk
if len(buf) != need:
    raise SystemExit(1)
sys.stdout.write(binascii.hexlify(buf).decode())
PY
}

hex_to_bytes() {
  python3 - "$1" <<'PY'
import sys
h = sys.argv[1].replace(' ', '').strip()
sys.stdout.buffer.write(bytes.fromhex(h))
PY
}

# Отправить готовый hex-кадр (без пробелов)
uart_send_hex() {
  local hex=$1
  hex_to_bytes "$hex" >&3
}

# --- Высокоуровневые команды ---

cmd_ping() {
  uart_drain_fd
  uart_tx_frame 0x01
  sleep 0.15
  uart_rx "$ACK_FRAME_LEN" "$ACK_TIMEOUT_SEC"
}

# Пробный PING для cold boot: читаем поток и ищем ACK в буфере.
cmd_ping_probe_ready() {
  local timeout_sec="${1:-1.2}"
  uart_drain_fd
  uart_tx_frame 0x01
  python3 - "$timeout_sec" 3<&3 <<'PY'
import os
import select
import sys
import time

limit = float(sys.argv[1])
fd = 3
buf = b""
end = time.time() + limit

while time.time() < end:
    wait = max(0.0, end - time.time())
    r, _, _ = select.select([fd], [], [], min(wait, 0.1))
    if not r:
        continue
    chunk = os.read(fd, 256)
    if not chunk:
        continue
    buf += chunk
    if len(buf) > 512:
        buf = buf[-512:]

for i in range(0, max(0, len(buf) - 5)):
    frame = buf[i:i+6]
    if len(frame) == 6 and frame[0] == 0x02 and frame[1] == 0x01 and frame[2] == 0x01 and frame[3] == 0xAA and frame[5] == 0x03:
        sys.stdout.write(frame.hex())
        sys.exit(0)
sys.exit(1)
PY
}

cmd_get_status() {
  local attempt hex
  for attempt in 1 2 3; do
    uart_drain_fd
    uart_tx_frame 0x04
    sleep "$GET_STATUS_TX_DELAY_SEC"
    if hex="$(uart_rx "$GET_STATUS_FRAME_LEN" "$GET_STATUS_TIMEOUT_SEC" 2>/dev/null)" \
      && validate_get_status_hex "$hex" \
      && validate_frame_crc "$hex"; then
      printf '%s' "$hex"
      return 0
    fi
    sleep 0.1
  done
  return 1
}

cmd_reset_fault() {
  uart_drain_fd
  uart_tx_frame 0x05
  sleep 0.15
  uart_rx "$ACK_FRAME_LEN" "$ACK_TIMEOUT_SEC"
}

cmd_reset_bridge() {
  uart_drain_fd
  uart_tx_frame 0x06
  sleep 0.15
  uart_rx "$ACK_FRAME_LEN" "$ACK_TIMEOUT_SEC"
}

cmd_calibrate_offset() {
  uart_drain_fd
  uart_tx_frame 0x09
  sleep 0.05
  uart_rx "$ACK_FRAME_LEN" "${CALIBRATE_OFFSET_TIMEOUT_SEC:-3.0}"
}

cmd_power_ctrl() {
  local mask=$1 value=$2
  local ml=$((mask & 0xff)) mh=$(((mask >> 8) & 0xff))
  local vl=$((value & 0xff)) vh=$(((value >> 8) & 0xff))
  local attempt hex
  for attempt in 1 2 3; do
    uart_drain_fd
    uart_tx_frame 0x02 "$ml" "$mh" "$vl" "$vh"
    sleep "$POWER_CTRL_TX_DELAY_SEC"
    if hex="$(uart_rx "$ACK_FRAME_LEN" "$ACK_TIMEOUT_SEC" 2>/dev/null)" \
      && validate_frame_crc "$hex" \
      && python3 - "$hex" <<'PY'
import sys
b = bytes.fromhex(sys.argv[1].replace(' ', ''))
ok = len(b) == 6 and b[0] == 0x02 and b[1] == 0x02 and b[2] == 0x01 and b[-1] == 0x03
sys.exit(0 if ok else 1)
PY
    then
      printf '%s' "$hex"
      return 0
    fi
    sleep 0.1
  done
  return 1
}

# Кадр с произвольным LEN (для негативных тестов протокола)
uart_tx_frame_len() {
  local cmd=$1 len=$2
  shift 2
  python3 - "$cmd" "$len" "$@" <<'PY' >&3
import sys
crc8_table = bytes([
    0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,0x24,0x23,0x2A,0x2D,
    0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,
    0xE0,0xE7,0xEE,0xE9,0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,
    0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,0xB4,0xB3,0xBA,0xBD,
    0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,
    0xB7,0xB0,0xB9,0xBE,0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,
    0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,0x03,0x04,0x0D,0x0A,
    0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,
    0x89,0x8E,0x87,0x80,0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,
    0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,0xDD,0xDA,0xD3,0xD4,
    0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,
    0x19,0x1E,0x17,0x10,0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,
    0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,0x6A,0x6D,0x64,0x63,
    0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,
    0xAE,0xA9,0xA0,0xA7,0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,
    0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,0xFA,0xFD,0xF4,0xF3,
])
def crc8_calc(data: bytes) -> int:
    crc = 0
    for b in data:
        crc = crc8_table[crc ^ b]
    return crc
cmd = int(sys.argv[1], 0)
ln = int(sys.argv[2], 0)
data = bytes(int(x, 0) for x in sys.argv[3:])
body = bytes([cmd, ln]) + data
frame = bytes([0x02]) + body + bytes([crc8_calc(body), 0x03])
sys.stdout.buffer.write(frame)
PY
}

cmd_power_ctrl_len() {
  local len=$1
  shift
  local attempt hex
  for attempt in 1 2 3; do
    uart_drain_fd
    uart_tx_frame_len 0x02 "$len" "$@"
    sleep "$POWER_CTRL_TX_DELAY_SEC"
    if hex="$(uart_rx "$ACK_FRAME_LEN" "$ACK_TIMEOUT_SEC" 2>/dev/null)"; then
      printf '%s' "$hex"
      return 0
    fi
    sleep 0.1
  done
  return 1
}

cmd_set_brightness() {
  local pwm=$1
  local lo=$((pwm & 0xff)) hi=$(((pwm >> 8) & 0xff))
  uart_drain_fd
  uart_tx_frame 0x03 "$lo" "$hi"
  sleep 0.15
  uart_rx "$ACK_FRAME_LEN" "$ACK_TIMEOUT_SEC"
}

cmd_set_brightness_len() {
  local len=$1
  shift
  uart_drain_fd
  uart_tx_frame_len 0x03 "$len" "$@"
  sleep 0.15
  uart_rx "$ACK_FRAME_LEN" "$ACK_TIMEOUT_SEC"
}

# SET_THRESHOLDS (CMD=0x07): mask u16 LE + поля по битам mask (см. Protocol/uart_protocol.c)
cmd_set_thresholds() {
  local mask=$1
  shift
  local ml=$((mask & 0xff)) mh=$(((mask >> 8) & 0xff))
  local attempt hex
  for attempt in 1 2 3; do
    uart_drain_fd
    uart_tx_frame 0x07 "$ml" "$mh" "$@"
    sleep "${SET_THRESH_TX_DELAY_SEC:-0.2}"
    if hex="$(uart_rx "$ACK_FRAME_LEN" "$ACK_TIMEOUT_SEC" 2>/dev/null)"; then
      printf '%s' "$hex"
      return 0
    fi
    sleep 0.1
  done
  return 1
}

# Узкий V12 → FAULT_V12_RANGE (0x0400); восстановление порогов из config.sh
fault_trigger_v12_range() {
  local tmin="${FAULT_V12_TRAP_MIN_MV:-13000}"
  local tmax="${FAULT_V12_TRAP_MAX_MV:-14000}"
  local lo_min=$((tmin & 0xff)) hi_min=$(((tmin >> 8) & 0xff))
  local lo_max=$((tmax & 0xff)) hi_max=$(((tmax >> 8) & 0xff))
  cmd_set_thresholds 0x0002 "$lo_min" "$hi_min" "$lo_max" "$hi_max"
}

fault_restore_v12_defaults() {
  local tmin="${THRESH_V12_MIN_MV:-10000}"
  local tmax="${THRESH_V12_MAX_MV:-13000}"
  cmd_set_thresholds 0x0002 $((tmin & 0xff)) $(((tmin >> 8) & 0xff)) \
    $((tmax & 0xff)) $(((tmax >> 8) & 0xff))
}

fault_trigger_v5_range() {
  local tmin="${FAULT_V5_TRAP_MIN_MV:-5600}"
  local tmax="${FAULT_V5_TRAP_MAX_MV:-5800}"
  cmd_set_thresholds 0x0004 $((tmin & 0xff)) $(((tmin >> 8) & 0xff)) \
    $((tmax & 0xff)) $(((tmax >> 8) & 0xff))
}

fault_restore_v5_defaults() {
  local tmin="${THRESH_V5_MIN_MV:-4500}"
  local tmax="${THRESH_V5_MAX_MV:-5500}"
  cmd_set_thresholds 0x0004 $((tmin & 0xff)) $(((tmin >> 8) & 0xff)) \
    $((tmax & 0xff)) $(((tmax >> 8) & 0xff))
}

fault_trigger_v3v3_range() {
  local tmin="${FAULT_V3V3_TRAP_MIN_MV:-3600}"
  local tmax="${FAULT_V3V3_TRAP_MAX_MV:-3700}"
  cmd_set_thresholds 0x0008 $((tmin & 0xff)) $(((tmin >> 8) & 0xff)) \
    $((tmax & 0xff)) $(((tmax >> 8) & 0xff))
}

fault_restore_v3v3_defaults() {
  local tmin="${THRESH_V3V3_MIN_MV:-3000}"
  local tmax="${THRESH_V3V3_MAX_MV:-3600}"
  cmd_set_thresholds 0x0008 $((tmin & 0xff)) $(((tmin >> 8) & 0xff)) \
    $((tmax & 0xff)) $(((tmax >> 8) & 0xff))
}

# fault_manager.c: проверка шин только при state != 0 — включаем TOUCH без секвенса
fault_enable_voltage_monitor() {
  local hex
  hex="$(cmd_power_ctrl 0x0040 0x0040)" || return 1
  expect_ack_status "$hex" 0 || return 1
  sleep "${FAULT_MONITOR_ARM_SEC:-0.15}"
}

fault_disable_voltage_monitor() {
  cmd_power_ctrl 0x0040 0x0000 >/dev/null 2>&1 || true
}

# Ждём latch FAULT_V12_RANGE (0x0400), опрос GET_STATUS
fault_wait_flags() {
  local want=$1
  local tries="${2:-30}"
  local i hex
  for ((i = 1; i <= tries; i++)); do
    sleep "${FAULT_POLL_INTERVAL_SEC:-0.1}"
    hex="$(cmd_get_status)" || continue
    if expect_fault_flags "$hex" "has:${want}"; then
      printf '%s' "$hex"
      return 0
    fi
  done
  return 1
}

# Валидный кадр GET_STATUS: 42 байта, CMD=0x04, LEN=0x25 (37 DATA), ETX=0x03
validate_get_status_hex() {
  local hex=$1
  python3 - "$hex" <<'PY'
import sys
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
ok = (
    len(raw) == 42
    and raw[0] == 0x02
    and raw[1] == 0x04
    and raw[2] == 0x25
    and raw[-1] == 0x03
)
sys.exit(0 if ok else 1)
PY
}

# CRC-8/ATM по [CMD][LEN][DATA] — как uart_protocol.c
validate_frame_crc() {
  local hex=$1
  python3 - "$hex" <<'PY'
import sys
crc8_table = bytes([
    0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,0x24,0x23,0x2A,0x2D,
    0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,
    0xE0,0xE7,0xEE,0xE9,0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,
    0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,0xB4,0xB3,0xBA,0xBD,
    0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,
    0xB7,0xB0,0xB9,0xBE,0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,
    0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,0x03,0x04,0x0D,0x0A,
    0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,
    0x89,0x8E,0x87,0x80,0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,
    0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,0xDD,0xDA,0xD3,0xD4,
    0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,
    0x19,0x1E,0x17,0x10,0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,
    0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,0x6A,0x6D,0x64,0x63,
    0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,
    0xAE,0xA9,0xA0,0xA7,0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,
    0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,0xFA,0xFD,0xF4,0xF3,
])
def crc8_calc(data: bytes) -> int:
    crc = 0
    for b in data:
        crc = crc8_table[crc ^ b]
    return crc
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
if len(raw) < 5 or raw[0] != 0x02 or raw[-1] != 0x03:
    sys.exit(1)
cmd, ln = raw[1], raw[2]
data = raw[3:3 + ln]
crc_rx = raw[3 + ln]
body = bytes([cmd, ln]) + data
sys.exit(0 if crc8_calc(body) == crc_rx else 1)
PY
}

# Парсинг GET_STATUS (37 байт DATA) — вывод key=value
parse_get_status_hex() {
  local hex=$1
  python3 - "$hex" <<'PY'
import struct, sys
h = sys.argv[1].replace(' ', '').strip().lower()
raw = bytes.fromhex(h)
if len(raw) != 42:
    print(f'error=bad_frame_len len={len(raw)} expected=42', file=sys.stderr)
    sys.exit(2)
if raw[0] != 0x02 or raw[1] != 0x04 or raw[2] != 0x25 or raw[-1] != 0x03:
    print('error=bad_header_or_etx', file=sys.stderr)
    sys.exit(2)
data = raw[3:40]
off = 0
for n in ['v24','v12','v5','v3v3']:
    v = struct.unpack_from('<H', data, off)[0]
    print(f'{n}={v}')
    off += 2
for n in ['i_lcd','i_backlight','i_scaler','i_audio_l','i_audio_r']:
    v = struct.unpack_from('<h', data, off)[0]
    print(f'{n}={v}')
    off += 2
for n in ['temp0','temp1']:
    v = struct.unpack_from('<h', data, off)[0]
    print(f'{n}={v}')
    off += 2
state = data[22]
fault = struct.unpack_from('<H', data, 23)[0]
inputs = data[25]
dseq = data[26]
last_mask_lo = data[27]
last_value_lo = data[28]
rf = int.from_bytes(data[29:33], 'little')
bc = int.from_bytes(data[33:37], 'little')
print(f'state=0x{state:02x}')
print(f'fault_flags=0x{fault:04x}')
print(f'inputs=0x{inputs:02x}')
print(f'dseq={dseq}')
print(f'last_power_ctrl_mask_lo=0x{last_mask_lo:02x}')
print(f'last_power_ctrl_value_lo=0x{last_value_lo:02x}')
print(f'reset_flags_raw=0x{rf:08x}')
print(f'boot_counter={bc}')
print(f'pgood={(inputs >> 6) & 1}')
PY
}

expect_ack_status() {
  local hex=$1 expected=$2
  local got
  got="$(hex_to_bytes "$hex" | od -An -tx1 | tr -d ' \n' | sed 's/^//' )"
  # bytes: STX CMD LEN STATUS CRC ETX -> status at index 3*2+... simpler:
  local status_byte
  status_byte="$(python3 - "$hex" <<'PY'
import sys
b = bytes.fromhex(sys.argv[1].replace(' ',''))
print(b[3] if len(b) >= 4 else 255)
PY
)"
  [[ "$status_byte" == "$expected" ]]
}

expect_ping_aa() {
  local hex=$1
  python3 - "$hex" <<'PY'
import sys
b = bytes.fromhex(sys.argv[1].replace(' ',''))
ok = len(b) >= 6 and b[0]==2 and b[1]==1 and b[3]==0xAA
sys.exit(0 if ok else 1)
PY
}

expect_get_status_clean() {
  local hex=$1
  local parsed
  parsed="$(parse_get_status_hex "$hex")" || return 1
  local state fault
  state="$(echo "$parsed" | awk -F= '/^state=/{print $2}')"
  fault="$(echo "$parsed" | awk -F= '/^fault_flags=/{print $2}')"
  [[ "$state" == "0x00" && "$fault" == "0x0000" ]]
}

ensure_clean_state() {
  local attempts=${1:-8}
  local status_hex ack_hex
  local i

  for ((i = 1; i <= attempts; i++)); do
    cmd_reset_fault >/dev/null 2>&1 || true
    ack_hex="$(cmd_power_ctrl 0x007f 0x0000 2>/dev/null || true)"
    if [[ -n "$ack_hex" ]] && ! expect_ack_status "$ack_hex" 0; then
      sleep 0.1
      continue
    fi
    sleep 0.2
    status_hex="$(cmd_get_status 2>/dev/null || true)"
    if [[ -n "$status_hex" ]] && expect_get_status_clean "$status_hex"; then
      return 0
    fi
    sleep 0.15
  done

  return 1
}

# fault_flags: exact match (hex 0x0400) или маска (второй аргумент — биты, которые должны быть set)
expect_fault_flags() {
  local hex=$1 expected=$2
  python3 - "$hex" "$expected" <<'PY'
import sys
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
exp = sys.argv[2].lower()
if len(raw) != 42:
    sys.exit(1)
fault = raw[26] | (raw[27] << 8)
if exp.startswith('has:'):
    mask = int(exp[4:], 16)
    sys.exit(0 if (fault & mask) == mask else 1)
want = int(exp, 16)
sys.exit(0 if fault == want else 1)
PY
}

expect_state_bits() {
  local hex=$1 set_mask=$2 clear_mask=${3:-0}
  python3 - "$hex" "$set_mask" "$clear_mask" <<'PY'
import sys
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
if len(raw) != 42:
    sys.exit(1)
state = raw[25]
set_m = int(sys.argv[2], 0)
clr_m = int(sys.argv[3], 0)
ok = ((state & set_m) == set_m) and ((state & clr_m) == 0)
sys.exit(0 if ok else 1)
PY
}

expect_state_unchanged() {
  local hex_before=$1 hex_after=$2
  python3 - "$hex_before" "$hex_after" <<'PY'
import sys
def state(h):
    raw = bytes.fromhex(h.replace(' ', ''))
    return raw[25] if len(raw) == 42 else None
a, b = state(sys.argv[1]), state(sys.argv[2])
sys.exit(0 if a is not None and a == b else 1)
PY
}

expect_fault_reserved_clear() {
  local hex=$1
  python3 - "$hex" <<'PY'
import sys
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
if len(raw) != 42:
    sys.exit(1)
fault = raw[26] | (raw[27] << 8)
sys.exit(0 if (fault & 0x8000) == 0 else 1)
PY
}

test_cleanup() { uart_close; }
