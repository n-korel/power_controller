# BNT_TESTS lib — pure BusyBox ash + socat (no python/bash).
# shellcheck shell=sh

# Caller must set SCRIPT_DIR to this directory before sourcing.
_BNT_DIR="${SCRIPT_DIR:-}"
if [ -z "$_BNT_DIR" ] || [ ! -f "$_BNT_DIR/config.sh" ]; then
  _BNT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
fi
# shellcheck source=config.sh
. "$_BNT_DIR/config.sh"

# CRC-8/ATM table (256 bytes as hex)
_CRC8_TBL="00070e091c1b1215383f363124232a2d70777e796c6b6265484f464154535a5de0e7eee9fcfbf2f5d8dfd6d1c4c3cacd90979e998c8b8285a8afa6a1b4b3babdc7c0c9cedbdcd5d2fff8f1f6e3e4edeab7b0b9beabaca5a28f88818693949d9a2720292e3b3c35321f18111603040d0a5750595e4b4c45426f68616673747d7a898e878095929b9cb1b6bfb8adaaa3a4f9fef7f0e5e2ebecc1c6cfc8dddad3d4696e676075727b7c51565f584d4a4344191e171005020b0c21262f283d3a33344e49404752555c5b7671787f6a6d64633e39303722252c2b0601080f1a1d1413aea9a0a7b2b5bcbb9691989f8a8d8483ded9d0d7c2c5cccbe6e1e8effafdf4f3"

_red()   { printf '\033[31m'; }
_green() { printf '\033[32m'; }
_yellow(){ printf '\033[33m'; }
_nc()    { printf '\033[0m'; }

log_info()  { printf '%s[INFO]%s %s\n' "$(_yellow)" "$(_nc)" "$*"; }
log_pass()  { printf '%s[PASS]%s %s\n' "$(_green)" "$(_nc)" "$*"; }
log_fail()  { printf '%s[FAIL]%s %s\n' "$(_red)" "$(_nc)" "$*"; }
log_skip()  { printf '%s[SKIP]%s %s\n' "$(_yellow)" "$(_nc)" "$*"; }

die() { log_fail "$*"; exit 1; }

_to_dec() {
  printf '%d' "$1" 2>/dev/null || printf '%d' "0$1"
}

hex_norm() {
  printf '%s' "$1" | tr -d ' \n\r\t' | tr 'A-F' 'a-f'
}

hex_byte() {
  # $1=hex $2=byte index → decimal
  _h="$(hex_norm "$1")"
  _i="$2"
  _pair="$(printf '%s' "$_h" | cut -c "$((_i * 2 + 1))"-"$((_i * 2 + 2))")"
  [ ${#_pair} -eq 2 ] || { printf '255'; return 1; }
  printf '%d' "0x$_pair"
}

hex_u16le() {
  _lo="$(hex_byte "$1" "$2")"
  _hi="$(hex_byte "$1" "$(($2 + 1))")"
  printf '%d' "$((_lo + _hi * 256))"
}

hex_i16le() {
  _v="$(hex_u16le "$1" "$2")"
  if [ "$_v" -ge 32768 ]; then
    printf '%d' "$((_v - 65536))"
  else
    printf '%d' "$_v"
  fi
}

_crc8_lookup() {
  # $1 = index 0..255 → table byte decimal
  _pair="$(printf '%s' "$_CRC8_TBL" | cut -c "$(($1 * 2 + 1))"-"$(($1 * 2 + 2))")"
  printf '%d' "0x$_pair"
}

crc8_calc() {
  # args: decimal bytes of body [CMD LEN DATA...]
  _crc=0
  for _b in "$@"; do
    _b="$(_to_dec "$_b")"
    _idx=$(( (_crc ^ _b) & 255 ))
    _crc="$(_crc8_lookup "$_idx")"
  done
  printf '%d' "$_crc"
}

# Emit one byte 0..255 as binary (BusyBox-safe).
_emit_byte() {
  awk -v b="$1" 'BEGIN { printf "%c", and(b,255) }' 2>/dev/null || \
  awk -v b="$1" 'BEGIN { printf "%c", int(b)%256 }'
}

# Emit binary frame to stdout (never store in shell var — NUL-safe).
# Usage: uart_emit_frame CMD [DATA_BYTES...]
uart_emit_frame() {
  _cmd="$(_to_dec "$1")"; shift
  _len=$#
  _body="$_cmd $_len"
  for _b in "$@"; do
    _body="$_body $(_to_dec "$_b")"
  done
  # shellcheck disable=SC2086
  _crc="$(crc8_calc $_body)"
  # shellcheck disable=SC2086
  set -- $_body
  _emit_byte 2
  for _b in "$@"; do
    _emit_byte "$(_to_dec "$_b")"
  done
  _emit_byte "$_crc"
  _emit_byte 3
}

# Emit with forced LEN (negative protocol tests).
# Usage: uart_emit_frame_len CMD LEN [DATA_BYTES...]
uart_emit_frame_len() {
  _cmd="$(_to_dec "$1")"
  _len="$(_to_dec "$2")"
  shift 2
  _body="$_cmd $_len"
  for _b in "$@"; do
    _body="$_body $(_to_dec "$_b")"
  done
  # shellcheck disable=SC2086
  _crc="$(crc8_calc $_body)"
  # shellcheck disable=SC2086
  set -- $_body
  _emit_byte 2
  for _b in "$@"; do
    _emit_byte "$(_to_dec "$_b")"
  done
  _emit_byte "$_crc"
  _emit_byte 3
}

_socat_addr() {
  printf 'FILE:%s,b%s,cs8,parenb=0,cstopb=0,raw,echo=0,crtscts=0' \
    "$UART_DEVICE" "$UART_BAUD"
}

# Binary stdin → hex stdout via socat. $1 = inactivity timeout (sec).
uart_xfer() {
  _t="${1:-1}"
  socat -t"$_t" - "$(_socat_addr)" 2>/dev/null | xxd -p -c 256 | tr -d ' \n'
}

uart_flush() {
  # Discard pending RX bytes
  socat -t0.15 - "$(_socat_addr)" </dev/null >/dev/null 2>&1 || true
}

require_tty() {
  [ -e "$UART_DEVICE" ] || die "Serial port not found: ${UART_DEVICE} (set UART_DEVICE=...)"
  command -v socat >/dev/null 2>&1 || die "socat is required"
  command -v xxd >/dev/null 2>&1 || die "xxd is required"
}

uart_open() {
  require_tty
  uart_flush
  if [ "${UART_POST_OPEN_DELAY_SEC:-0}" != 0 ]; then
    sleep "$UART_POST_OPEN_DELAY_SEC"
  fi
}

uart_close() { :; }
uart_drain_fd() { uart_flush; }
test_cleanup() { uart_close; }

uart_tx_rx() {
  # $1=timeout; remaining args = uart_emit_frame args
  _t="$1"; shift
  uart_flush
  uart_emit_frame "$@" | uart_xfer "$_t"
}

uart_tx_rx_len() {
  _t="$1"; shift
  uart_flush
  uart_emit_frame_len "$@" | uart_xfer "$_t"
}

uart_wait_mcu_ready() {
  _attempt=1
  _max="${BOOT_PING_RETRIES:-60}"
  _interval="${BOOT_PING_INTERVAL_SEC:-0.5}"
  log_info "Waiting for MCU UART (up to ${_max} PING, ${_interval}s interval)..."
  require_tty
  while [ "$_attempt" -le "$_max" ]; do
    if _hex="$(cmd_ping 2>/dev/null)" && expect_ping_aa "$_hex"; then
      log_pass "MCU ready (PING ok, attempt ${_attempt})"
      return 0
    fi
    sleep "$_interval"
    _attempt=$((_attempt + 1))
  done
  return 1
}

# --- Commands ---

cmd_ping() {
  uart_tx_rx "$ACK_TIMEOUT_SEC" 0x01
}

cmd_ping_probe_ready() {
  _hex="$(cmd_ping 2>/dev/null)" || return 1
  expect_ping_aa "$_hex" || return 1
  printf '%s' "$_hex"
}

cmd_get_status() {
  _attempt=1
  while [ "$_attempt" -le 3 ]; do
    uart_flush
    _hex="$(uart_emit_frame 0x04 | uart_xfer "$GET_STATUS_TIMEOUT_SEC")" || _hex=""
    if [ -n "$_hex" ] && validate_get_status_hex "$_hex" && validate_frame_crc "$_hex"; then
      printf '%s' "$_hex"
      return 0
    fi
    sleep 0.1
    _attempt=$((_attempt + 1))
  done
  return 1
}

cmd_reset_fault() {
  uart_tx_rx "$ACK_TIMEOUT_SEC" 0x05
}

cmd_reset_bridge() {
  uart_tx_rx "$ACK_TIMEOUT_SEC" 0x06
}

cmd_calibrate_offset() {
  uart_tx_rx "${CALIBRATE_OFFSET_TIMEOUT_SEC:-3.0}" 0x09
}

cmd_bootloader_enter() {
  # Graceful DN before ACK can take ~50–100 ms with display on; keep headroom.
  uart_tx_rx "${OTA_BOOTLOADER_ACK_TIMEOUT_SEC:-${ACK_TIMEOUT_SEC}}" 0x08
}

cmd_get_version() {
  _attempt=1
  while [ "$_attempt" -le 3 ]; do
    _hex="$(uart_tx_rx "$ACK_TIMEOUT_SEC" 0x0A)" || _hex=""
    if [ -n "$_hex" ] && validate_frame_crc "$_hex"; then
      _ln="$(hex_byte "$_hex" 2)"
      if [ "$_ln" -eq 13 ]; then
        printf '%s' "$_hex"
        return 0
      fi
    fi
    sleep 0.1
    _attempt=$((_attempt + 1))
  done
  return 1
}

cmd_power_ctrl() {
  _mask="$(_to_dec "$1")"
  _value="$(_to_dec "$2")"
  _ml=$((_mask & 255))
  _mh=$(( (_mask >> 8) & 255 ))
  _vl=$((_value & 255))
  _vh=$(( (_value >> 8) & 255 ))
  _attempt=1
  while [ "$_attempt" -le 3 ]; do
    _hex="$(uart_tx_rx "$ACK_TIMEOUT_SEC" 0x02 "$_ml" "$_mh" "$_vl" "$_vh")" || _hex=""
    if [ -n "$_hex" ] && validate_frame_crc "$_hex"; then
      _stx="$(hex_byte "$_hex" 0)"
      _cmd="$(hex_byte "$_hex" 1)"
      _ln="$(hex_byte "$_hex" 2)"
      _etx="$(hex_byte "$_hex" 5)"
      if [ "$_stx" -eq 2 ] && [ "$_cmd" -eq 2 ] && [ "$_ln" -eq 1 ] && [ "$_etx" -eq 3 ]; then
        # honour POWER_CTRL_TX_DELAY after success path as quiet settle
        sleep "${POWER_CTRL_TX_DELAY_SEC:-0.25}" >/dev/null 2>&1 || true
        printf '%s' "$_hex"
        return 0
      fi
    fi
    sleep 0.1
    _attempt=$((_attempt + 1))
  done
  return 1
}

cmd_set_brightness() {
  _pwm="$(_to_dec "$1")"
  _lo=$((_pwm & 255))
  _hi=$(( (_pwm >> 8) & 255 ))
  uart_tx_rx "$ACK_TIMEOUT_SEC" 0x03 "$_lo" "$_hi"
}

cmd_set_brightness_len() {
  _ln="$1"; shift
  uart_tx_rx_len "$ACK_TIMEOUT_SEC" 0x03 "$_ln" "$@"
}

cmd_set_thresholds() {
  _mask="$(_to_dec "$1")"; shift
  _ml=$((_mask & 255))
  _mh=$(( (_mask >> 8) & 255 ))
  _attempt=1
  _last_hex=""
  while [ "$_attempt" -le 3 ]; do
    _hex="$(uart_tx_rx "$ACK_TIMEOUT_SEC" 0x07 "$_ml" "$_mh" "$@")" || _hex=""
    if [ -n "$_hex" ]; then
      _last_hex="$_hex"
      sleep "${SET_THRESH_TX_DELAY_SEC:-0.2}"
      # Require a real SET_THRESHOLDS ACK (STX/CMD/LEN) — UART noise can look like status≠0.
      if [ "$(hex_byte "$_hex" 0)" -eq 2 ] && [ "$(hex_byte "$_hex" 1)" -eq 7 ] \
        && [ "$(hex_byte "$_hex" 2)" -eq 1 ] && expect_ack_status "$_hex" 0; then
        printf '%s' "$_hex"
        return 0
      fi
      log_info "SET_THRESHOLDS bad/NACK frame attempt=${_attempt}/3 hex=${_hex}" >&2
    fi
    sleep 0.15
    _attempt=$((_attempt + 1))
  done
  [ -n "$_last_hex" ] && printf '%s' "$_last_hex"
  return 1
}

fault_trigger_v12_range() {
  _tmin="${FAULT_V12_TRAP_MIN_MV:-13000}"
  _tmax="${FAULT_V12_TRAP_MAX_MV:-14000}"
  cmd_set_thresholds 0x0002 $((_tmin & 255)) $(( (_tmin >> 8) & 255 )) \
    $((_tmax & 255)) $(( (_tmax >> 8) & 255 ))
}

fault_restore_v12_defaults() {
  _tmin="${THRESH_V12_MIN_MV:-10000}"
  _tmax="${THRESH_V12_MAX_MV:-13000}"
  cmd_set_thresholds 0x0002 $((_tmin & 255)) $(( (_tmin >> 8) & 255 )) \
    $((_tmax & 255)) $(( (_tmax >> 8) & 255 ))
}

fault_trigger_v5_range() {
  _tmin="${FAULT_V5_TRAP_MIN_MV:-5600}"
  _tmax="${FAULT_V5_TRAP_MAX_MV:-5800}"
  cmd_set_thresholds 0x0004 $((_tmin & 255)) $(( (_tmin >> 8) & 255 )) \
    $((_tmax & 255)) $(( (_tmax >> 8) & 255 ))
}

fault_restore_v5_defaults() {
  _tmin="${THRESH_V5_MIN_MV:-4500}"
  _tmax="${THRESH_V5_MAX_MV:-5500}"
  cmd_set_thresholds 0x0004 $((_tmin & 255)) $(( (_tmin >> 8) & 255 )) \
    $((_tmax & 255)) $(( (_tmax >> 8) & 255 ))
}

fault_trigger_v3v3_range() {
  _tmin="${FAULT_V3V3_TRAP_MIN_MV:-3600}"
  _tmax="${FAULT_V3V3_TRAP_MAX_MV:-3700}"
  cmd_set_thresholds 0x0008 $((_tmin & 255)) $(( (_tmin >> 8) & 255 )) \
    $((_tmax & 255)) $(( (_tmax >> 8) & 255 ))
}

fault_restore_v3v3_defaults() {
  _tmin="${THRESH_V3V3_MIN_MV:-3000}"
  _tmax="${THRESH_V3V3_MAX_MV:-3600}"
  cmd_set_thresholds 0x0008 $((_tmin & 255)) $(( (_tmin >> 8) & 255 )) \
    $((_tmax & 255)) $(( (_tmax >> 8) & 255 ))
}

fault_wait_flags() {
  _want="$(_to_dec "$1")"
  _tries="${2:-30}"
  _i=1
  while [ "$_i" -le "$_tries" ]; do
    sleep "${FAULT_POLL_INTERVAL_SEC:-0.1}"
    _hex="$(cmd_get_status)" || { _i=$((_i + 1)); continue; }
    if expect_fault_flags "$_hex" "has:$_want"; then
      printf '%s' "$_hex"
      return 0
    fi
    _i=$((_i + 1))
  done
  return 1
}

validate_get_status_hex() {
  _h="$(hex_norm "$1")"
  [ ${#_h} -eq 54 ] || return 1
  [ "$(hex_byte "$_h" 0)" -eq 2 ] || return 1
  [ "$(hex_byte "$_h" 1)" -eq 4 ] || return 1
  [ "$(hex_byte "$_h" 2)" -eq 22 ] || return 1
  [ "$(hex_byte "$_h" 26)" -eq 3 ] || return 1
  return 0
}

validate_frame_crc() {
  _h="$(hex_norm "$1")"
  _nbytes=$(( ${#_h} / 2 ))
  [ "$_nbytes" -ge 5 ] || return 1
  [ "$(hex_byte "$_h" 0)" -eq 2 ] || return 1
  [ "$(hex_byte "$_h" $((_nbytes - 1)))" -eq 3 ] || return 1
  _cmd="$(hex_byte "$_h" 1)"
  _ln="$(hex_byte "$_h" 2)"
  _body="$_cmd $_ln"
  _i=0
  while [ "$_i" -lt "$_ln" ]; do
    _body="$_body $(hex_byte "$_h" $((3 + _i)))"
    _i=$((_i + 1))
  done
  _crc_rx="$(hex_byte "$_h" $((3 + _ln)))"
  # shellcheck disable=SC2086
  _crc="$(crc8_calc $_body)"
  [ "$_crc" -eq "$_crc_rx" ]
}

parse_get_status_hex() {
  _h="$(hex_norm "$1")"
  [ ${#_h} -eq 54 ] || { echo "error=bad_frame_len" >&2; return 2; }
  printf 'v24=%s\n' "$(hex_u16le "$_h" 3)"
  printf 'v12=%s\n' "$(hex_u16le "$_h" 5)"
  printf 'v5=%s\n' "$(hex_u16le "$_h" 7)"
  printf 'v3v3=%s\n' "$(hex_u16le "$_h" 9)"
  printf 'i_lcd=%s\n' "$(hex_i16le "$_h" 11)"
  printf 'i_backlight=%s\n' "$(hex_i16le "$_h" 13)"
  printf 'i_scaler=%s\n' "$(hex_i16le "$_h" 15)"
  printf 'i_audio_l=%s\n' "$(hex_i16le "$_h" 17)"
  printf 'i_audio_r=%s\n' "$(hex_i16le "$_h" 19)"
  _state="$(hex_byte "$_h" 21)"
  _fault="$(hex_u16le "$_h" 22)"
  _inputs="$(hex_byte "$_h" 24)"
  printf 'state=0x%02x\n' "$_state"
  printf 'fault_flags=0x%04x\n' "$_fault"
  printf 'inputs=0x%02x\n' "$_inputs"
  printf 'pgood=%d\n' "$(( (_inputs >> 6) & 1 ))"
}

expect_ack_status() {
  _got="$(hex_byte "$1" 3)"
  [ "$_got" -eq "$2" ]
}

expect_ping_aa() {
  _h="$(hex_norm "$1")"
  [ ${#_h} -ge 12 ] || return 1
  [ "$(hex_byte "$_h" 0)" -eq 2 ] || return 1
  [ "$(hex_byte "$_h" 1)" -eq 1 ] || return 1
  [ "$(hex_byte "$_h" 3)" -eq 170 ] || return 1
  return 0
}

expect_bootloader_enter_ack() {
  _h="$(hex_norm "$1")"
  [ ${#_h} -ge 12 ] || return 1
  [ "$(hex_byte "$_h" 0)" -eq 2 ] || return 1
  [ "$(hex_byte "$_h" 1)" -eq 8 ] || return 1
  [ "$(hex_byte "$_h" 2)" -eq 1 ] || return 1
  [ "$(hex_byte "$_h" 3)" -eq 0 ] || return 1
  [ "$(hex_byte "$_h" 5)" -eq 3 ] || return 1
  return 0
}

expect_get_status_clean() {
  _h="$(hex_norm "$1")"
  _state="$(hex_byte "$_h" 21)"
  _fault="$(hex_u16le "$_h" 22)"
  [ "$_state" -eq 48 ] && [ "$_fault" -eq 0 ]
}

expect_fault_flags() {
  _h="$(hex_norm "$1")"
  _fault="$(hex_u16le "$_h" 22)"
  _spec="$2"
  case "$_spec" in
    has:*)
      _mask="$(_to_dec "${_spec#has:}")"
      [ "$((_fault & _mask))" -eq "$_mask" ]
      ;;
    *)
      _want="$(_to_dec "$_spec")"
      [ "$_fault" -eq "$_want" ]
      ;;
  esac
}

expect_state_bits() {
  _h="$(hex_norm "$1")"
  _state="$(hex_byte "$_h" 21)"
  _set="$(_to_dec "$2")"
  _clr="$(_to_dec "${3:-0}")"
  [ "$((_state & _set))" -eq "$_set" ] || return 1
  [ "$((_state & _clr))" -eq 0 ] || return 1
  return 0
}

expect_state_unchanged() {
  _s1="$(hex_byte "$1" 21)"
  _s2="$(hex_byte "$2" 21)"
  [ "$_s1" -eq "$_s2" ]
}

expect_fault_reserved_clear() {
  _fault="$(hex_u16le "$1" 22)"
  [ "$((_fault & 32768))" -eq 0 ]
}

# --- Peripheral helpers ---

periph_test_needs_display() {
  case "$1" in
    04_telemetry_under_load.sh|05_backlight_brightness.sh|06_reset_bridge_display.sh|\
    08_display_shutdown.sh|09_fault_lcd_current.sh|11_backlight_only_off.sh|\
    12_all_at_once_up.sh|13_fault_recovery_display.sh|14_set_brightness_boundary.sh|\
    15_display_resequence.sh|16_stress_get_status_load.sh|17_iwdg_stress_load.sh|\
    18_fault_v12_under_load.sh|19_fault_scaler_current.sh|20_fault_backlight_current.sh|\
    21_fault_audio_current.sh|26_set_brightness_no_bl.sh|\
    27_set_brightness_neg.sh|28_bl_bor_diag.sh|29_calibrate_offset_neg_display.sh|\
    30_fault_v5_under_load.sh|32_fault_v3v3_under_load.sh) return 0 ;;
    *) return 1 ;;
  esac
}

periph_output_blocks_display_suite() {
  printf '%s' "$1" | grep -qE '0x2001|FAULT_SEQ_ABORT\|FAULT_SCALER|FAULT_SEQ_ABORT|SCALER\+LCD ON failed|SCALER_POWER_M'
}

wait_get_status_state() {
  _set_mask="$1"
  _clear_mask="${2:-0}"
  _tries="${3:-${STATE_POLL_TRIES:-40}}"
  _i=1
  while [ "$_i" -le "$_tries" ]; do
    sleep "${STATE_POLL_INTERVAL_SEC:-0.1}"
    _hex="$(cmd_get_status)" || { _i=$((_i + 1)); continue; }
    if expect_state_bits "$_hex" "$_set_mask" "$_clear_mask"; then
      printf '%s' "$_hex"
      return 0
    fi
    _i=$((_i + 1))
  done
  return 1
}

wait_get_status_clean() {
  _tries="${1:-${STATE_POLL_TRIES:-40}}"
  _i=1
  while [ "$_i" -le "$_tries" ]; do
    sleep "${STATE_POLL_INTERVAL_SEC:-0.1}"
    _hex="$(cmd_get_status)" || { _i=$((_i + 1)); continue; }
    if expect_get_status_clean "$_hex"; then
      printf '%s' "$_hex"
      return 0
    fi
    _i=$((_i + 1))
  done
  return 1
}

periph_log_status() {
  log_info "--- GET_STATUS (${2:-status}) ---" >&2
  parse_get_status_hex "$1" >&2 || true
}

periph_currents_near_zero() {
  expect_currents_in_window "$1" "$TELEMETRY_I_ZERO_MIN_MA" "$TELEMETRY_I_ZERO_MAX_MA" \
    "${TELEMETRY_I_CHANNELS:-i_lcd,i_scaler,i_audio_l,i_audio_r}"
}

periph_prepare_zero_load() {
  cmd_reset_fault >/dev/null 2>&1 || true
  sleep 0.1
  periph_all_domains_off || return 1
  sleep 0.2
  _gs="$(cmd_get_status)" || return 1
  if periph_currents_near_zero "$_gs" >/dev/null 2>&1; then
    log_info "current offsets OK (±${TELEMETRY_I_ZERO_MAX_MA} mA at state=0x30)"
    return 0
  fi
  periph_log_status "$_gs" "before calibrate"
  log_info "CALIBRATE_OFFSET (zero load, state=0x30; else spurious FAULT_SCALER/AUDIO)"
  _hex="$(cmd_calibrate_offset)" || { log_fail "CALIBRATE_OFFSET: no ACK"; return 1; }
  expect_ack_status "$_hex" 0 || { log_fail "CALIBRATE_OFFSET: status!=0 (domains must be off)"; return 1; }
  sleep 0.35
  cmd_reset_fault >/dev/null 2>&1 || true
  sleep 0.1
  _gs="$(cmd_get_status)" || return 1
  if periph_currents_near_zero "$_gs"; then
    log_pass "CALIBRATE_OFFSET: zero-load currents OK"
    return 0
  fi
  periph_log_status "$_gs" "after calibrate"
  log_fail "currents still high after CALIBRATE_OFFSET — check shunts / state!=0"
  return 1
}

periph_display_all_off() {
  log_info "display off: mask=0x0007 value=0"
  _hex="$(cmd_power_ctrl 0x0007 0x0000)" || return 1
  expect_ack_status "$_hex" 0 || return 1
  sleep "${SEQ_DN_WAIT_SEC:-1.0}"
  wait_get_status_state "$STATE_ETH_ALWAYS_ON_HEX" 0x07 >/dev/null
}

periph_all_domains_off() {
  log_info "all domains off: mask=0x007F value=0"
  _hex="$(cmd_power_ctrl 0x007f 0x0000)" || return 1
  expect_ack_status "$_hex" 0 || return 1
  sleep "${SEQ_DN_WAIT_SEC:-1.0}"
  if ! _gs="$(wait_get_status_clean "${STATE_POLL_TRIES:-40}")"; then
    log_fail "all domains off: timeout waiting state=0x30 fault=0"
    return 1
  fi
  if ! expect_state_bits "$_gs" "$STATE_ETH_ALWAYS_ON_HEX" "$STATE_MANAGED_OFF_CLEAR_MASK_HEX"; then
    periph_log_status "$_gs" "all domains off"
    log_fail "all domains off: state!=0x30 (e.g. 0x4B auto-startup tail — retry RESET_FAULT)"
    return 1
  fi
}

periph_state_is_autostart_tail() {
  [ "$(hex_byte "$1" 21)" -eq 75 ]
}

periph_strip_nondisplay_domains() {
  _gs="$(cmd_get_status 2>/dev/null)" || return 1
  if expect_state_bits "$_gs" "$STATE_ETH_ALWAYS_ON_HEX" "$PERIPH_PREP_NONDISPLAY_MASK_HEX"; then
    return 0
  fi
  log_info "clear non-display domains (mask=0x78 value=0)" >&2
  _hex="$(cmd_power_ctrl 0x0078 0x0000)" || return 1
  expect_ack_status "$_hex" 0 || return 1
  sleep "${AUDIO_SEQ_WAIT_SEC:-0.25}"
  if ! _gs="$(wait_get_status_state "$STATE_ETH_ALWAYS_ON_HEX" "$PERIPH_PREP_NONDISPLAY_MASK_HEX" 20)"; then
    _gs="$(cmd_get_status 2>/dev/null)" || _gs=""
    [ -n "$_gs" ] && periph_log_status "$_gs" "strip non-display failed"
    return 1
  fi
}

periph_display_scaler_lcd_on() {
  cmd_reset_fault >/dev/null 2>&1 || true
  sleep 0.05
  _gs="$(cmd_get_status 2>/dev/null)" || true
  if [ -n "${_gs:-}" ] && expect_state_bits "$_gs" 0x03 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" \
    && expect_fault_flags "$_gs" "0x0000"; then
    log_info "display path already on (state has SCALER+LCD)" >&2
    printf '%s' "$_gs"
    return 0
  fi
  log_info "POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)" >&2
  _hex="$(cmd_power_ctrl 0x0003 0x0003)" || return 1
  expect_ack_status "$_hex" 0 || return 1
  sleep "${SEQ_ON_WAIT_SEC:-2.0}"
  if ! _gs="$(wait_get_status_state 0x03 "${PERIPH_PREP_NONDISPLAY_MASK_HEX}")"; then
    _gs="$(cmd_get_status 2>/dev/null)" || _gs=""
    if [ -n "$_gs" ]; then
      periph_log_status "$_gs" "SCALER+LCD ON failed"
      if expect_fault_flags "$_gs" "has:0x2001"; then
        log_fail "FAULT_SEQ_ABORT|FAULT_SCALER — check SCALER_POWER_M (PB1) / SEQ_VERIFY"
      elif expect_fault_flags "$_gs" "has:0x0001"; then
        log_fail "FAULT_SCALER — often false trip >1500 mA: run CALIBRATE_OFFSET at state=0x30"
      fi
    fi
    return 1
  fi
  expect_fault_flags "$_gs" "0x0000" || return 1
  printf '%s' "$_gs"
}

periph_display_backlight_on() {
  _max_attempts="${PERIPH_BACKLIGHT_ON_TRIES:-3}"
  cmd_reset_fault >/dev/null 2>&1 || true
  sleep 0.05
  periph_strip_nondisplay_domains || return 1
  _gs="$(cmd_get_status 2>/dev/null)" || true
  if [ -n "${_gs:-}" ] && expect_state_bits "$_gs" 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" \
    && expect_fault_flags "$_gs" "0x0000"; then
    log_info "BACKLIGHT already on" >&2
    printf '%s' "$_gs"
    return 0
  fi
  _attempt=1
  while [ "$_attempt" -le "$_max_attempts" ]; do
    cmd_reset_fault >/dev/null 2>&1 || true
    sleep 0.05
    _gs="$(periph_display_scaler_lcd_on)" || {
      log_info "BACKLIGHT precondition failed: SCALER+LCD not ready (attempt ${_attempt}/${_max_attempts})" >&2
      _attempt=$((_attempt + 1)); continue
    }
    periph_strip_nondisplay_domains || { _attempt=$((_attempt + 1)); continue; }
    _gs="$(cmd_get_status 2>/dev/null)" || _gs=""
    if [ -n "$_gs" ] && ! expect_state_bits "$_gs" 0x03 "$PERIPH_PREP_NONDISPLAY_MASK_HEX"; then
      log_info "pre-BL: need clean SCALER+LCD (state=0x03), got unexpected state" >&2
      periph_log_status "$_gs" "pre-BL state"
      _attempt=$((_attempt + 1)); continue
    fi
    log_info "POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt ${_attempt}/${_max_attempts}" >&2
    _hex="$(cmd_power_ctrl 0x0004 0x0004)" || { _attempt=$((_attempt + 1)); continue; }
    _ack_status="$(hex_byte "$_hex" 3)"
    if [ "$_ack_status" != "0" ]; then
      log_info "BACKLIGHT ON rejected: ACK status=${_ack_status} (attempt ${_attempt}/${_max_attempts})" >&2
      sleep 0.15
      _attempt=$((_attempt + 1)); continue
    fi
    sleep "${SEQ_BL_WAIT_SEC:-1.0}"
    _gs="$(wait_get_status_state 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX")" || _gs=""
    if [ -n "$_gs" ]; then
      parse_get_status_hex "$_gs" >&2 || true
      if expect_fault_flags "$_gs" "0x0000"; then
        printf '%s' "$_gs"
        return 0
      fi
    fi
    _gs="$(cmd_get_status 2>/dev/null)" || _gs=""
    if [ -n "$_gs" ]; then
      periph_log_status "$_gs" "BACKLIGHT ON attempt ${_attempt} failed"
      if periph_state_is_autostart_tail "$_gs"; then
        log_info "state=0x4B: auto-startup tail or MCU reset — full re-prepare" >&2
        periph_all_domains_off || { _attempt=$((_attempt + 1)); continue; }
        sleep 0.2
        cmd_reset_fault >/dev/null 2>&1 || true
        periph_prepare_zero_load || { _attempt=$((_attempt + 1)); continue; }
      fi
    fi
    sleep 0.1
    _attempt=$((_attempt + 1))
  done
  return 1
}

cmd_set_thresholds_retry() {
  _mask="$1"; shift
  _attempt=1
  while [ "$_attempt" -le 3 ]; do
    if _hex="$(cmd_set_thresholds "$_mask" "$@")"; then
      printf '%s' "$_hex"
      return 0
    fi
    sleep 0.2
    _attempt=$((_attempt + 1))
  done
  return 1
}

expect_rails_in_range() {
  _h="$(hex_norm "$1")"
  _ok=1
  _v12="$(hex_u16le "$_h" 5)"
  _v5="$(hex_u16le "$_h" 7)"
  _v3="$(hex_u16le "$_h" 9)"
  if [ "$_v12" -lt "$THRESH_V12_MIN_MV" ] || [ "$_v12" -gt "$THRESH_V12_MAX_MV" ]; then
    echo "FAIL: v12=${_v12} mV not in [${THRESH_V12_MIN_MV},${THRESH_V12_MAX_MV}]" >&2
    _ok=0
  fi
  if [ "$_v5" -lt "$THRESH_V5_MIN_MV" ] || [ "$_v5" -gt "$THRESH_V5_MAX_MV" ]; then
    echo "FAIL: v5=${_v5} mV not in [${THRESH_V5_MIN_MV},${THRESH_V5_MAX_MV}]" >&2
    _ok=0
  fi
  if [ "$_v3" -lt "$THRESH_V3V3_MIN_MV" ] || [ "$_v3" -gt "$THRESH_V3V3_MAX_MV" ]; then
    echo "FAIL: v3v3=${_v3} mV not in [${THRESH_V3V3_MIN_MV},${THRESH_V3V3_MAX_MV}]" >&2
    _ok=0
  fi
  [ "$_ok" -eq 1 ] && echo "rails OK"
  [ "$_ok" -eq 1 ]
}

expect_currents_in_window() {
  _h="$(hex_norm "$1")"
  _min="$2"
  _max="$3"
  _csv="$4"
  _ok=1
  _old_ifs="$IFS"
  IFS=','
  # shellcheck disable=SC2086
  set -- $_csv
  IFS="$_old_ifs"
  for _ch in "$@"; do
    _ch="$(printf '%s' "$_ch" | tr -d ' ')"
    [ -n "$_ch" ] || continue
    case "$_ch" in
      i_lcd) _v="$(hex_i16le "$_h" 11)" ;;
      i_backlight) _v="$(hex_i16le "$_h" 13)" ;;
      i_scaler) _v="$(hex_i16le "$_h" 15)" ;;
      i_audio_l) _v="$(hex_i16le "$_h" 17)" ;;
      i_audio_r) _v="$(hex_i16le "$_h" 19)" ;;
      *) echo "FAIL: unknown channel ${_ch}" >&2; _ok=0; continue ;;
    esac
    if [ "$_v" -lt "$_min" ] || [ "$_v" -gt "$_max" ]; then
      echo "FAIL: ${_ch}=${_v} mA not in [${_min},${_max}]" >&2
      _ok=0
    fi
  done
  [ "$_ok" -eq 1 ] && echo "currents OK"
  [ "$_ok" -eq 1 ]
}

fault_set_i_lcd_max_ma() {
  _ma="$(_to_dec "$1")"
  cmd_set_thresholds_retry 0x0100 $((_ma & 255)) $(( (_ma >> 8) & 255 ))
}
fault_restore_i_lcd_max() { fault_set_i_lcd_max_ma "${THRESH_I_LCD_DEFAULT_MA:-2000}"; }

fault_set_i_bl_max_ma() {
  _ma="$(_to_dec "$1")"
  cmd_set_thresholds_retry 0x0200 $((_ma & 255)) $(( (_ma >> 8) & 255 ))
}
fault_restore_i_bl_max() { fault_set_i_bl_max_ma "${THRESH_I_BL_DEFAULT_MA:-3000}"; }

fault_set_i_scaler_max_ma() {
  _ma="$(_to_dec "$1")"
  cmd_set_thresholds_retry 0x0400 $((_ma & 255)) $(( (_ma >> 8) & 255 ))
}
fault_restore_i_scaler_max() { fault_set_i_scaler_max_ma "${THRESH_I_SCALER_DEFAULT_MA:-1500}"; }

fault_set_i_audio_lr_max_ma() {
  _ma="$(_to_dec "$1")"
  _lo=$((_ma & 255)); _hi=$(( (_ma >> 8) & 255 ))
  cmd_set_thresholds_retry 0x1800 "$_lo" "$_hi" "$_lo" "$_hi"
}
fault_restore_i_audio_lr_max() { fault_set_i_audio_lr_max_ma "${THRESH_I_AUDIO_DEFAULT_MA:-5000}"; }

periph_get_current_ma() {
  case "$2" in
    i_lcd) hex_i16le "$1" 11 ;;
    i_backlight) hex_i16le "$1" 13 ;;
    i_scaler) hex_i16le "$1" 15 ;;
    i_audio_l) hex_i16le "$1" 17 ;;
    i_audio_r) hex_i16le "$1" 19 ;;
    *) return 1 ;;
  esac
}

periph_wait_status_load_ma() {
  _channel="$1"
  _min_ma="${2:-3}"
  _want_state="${3:-0x03}"
  _clear_mask="${4:-${PERIPH_PREP_NONDISPLAY_MASK_HEX:-0x78}}"
  _tries="${5:-${STATE_POLL_TRIES:-40}}"
  _i=1
  while [ "$_i" -le "$_tries" ]; do
    sleep "${STATE_POLL_INTERVAL_SEC:-0.1}"
    _hex="$(cmd_get_status)" || { _i=$((_i + 1)); continue; }
    expect_state_bits "$_hex" "$_want_state" "$_clear_mask" 2>/dev/null || { _i=$((_i + 1)); continue; }
    _ma="$(periph_get_current_ma "$_hex" "$_channel" 2>/dev/null)" || { _i=$((_i + 1)); continue; }
    if [ "$_ma" -ge "$_min_ma" ]; then
      printf '%s' "$_hex"
      return 0
    fi
    _i=$((_i + 1))
  done
  return 1
}

periph_fault_trap_i_ma() {
  _hex="$1"; _channel="$2"; _setter="$3"
  _margin="${4:-${THRESH_I_TRAP_MARGIN_MA:-15}}"
  _i_ma="$(periph_get_current_ma "$_hex" "$_channel" 2>/dev/null)" || _i_ma=""
  if [ -z "$_i_ma" ] || [ "$_i_ma" -le 0 ]; then
    _hex="$(cmd_get_status 2>/dev/null)" || return 1
    _i_ma="$(periph_get_current_ma "$_hex" "$_channel")" || return 1
  fi
  if [ "$_i_ma" -le 0 ]; then
    log_fail "${_channel}=${_i_ma} mA: need positive load for overcurrent trap" >&2
    return 1
  fi
  if [ "$_i_ma" -gt "$_margin" ]; then
    _trap=$((_i_ma - _margin))
  elif [ "$_i_ma" -gt 10 ]; then
    _trap=$((_i_ma - 10))
  else
    _trap=$((_i_ma / 2))
  fi
  # Floor 1 mA (not 5): this board often has i_lcd ~3..11 mA — a 5 mA floor
  # made FAULT_LCD traps impossible whenever load <= 5.
  if [ "$_trap" -lt 1 ]; then _trap=1; fi
  if [ "$_i_ma" -le "$_trap" ]; then
    log_skip "${_channel}=${_i_ma} mA: cannot trap below load (I_MAX=${_trap} mA)"
    return 2
  fi
  log_info "${_channel} load=${_i_ma} mA → trap I_MAX=${_trap} mA" >&2
  "$_setter" "$_trap"
}

periph_fault_wait_latched() {
  fault_wait_flags "$(_to_dec "$1")" "${2:-${FAULT_WAIT_TRIES:-40}}"
}

periph_stress_get_status_under_display_load() {
  _count="$1"; _gap="$2"; _pass_label="$3"; _pass_detail="$4"
  _info_suffix="${5:-}"; _no_resp_extra="${6:-}"
  periph_prepare_zero_load || die "prepare failed"
  periph_display_scaler_lcd_on >/dev/null || die "need SCALER+LCD for load stress"
  _gs="$(periph_display_backlight_on)" || die "need BACKLIGHT for state=0x07"
  expect_state_bits "$_gs" 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" || die "expected state=0x07"
  _info_msg="${_count}x GET_STATUS @ state=0x07, gap ${_gap}s"
  [ -n "$_info_suffix" ] && _info_msg="${_info_msg} ${_info_suffix}"
  log_info "$_info_msg"
  _ok=0
  _i=1
  while [ "$_i" -le "$_count" ]; do
    _hex="$(cmd_get_status)" || die "iteration ${_i}: no response${_no_resp_extra}"
    validate_get_status_hex "$_hex" || die "iteration ${_i}: invalid frame"
    _ok=$((_ok + 1))
    sleep "$_gap"
    _i=$((_i + 1))
  done
  log_pass "${_pass_label}: ${_ok}/${_count} ${_pass_detail}"
}

parse_get_version_hex() {
  _h="$(hex_norm "$1")"
  [ ${#_h} -eq 36 ] || { echo "frame_len=$(( ${#_h}/2 )) expected=18" >&2; return 1; }
  [ "$(hex_byte "$_h" 0)" -eq 2 ] || return 1
  [ "$(hex_byte "$_h" 1)" -eq 10 ] || return 1
  [ "$(hex_byte "$_h" 2)" -eq 13 ] || return 1
  [ "$(hex_byte "$_h" 17)" -eq 3 ] || return 1
  # DATA bytes 0..7 = git hash ASCII as hex pairs at frame bytes 3..10
  _hash="$(printf '%s' "$_h" | cut -c 7-22)"
  # convert hex ascii encoding of ascii chars: each pair is one char code
  _hash_s=""
  _i=0
  while [ "$_i" -lt 8 ]; do
    _pair="$(printf '%s' "$_hash" | cut -c $((_i * 2 + 1))-$((_i * 2 + 2)))"
    _b="$(printf '%d' "0x$_pair")"
    _hash_s="${_hash_s}$(awk -v b="$_b" 'BEGIN{printf "%c", b}')"
    _i=$((_i + 1))
  done
  _dirty="$(hex_byte "$_h" 11)"
  _e0="$(hex_byte "$_h" 12)"
  _e1="$(hex_byte "$_h" 13)"
  _e2="$(hex_byte "$_h" 14)"
  _e3="$(hex_byte "$_h" 15)"
  _epoch=$((_e0 + _e1 * 256 + _e2 * 65536 + _e3 * 16777216))
  printf 'git_hash=%s dirty=%s build_epoch=%s\n' "$_hash_s" "$_dirty" "$_epoch"
}

# --- OTA helpers (stm32flash + IC17 NRST after -g) ---

ota_require_stm32flash() {
  command -v "${OTA_STM32FLASH:-stm32flash}" >/dev/null 2>&1 \
    || die "${OTA_STM32FLASH:-stm32flash} not found"
}

# PCA9555 push-pull: every level must be written; --mode=exit hangs on this board.
ic17_gpioset_write() {
  if gpioset --help 2>&1 | grep -q -- '--mode'; then
    gpioset --mode=time --usec="${IC17_PULSE_USEC:-20000}" \
      "${IC17_GPIOCHIP:-gpiochip5}" "$@"
  else
    gpioset -c "${IC17_GPIOCHIP:-gpiochip5}" \
      -t "${IC17_PULSE_USEC:-20000}us" "$@"
  fi
}

# Application reboot only: BOOT0=0, NRST assert/release. Does not re-arm pending.
ic17_nrst_pulse() {
  command -v gpioset >/dev/null 2>&1 || die "gpioset not found (need IC17 NRST)"
  _chip="${IC17_GPIOCHIP:-gpiochip5}"
  _nrst="${IC17_LINE_NRST:-8}"
  _boot0="${IC17_LINE_BOOT0:-9}"
  log_info "IC17 NRST pulse (BOOT0=0) ${_chip} boot0=${_boot0} nrst=${_nrst}"
  ic17_gpioset_write "${_boot0}=0" || die "IC17 BOOT0=0 failed"
  ic17_gpioset_write "${_nrst}=0" || die "IC17 NRST=0 failed"
  ic17_gpioset_write "${_nrst}=1" || die "IC17 NRST=1 failed"
}

# Run OTA_NRST_CMD if set, else built-in IC17 pulse, else interactive Enter.
ota_run_nrst() {
  if [ -n "${OTA_NRST_CMD:-}" ]; then
    log_info "NRST: OTA_NRST_CMD"
    sh -c "$OTA_NRST_CMD" || die "OTA_NRST_CMD failed"
  elif command -v gpioset >/dev/null 2>&1; then
    ic17_nrst_pulse
  else
    log_info "NRST: press Enter after NRST/power-cycle (gpioset/OTA_NRST_CMD unavailable)"
    # BusyBox ash: plain read
    read -r _
  fi
  sleep "${OTA_NRST_SETTLE_SEC:-1}"
}

# BOOTLOADER_ENTER → stm32flash -w -v -g → [IC17 NRST] → wait for app PING.
# Usage: ota_flash_app /path/to/POWER_Controller_BNT.bin
ota_flash_app() {
  _fw="$1"
  [ -n "$_fw" ] && [ -f "$_fw" ] || die "firmware bin not found: ${_fw:-<empty>}"
  ota_require_stm32flash
  require_tty

  uart_open
  _hex="$(cmd_bootloader_enter)" || die "BOOTLOADER_ENTER: no response"
  expect_bootloader_enter_ack "$_hex" || die "BOOTLOADER_ENTER: bad ACK"
  log_pass "BOOTLOADER_ENTER ACK"
  uart_close
  sleep "${OTA_RESET_DELAY_SEC:-0.5}"

  log_info "stm32flash -w $_fw → ${OTA_FLASH_ADDR:-0x08000000}"
  "${OTA_STM32FLASH:-stm32flash}" -b "$UART_BAUD" \
    -w "$_fw" -v -g "${OTA_FLASH_ADDR:-0x08000000}" \
    "$UART_DEVICE" || die "stm32flash write failed"

  # ROM Go leaves UART dead on this board; NRST (BOOT0=0) starts app cleanly.
  if [ "${OTA_POST_FLASH_NRST:-1}" = 1 ]; then
    log_info "post-flash NRST (after stm32flash -g)"
    ota_run_nrst
  fi

  uart_wait_mcu_ready || die "MCU did not answer PING after OTA"
}

# Application reboot without BOOTLOADER_ENTER (does not re-arm pending).
ota_nrst_reboot() {
  _label="${1:-NRST reboot}"
  log_info "$_label"
  uart_close
  ota_run_nrst
  uart_open
  _hex="$(cmd_ping)" || die "no PING after reboot ($_label)"
  expect_ack_status "$_hex" 170 || die "PING expected 0xAA ($_label)"
}

