# Общие функции UART-тестов с периферией (поверх Tests_UART).
# shellcheck shell=bash

_TESTS_UART_ALL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=config.sh
source "${_TESTS_UART_ALL_DIR}/config.sh"
# shellcheck source=../Tests_UART/lib.sh
source "${_TESTS_UART_ALL_DIR}/../Tests_UART/lib.sh"
# shellcheck source=config.sh
source "${_TESTS_UART_ALL_DIR}/config.sh"

# --- Display suite gating (run_all: SKIP вместо каскада FAIL) ---

periph_test_needs_display() {
  case "$1" in
    04_telemetry_under_load.sh|05_backlight_brightness.sh|06_reset_bridge_display.sh|\
    08_display_shutdown.sh|09_fault_lcd_current.sh) return 0 ;;
    *) return 1 ;;
  esac
}

# По выводу теста 03 или fault_flags в GET_STATUS
periph_output_blocks_display_suite() {
  local out=$1
  grep -qE '0x2001|FAULT_SEQ_ABORT\|FAULT_SCALER|FAULT_SEQ_ABORT|SCALER\+LCD ON failed|SCALER_POWER_M' <<<"$out"
}

# --- Ожидание state / fault ---

wait_get_status_state() {
  local set_mask=$1 clear_mask=${2:-0}
  local tries=${3:-${STATE_POLL_TRIES:-40}}
  local i hex
  for ((i = 1; i <= tries; i++)); do
    sleep "${STATE_POLL_INTERVAL_SEC:-0.1}"
    hex="$(cmd_get_status)" || continue
    if expect_state_bits "$hex" "$set_mask" "$clear_mask"; then
      printf '%s' "$hex"
      return 0
    fi
  done
  return 1
}

wait_get_status_clean() {
  local tries=${1:-${STATE_POLL_TRIES:-40}}
  local i hex
  for ((i = 1; i <= tries; i++)); do
    sleep "${STATE_POLL_INTERVAL_SEC:-0.1}"
    hex="$(cmd_get_status)" || continue
    if expect_get_status_clean "$hex"; then
      printf '%s' "$hex"
      return 0
    fi
  done
  return 1
}

periph_log_status() {
  local hex=$1
  local tag=${2:-status}
  log_info "--- GET_STATUS (${tag}) ---"
  parse_get_status_hex "$hex" || true
}

periph_currents_near_zero() {
  local hex=$1
  export TELEMETRY_I_ZERO_MIN_MA TELEMETRY_I_ZERO_MAX_MA
  expect_currents_in_window "$hex" "$TELEMETRY_I_ZERO_MIN_MA" "$TELEMETRY_I_ZERO_MAX_MA" \
    "i_lcd,i_backlight,i_scaler,i_audio_l,i_audio_r"
}

# state=0, сброс fault, CALIBRATE_OFFSET при «завышенных» токах (типично без калибровки во flash)
periph_prepare_zero_load() {
  local hex gs
  cmd_reset_fault >/dev/null 2>&1 || true
  sleep 0.1
  periph_all_domains_off || return 1
  sleep 0.2
  gs="$(cmd_get_status)" || return 1
  if periph_currents_near_zero "$gs"; then
    log_info "current offsets OK (±${TELEMETRY_I_ZERO_MAX_MA} mA at state=0)"
    return 0
  fi
  log_info "CALIBRATE_OFFSET (zero load, state=0; else spurious FAULT_SCALER/AUDIO)"
  hex="$(cmd_calibrate_offset)" || {
    log_fail "CALIBRATE_OFFSET: no ACK"
    return 1
  }
  expect_ack_status "$hex" 0 || {
    log_fail "CALIBRATE_OFFSET: status!=0 (domains must be off)"
    return 1
  }
  sleep 0.35
  cmd_reset_fault >/dev/null 2>&1 || true
  sleep 0.1
  gs="$(cmd_get_status)" || return 1
  if periph_currents_near_zero "$gs"; then
    log_pass "CALIBRATE_OFFSET: zero-load currents OK"
    return 0
  fi
  periph_log_status "$gs" "after calibrate"
  log_fail "currents still high after CALIBRATE_OFFSET — check shunts / state!=0"
  return 1
}

# Снять биты SCALER|LCD|BACKLIGHT (полный DN-секвенс при SCALER/LCD OFF)
periph_display_all_off() {
  local hex
  log_info "display off: mask=0x0007 value=0"
  hex="$(cmd_power_ctrl 0x0007 0x0000)" || return 1
  expect_ack_status "$hex" 0 || return 1
  sleep "${SEQ_DN_WAIT_SEC:-1.0}"
  wait_get_status_state 0 0x07 >/dev/null
}

# Все домены (включая TOUCH/AUDIO после auto-startup при PGOOD)
periph_all_domains_off() {
  local hex
  log_info "all domains off: mask=0x007F value=0"
  hex="$(cmd_power_ctrl 0x007f 0x0000)" || return 1
  expect_ack_status "$hex" 0 || return 1
  sleep "${SEQ_DN_WAIT_SEC:-1.0}"
  wait_get_status_clean "${STATE_POLL_TRIES:-40}" >/dev/null
}

periph_display_scaler_lcd_on() {
  local hex gs
  cmd_reset_fault >/dev/null 2>&1 || true
  sleep 0.05
  gs="$(cmd_get_status 2>/dev/null)" || true
  if [[ -n "${gs:-}" ]] && expect_state_bits "$gs" 0x03 0 && expect_fault_flags "$gs" "0x0000"; then
    log_info "SCALER+LCD already on"
    printf '%s' "$gs"
    return 0
  fi
  log_info "POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)"
  hex="$(cmd_power_ctrl 0x0003 0x0003)" || return 1
  expect_ack_status "$hex" 0 || return 1
  sleep "${SEQ_ON_WAIT_SEC:-2.0}"
  if ! gs="$(wait_get_status_state 0x03 0)"; then
    gs="$(cmd_get_status 2>/dev/null)" || gs=""
    if [[ -n "$gs" ]]; then
      periph_log_status "$gs" "SCALER+LCD ON failed"
      if expect_fault_flags "$gs" "has:0x2001"; then
        log_fail "FAULT_SEQ_ABORT|FAULT_SCALER — check SCALER_POWER_M (PB1) / SEQ_VERIFY"
      elif expect_fault_flags "$gs" "has:0x0001"; then
        log_fail "FAULT_SCALER — often false trip >1500 mA: run CALIBRATE_OFFSET at state=0"
      fi
    fi
    return 1
  fi
  expect_fault_flags "$gs" "0x0000" || return 1
  printf '%s' "$gs"
}

periph_display_backlight_on() {
  local hex gs
  cmd_reset_fault >/dev/null 2>&1 || true
  sleep 0.05
  gs="$(cmd_get_status 2>/dev/null)" || true
  if [[ -n "${gs:-}" ]] && expect_state_bits "$gs" 0x07 0 && expect_fault_flags "$gs" "0x0000"; then
    log_info "BACKLIGHT already on"
    printf '%s' "$gs"
    return 0
  fi
  log_info "POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004)"
  hex="$(cmd_power_ctrl 0x0004 0x0004)" || return 1
  expect_ack_status "$hex" 0 || return 1
  sleep "${SEQ_BL_WAIT_SEC:-0.5}"
  gs="$(wait_get_status_state 0x07 0)" || return 1
  parse_get_status_hex "$gs"
  expect_fault_flags "$gs" "0x0000" || return 1
  printf '%s' "$gs"
}

expect_rails_in_range() {
  local hex=$1
  python3 - "$hex" <<'PY'
import os, struct, sys
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
if len(raw) != 31:
    sys.exit(1)
data = raw[3:29]
off = 0
rails = {}
for n in ('v24', 'v12', 'v5', 'v3v3'):
    rails[n] = struct.unpack_from('<H', data, off)[0]
    off += 2
limits = {
    'v12': (int(os.environ['THRESH_V12_MIN_MV']), int(os.environ['THRESH_V12_MAX_MV'])),
    'v5': (int(os.environ['THRESH_V5_MIN_MV']), int(os.environ['THRESH_V5_MAX_MV'])),
    'v3v3': (int(os.environ['THRESH_V3V3_MIN_MV']), int(os.environ['THRESH_V3V3_MAX_MV'])),
}
ok = True
for name, (lo, hi) in limits.items():
    v = rails[name]
    if not (lo <= v <= hi):
        print(f'FAIL: {name}={v} mV not in [{lo},{hi}]', file=sys.stderr)
        ok = False
if ok:
    print('rails OK')
sys.exit(0 if ok else 1)
PY
}

expect_currents_in_window() {
  local hex=$1 min_ma=$2 max_ma=$3 channels_csv=$4
  CHANNELS_CSV="$channels_csv" python3 - "$hex" "$min_ma" "$max_ma" <<'PY'
import os, struct, sys
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
i_min = int(sys.argv[2])
i_max = int(sys.argv[3])
if len(raw) != 31:
    sys.exit(1)
data = raw[3:29]
names = ['i_lcd', 'i_backlight', 'i_scaler', 'i_audio_l', 'i_audio_r']
vals = {}
off = 8
for n in names:
    vals[n] = struct.unpack_from('<h', data, off)[0]
    off += 2
want = [x.strip() for x in os.environ.get('CHANNELS_CSV', '').split(',') if x.strip()]
ok = True
for n in want:
    v = vals.get(n)
    if v is None:
        print(f'FAIL: unknown channel {n}', file=sys.stderr)
        ok = False
        continue
    if not (i_min <= v <= i_max):
        print(f'FAIL: {n}={v} mA not in [{i_min},{i_max}]', file=sys.stderr)
        ok = False
if ok:
    print('currents OK')
sys.exit(0 if ok else 1)
PY
}

fault_set_i_lcd_max_ma() {
  local ma=$1
  local lo=$((ma & 0xff)) hi=$(((ma >> 8) & 0xff))
  cmd_set_thresholds 0x0100 "$lo" "$hi"
}

fault_restore_i_lcd_max() {
  fault_set_i_lcd_max_ma "${THRESH_I_LCD_DEFAULT_MA:-2000}"
}
