#!/usr/bin/env bash
# POWER_Controller_BNT backlight control over USB-UART (115200 8N1).
#
# Examples:
#   UART_DEVICE=/dev/ttyACM0 ./bl.sh ping
#   ./bl.sh status
#   ./bl.sh on
#   ./bl.sh on --with-display    # SCALER+LCD, then BACKLIGHT (bench with display)
#   ./bl.sh off
#   ./bl.sh set 50               # 50% (pwm=500)
#   ./bl.sh set pwm 750
#   ./bl.sh preset mid           # dim | mid | max
#
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

usage() {
  cat <<'EOF'
Backlight control over UART (SET_BRIGHTNESS / POWER_CTRL commands).

Usage:
  bl.sh ping
  bl.sh status
  bl.sh on [--with-display]
  bl.sh off
  bl.sh set <0-100|[0-100]%|pwm <0-1000>>
  bl.sh preset <dim|mid|max>

Environment:
  UART_DEVICE=/dev/ttyUSB0   serial port
  BL_SKIP_PING=1             skip PING when opening session
  BL_QUIET=1                 less logging

Brightness: pwm 0…1000 (= 0…100%, 0.1% step). set without "pwm" uses percent.
EOF
}

trap bl_session_close EXIT

with_display=0
_bl_quiet="${BL_QUIET:-0}"

while [[ $# -gt 0 && "$1" == --* ]]; do
  case "$1" in
    --with-display) with_display=1; shift ;;
    -q|--quiet) _bl_quiet=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) bl_die "unknown flag: $1" ;;
  esac
done

cmd=${1:-}
shift || true

case "${cmd:-}" in
  ping)
    bl_session_open
    log_pass "MCU link OK"
    ;;
  status)
    bl_session_open
    hex="$(cmd_get_status)" || bl_die "GET_STATUS: no response"
    bl_parse_state "$hex"
    if bl_state_backlight_on "$hex"; then
      bl_log "BACKLIGHT: ON"
    else
      bl_log "BACKLIGHT: OFF"
    fi
    ;;
  on)
    bl_session_open
    if [[ "$with_display" -eq 1 ]]; then
      # shellcheck source=../../Tests_UART_All/lib.sh
      source "${_REPO_ROOT}/Tests_UART_All/lib.sh"
      hex="$(periph_display_backlight_on)" || bl_die "failed to enable SCALER+LCD+BACKLIGHT"
      bl_parse_state "$hex"
    else
      hex="$(cmd_power_ctrl 0x0004 0x0004)" || bl_die "POWER_CTRL BACKLIGHT ON: no response"
      bl_ack_expect_ok "$hex" "BACKLIGHT ON"
      bl_log "BACKLIGHT ON (SCALER+LCD required; on failure try: bl.sh on --with-display)"
    fi
    log_pass "backlight on"
    ;;
  off)
    bl_session_open
    hex="$(cmd_power_ctrl 0x0004 0x0000)" || bl_die "POWER_CTRL BACKLIGHT OFF: no response"
    bl_ack_expect_ok "$hex" "BACKLIGHT OFF"
    log_pass "backlight off (SCALER/LCD unchanged)"
    ;;
  set)
    [[ $# -ge 1 ]] || { usage; exit 1; }
    bl_session_open
    pwm=
    if [[ "${1:-}" == pwm ]]; then
      shift
      [[ $# -ge 1 ]] || bl_die "specify pwm 0…1000"
      pwm=$1
      bl_pwm_validate "$pwm"
    else
      pwm="$(bl_percent_to_pwm "$1")"
    fi
    hex="$(cmd_set_brightness "$pwm")" || bl_die "SET_BRIGHTNESS: no response"
    bl_ack_expect_ok "$hex" "SET_BRIGHTNESS pwm=${pwm}"
    if bl_state_backlight_on "$(cmd_get_status 2>/dev/null || true)" 2>/dev/null; then
      log_pass "brightness pwm=${pwm} ($(( pwm / 10 ))%)"
    else
      log_pass "brightness pwm=${pwm} stored (BACKLIGHT OFF — PWM applies after BL on)"
    fi
    ;;
  preset)
    [[ $# -ge 1 ]] || { usage; exit 1; }
    case "$1" in
      dim|min|0)   preset_pwm=0 ;;
      mid|half|50) preset_pwm=500 ;;
      max|full|100) preset_pwm=1000 ;;
      *) bl_die "preset: dim | mid | max" ;;
    esac
    exec "$0" set pwm "$preset_pwm" "${@:2}"
    ;;
  ""|help|-h|--help)
    usage
    exit 0
    ;;
  *)
    bl_die "unknown command: ${cmd} (bl.sh --help)"
    ;;
esac
