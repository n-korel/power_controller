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
    08_display_shutdown.sh|09_fault_lcd_current.sh|11_backlight_only_off.sh|\
    12_all_at_once_up.sh|13_fault_recovery_display.sh|14_set_brightness_boundary.sh|\
    15_display_resequence.sh) return 0 ;;
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
  log_info "--- GET_STATUS (${tag}) ---" >&2
  parse_get_status_hex "$hex" >&2 || true
}

periph_currents_near_zero() {
  local hex=$1
  export TELEMETRY_I_ZERO_MIN_MA TELEMETRY_I_ZERO_MAX_MA
  expect_currents_in_window "$hex" "$TELEMETRY_I_ZERO_MIN_MA" "$TELEMETRY_I_ZERO_MAX_MA" \
    "${TELEMETRY_I_CHANNELS:-i_lcd,i_scaler,i_audio_l,i_audio_r}"
}

# state=0, сброс fault, CALIBRATE_OFFSET при «завышенных» токах (типично без калибровки во flash)
periph_prepare_zero_load() {
  local hex gs
  cmd_reset_fault >/dev/null 2>&1 || true
  sleep 0.1
  periph_all_domains_off || return 1
  sleep 0.2
  gs="$(cmd_get_status)" || return 1
  # Важный нюанс: при отсутствии калибровки во flash токи на state=0 могут быть далеко от нуля,
  # и expect_currents_in_window печатает "FAIL: ..." как часть диагностики. Здесь это ожидаемо —
  # мы сразу запускаем CALIBRATE_OFFSET. Поэтому проверяем "тихо" и не шумим "FAIL" в логе.
  if periph_currents_near_zero "$gs" >/dev/null 2>&1; then
    log_info "current offsets OK (±${TELEMETRY_I_ZERO_MAX_MA} mA at state=0)"
    return 0
  fi
  periph_log_status "$gs" "before calibrate"
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
  local hex gs
  log_info "all domains off: mask=0x007F value=0"
  hex="$(cmd_power_ctrl 0x007f 0x0000)" || return 1
  expect_ack_status "$hex" 0 || return 1
  sleep "${SEQ_DN_WAIT_SEC:-1.0}"
  if ! gs="$(wait_get_status_clean "${STATE_POLL_TRIES:-40}")"; then
    log_fail "all domains off: timeout waiting state=0 fault=0"
    return 1
  fi
  if ! expect_state_bits "$gs" 0x00 0; then
    periph_log_status "$gs" "all domains off"
    log_fail "all domains off: state!=0x00 (e.g. 0x4B auto-startup tail — retry RESET_FAULT)"
    return 1
  fi
}

# Снять AUDIO|TOUCH|ETH, не трогая SCALER|LCD|BL (если остался хвост 0x4B)
# state=0x4B (SCALER|LCD|AUDIO|TOUCH) — типичный хвост §6.5 или IWDG reset
periph_state_is_autostart_tail() {
  local hex=$1
  python3 - "$hex" <<'PY'
import sys
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
if len(raw) != 42:
    sys.exit(1)
sys.exit(0 if raw[25] == 0x4B else 1)
PY
}

periph_strip_nondisplay_domains() {
  local hex gs
  gs="$(cmd_get_status 2>/dev/null)" || return 1
  if expect_state_bits "$gs" 0x00 "$PERIPH_PREP_NONDISPLAY_MASK_HEX"; then
    return 0
  fi
  log_info "clear non-display domains (mask=0x78 value=0)" >&2
  hex="$(cmd_power_ctrl 0x0078 0x0000)" || return 1
  expect_ack_status "$hex" 0 || return 1
  sleep "${AUDIO_SEQ_WAIT_SEC:-0.25}"
  if ! gs="$(wait_get_status_state 0x00 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" 20)"; then
    gs="$(cmd_get_status 2>/dev/null)" || gs=""
    [[ -n "$gs" ]] && periph_log_status "$gs" "strip non-display failed"
    return 1
  fi
}

periph_display_scaler_lcd_on() {
  local hex gs
  cmd_reset_fault >/dev/null 2>&1 || true
  sleep 0.05
  gs="$(cmd_get_status 2>/dev/null)" || true
  # Явно исключаем «хвосты» доменов: при state=0x4B здесь уже true для бит 0–1 —
  # без clear-mask тест ошибочно думает, что включены только SCALER+LCD.
  if [[ -n "${gs:-}" ]] && expect_state_bits "$gs" 0x03 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" \
    && expect_fault_flags "$gs" "0x0000"; then
    log_info "SCALER+LCD already on" >&2
    printf '%s' "$gs"
    return 0
  fi
  log_info "POWER_CTRL SCALER+LCD ON (mask=0x0003 value=0x0003)" >&2
  hex="$(cmd_power_ctrl 0x0003 0x0003)" || return 1
  expect_ack_status "$hex" 0 || return 1
  sleep "${SEQ_ON_WAIT_SEC:-2.0}"
  if ! gs="$(wait_get_status_state 0x03 "${PERIPH_PREP_NONDISPLAY_MASK_HEX}")"; then
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
  local hex gs attempt ack_status max_attempts
  max_attempts="${PERIPH_BACKLIGHT_ON_TRIES:-3}"
  cmd_reset_fault >/dev/null 2>&1 || true
  sleep 0.05
  periph_strip_nondisplay_domains || return 1
  gs="$(cmd_get_status 2>/dev/null)" || true
  if [[ -n "${gs:-}" ]] && expect_state_bits "$gs" 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" \
    && expect_fault_flags "$gs" "0x0000"; then
    log_info "BACKLIGHT already on" >&2
    printf '%s' "$gs"
    return 0
  fi
  for ((attempt = 1; attempt <= max_attempts; attempt++)); do
    cmd_reset_fault >/dev/null 2>&1 || true
    sleep 0.05
    # После fault_policy state может быть 0x00: перед BACKLIGHT поднимаем SCALER+LCD заново.
    gs="$(periph_display_scaler_lcd_on)" || {
      log_info "BACKLIGHT precondition failed: SCALER+LCD not ready (attempt ${attempt}/${max_attempts})" >&2
      continue
    }
    periph_strip_nondisplay_domains || continue
    gs="$(cmd_get_status 2>/dev/null)" || gs=""
    if [[ -n "$gs" ]] && ! expect_state_bits "$gs" 0x03 "$PERIPH_PREP_NONDISPLAY_MASK_HEX"; then
      log_info "pre-BL: need clean SCALER+LCD (state=0x03), got unexpected state" >&2
      periph_log_status "$gs" "pre-BL state"
      continue
    fi
    log_info "POWER_CTRL BACKLIGHT ON (mask=0x0004 value=0x0004), attempt ${attempt}/${max_attempts}" >&2
    hex="$(cmd_power_ctrl 0x0004 0x0004)" || continue
    ack_status="$(python3 - "$hex" <<'PY'
import sys
b = bytes.fromhex(sys.argv[1].replace(' ', ''))
print(b[3] if len(b) >= 4 else 255)
PY
)"
    if [[ "$ack_status" != "0" ]]; then
      log_info "BACKLIGHT ON rejected: ACK status=${ack_status} (attempt ${attempt}/${max_attempts})" >&2
      sleep 0.15
      continue
    fi
    sleep "${SEQ_BL_WAIT_SEC:-1.0}"
    gs="$(wait_get_status_state 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX")" || gs=""
    if [[ -n "$gs" ]]; then
      parse_get_status_hex "$gs" >&2 || true
      if expect_fault_flags "$gs" "0x0000"; then
        printf '%s' "$gs"
        return 0
      fi
    fi
    gs="$(cmd_get_status 2>/dev/null)" || gs=""
    if [[ -n "$gs" ]]; then
      periph_log_status "$gs" "BACKLIGHT ON attempt ${attempt} failed"
      if periph_state_is_autostart_tail "$gs"; then
        log_info "state=0x4B: auto-startup tail or MCU reset — full re-prepare" >&2
        periph_all_domains_off || continue
        sleep 0.2
        cmd_reset_fault >/dev/null 2>&1 || true
        periph_prepare_zero_load || continue
      fi
    fi
    sleep 0.1
  done
  return 1
}

cmd_set_thresholds_retry() {
  local mask=$1
  shift
  local attempt hex
  for attempt in 1 2 3; do
    uart_drain_fd
    if hex="$(cmd_set_thresholds "$mask" "$@")"; then
      printf '%s' "$hex"
      return 0
    fi
    sleep 0.2
  done
  return 1
}

expect_rails_in_range() {
  local hex=$1
  python3 - "$hex" <<'PY'
import os, struct, sys
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
if len(raw) != 42:
    sys.exit(1)
data = raw[3:40]
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
if len(raw) != 42:
    sys.exit(1)
data = raw[3:40]
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
  cmd_set_thresholds_retry 0x0100 "$lo" "$hi"
}

fault_restore_i_lcd_max() {
  fault_set_i_lcd_max_ma "${THRESH_I_LCD_DEFAULT_MA:-2000}"
}
