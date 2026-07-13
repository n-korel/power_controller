# Shared helpers for backlight control over USB-UART.
# shellcheck shell=bash

_BL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_REPO_ROOT="$(cd "${_BL_DIR}/../.." && pwd)"

# shellcheck source=../uart/lib.sh
source "${_REPO_ROOT}/scripts/uart/lib.sh"

_bl_quiet=0

bl_log() {
  [[ "$_bl_quiet" -eq 1 ]] || log_info "$*"
}

bl_die() {
  log_fail "$*"
  exit 1
}

bl_percent_to_pwm() {
  local arg=$1
  local n
  if [[ "$arg" =~ ^([0-9]+)%?$ ]]; then
    n="${BASH_REMATCH[1]}"
    if (( n < 0 || n > 100 )); then
      bl_die "percent: 0…100 (got ${n})"
    fi
    echo $(( n * 10 ))
    return 0
  fi
  bl_die "invalid brightness format: ${arg} (expected 0…100 or 50%)"
}

bl_pwm_validate() {
  local pwm=$1
  if ! [[ "$pwm" =~ ^[0-9]+$ ]]; then
    bl_die "pwm must be an integer 0…1000"
  fi
  if (( pwm < 0 || pwm > 1000 )); then
    bl_die "pwm out of range 0…1000 (got ${pwm})"
  fi
}

bl_session_open() {
  require_tty
  uart_open
  if [[ "${BL_SKIP_PING:-0}" != 1 ]]; then
    local hex
    hex="$(cmd_ping)" || bl_die "MCU does not respond to PING (${UART_DEVICE})"
    expect_ping_aa "$hex" || bl_die "PING: expected status=0xAA"
    bl_log "PING OK on ${UART_DEVICE}"
  fi
}

bl_session_close() {
  test_cleanup
}

bl_ack_expect_ok() {
  local hex=$1 what=$2
  [[ -n "$hex" ]] || bl_die "no response: ${what}"
  expect_ack_status "$hex" 0 || bl_die "${what}: MCU returned status≠0x00"
}

bl_state_backlight_on() {
  local hex=$1
  python3 - "$hex" <<'PY'
import sys
raw = bytes.fromhex(sys.argv[1].replace(" ", ""))
if len(raw) < 27:
    sys.exit(2)
data = raw[3:25]
state = data[18]
sys.exit(0 if (state & 0x04) else 1)
PY
}

bl_parse_state() {
  local hex=$1
  parse_get_status_hex "$hex" 2>/dev/null || true
}
