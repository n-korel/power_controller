#!/bin/sh
# Manual diag (not in run_all): catch i_audio_l/r + fault timeline right after AUDIO ON.
#
# Normal cmd_power_ctrl sleeps POWER_CTRL_TX_DELAY_SEC (0.25s) after ACK — that hides
# the FAULT_AUDIO latch window. This script ACKs without settle delay and polls
# GET_STATUS as fast as socat allows.
#
# Usage on Q7:
#   ./36_audio_on_current_diag.sh
#   AUDIO_DIAG_WITH_DISPLAY=1 ./36_audio_on_current_diag.sh
#   AUDIO_DIAG_SAMPLES=60 AUDIO_DIAG_GAP_SEC=0.02 ./36_audio_on_current_diag.sh
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

SAMPLES="${AUDIO_DIAG_SAMPLES:-40}"
GAP_SEC="${AUDIO_DIAG_GAP_SEC:-0.05}"
XFER_SEC="${AUDIO_DIAG_XFER_SEC:-0.25}"
FAULT_AUDIO_BIT="${FAULT_AUDIO_FLAG:-0x0008}"
FAULT_FAULTZ_BIT="${FAULT_AMP_FAULTZ_FLAG:-0x0100}"

_abs_ma() {
  if [ "$1" -lt 0 ]; then
    printf '%d' "$((0 - $1))"
  else
    printf '%d' "$1"
  fi
}

# POWER_CTRL without post-ACK settle delay (keeps the early current window).
diag_power_ctrl_nodelay() {
  _mask="$(_to_dec "$1")"
  _value="$(_to_dec "$2")"
  _ml=$((_mask & 255))
  _mh=$(( (_mask >> 8) & 255 ))
  _vl=$((_value & 255))
  _vh=$(( (_value >> 8) & 255 ))
  uart_flush
  _hex="$(uart_emit_frame 0x02 "$_ml" "$_mh" "$_vl" "$_vh" | uart_xfer "$ACK_TIMEOUT_SEC")" || return 1
  validate_frame_crc "$_hex" || return 1
  [ "$(hex_byte "$_hex" 0)" -eq 2 ] || return 1
  [ "$(hex_byte "$_hex" 1)" -eq 2 ] || return 1
  [ "$(hex_byte "$_hex" 2)" -eq 1 ] || return 1
  [ "$(hex_byte "$_hex" 5)" -eq 3 ] || return 1
  printf '%s' "$_hex"
}

# Single GET_STATUS without retry/flush (caller flushes once before the burst).
diag_get_status_fast() {
  _hex="$(uart_emit_frame 0x04 | uart_xfer "$XFER_SEC")" || return 1
  validate_get_status_hex "$_hex" || return 1
  validate_frame_crc "$_hex" || return 1
  printf '%s' "$_hex"
}

diag_cleanup() {
  cmd_reset_fault >/dev/null 2>&1 || true
  cmd_power_ctrl 0x007f 0x0000 >/dev/null 2>&1 || true
  test_cleanup
}

trap diag_cleanup EXIT
uart_open

if [ "${PERIPH_AUDIO_HW_ENABLED:-0}" = "0" ]; then
  log_skip "AUDIO current diag: PERIPH_AUDIO_HW_ENABLED=0"
  exit 0
fi

periph_prepare_zero_load || die "prepare failed (need CALIBRATE_OFFSET before AUDIO)"

if [ "${AUDIO_DIAG_WITH_DISPLAY:-0}" = "1" ]; then
  log_info "precondition: SCALER+LCD ON (AUDIO_DIAG_WITH_DISPLAY=1)"
  periph_display_scaler_lcd_on >/dev/null || die "need SCALER+LCD before AUDIO"
fi

gs0="$(cmd_get_status)" || die "no GET_STATUS before AUDIO ON"
_il0="$(hex_i16le "$gs0" 17)"
_ir0="$(hex_i16le "$gs0" 19)"
_st0="$(hex_byte "$gs0" 21)"
_ft0="$(hex_u16le "$gs0" 22)"
log_info "before AUDIO ON: state=0x$(printf '%02x' "$_st0") fault=0x$(printf '%04x' "$_ft0") i_l=${_il0} i_r=${_ir0}"

log_info "POWER_CTRL AUDIO ON (no settle delay), then ${SAMPLES}x GET_STATUS gap=${GAP_SEC}s"
hex="$(diag_power_ctrl_nodelay 0x0008 0x0008)" || die "no ACK (AUDIO ON)"
expect_ack_status "$hex" 0 || die "AUDIO ON: expected status=0x00 (got $(hex_byte "$hex" 3))"

peak_l=0
peak_r=0
peak_l_s=0
peak_r_s=0
ok=0
dom_audio_samples=0
first_fault_n=""
first_fault_flags=""
first_audio_fault_n=""
first_faultz_n=""
last_state=""
last_fault=""

printf '%s\n' "#n state fault i_audio_l i_audio_r inputs"

n=1
while [ "$n" -le "$SAMPLES" ]; do
  gs="$(diag_get_status_fast)" || {
    log_info "sample ${n}: GET_STATUS failed (xfer)"
    n=$((n + 1))
    [ "$GAP_SEC" != 0 ] && sleep "$GAP_SEC"
    continue
  }
  ok=$((ok + 1))

  il="$(hex_i16le "$gs" 17)"
  ir="$(hex_i16le "$gs" 19)"
  st="$(hex_byte "$gs" 21)"
  ft="$(hex_u16le "$gs" 22)"
  inp="$(hex_byte "$gs" 24)"
  last_state="$st"
  last_fault="$ft"

  printf '%d 0x%02x 0x%04x %d %d 0x%02x\n' "$n" "$st" "$ft" "$il" "$ir" "$inp"

  if [ "$((st & 8))" -eq 8 ]; then
    dom_audio_samples=$((dom_audio_samples + 1))
  fi

  abl="$(_abs_ma "$il")"
  abr="$(_abs_ma "$ir")"
  if [ "$abl" -gt "$peak_l" ]; then
    peak_l="$abl"
    peak_l_s="$il"
  fi
  if [ "$abr" -gt "$peak_r" ]; then
    peak_r="$abr"
    peak_r_s="$ir"
  fi

  if [ -z "$first_fault_n" ] && [ "$ft" -ne 0 ]; then
    first_fault_n="$n"
    first_fault_flags="$ft"
  fi
  if [ -z "$first_audio_fault_n" ] && [ "$((ft & $(_to_dec "$FAULT_AUDIO_BIT")))" -ne 0 ]; then
    first_audio_fault_n="$n"
  fi
  if [ -z "$first_faultz_n" ] && [ "$((ft & $(_to_dec "$FAULT_FAULTZ_BIT")))" -ne 0 ]; then
    first_faultz_n="$n"
  fi

  n=$((n + 1))
  if [ "$n" -le "$SAMPLES" ] && [ "$GAP_SEC" != 0 ]; then
    sleep "$GAP_SEC"
  fi
done

log_info "--- summary ---"
log_info "samples_ok=${ok}/${SAMPLES} dom_audio_seen=${dom_audio_samples}"
log_info "peak |i_audio_l|=${peak_l} mA (signed ${peak_l_s}), |i_audio_r|=${peak_r} mA (signed ${peak_r_s})"
log_info "last state=0x$(printf '%02x' "${last_state:-0}") fault=0x$(printf '%04x' "${last_fault:-0}")"
log_info "thresh I_AUDIO default=${THRESH_I_AUDIO_DEFAULT_MA:-800} mA"

if [ -n "$first_fault_n" ]; then
  log_info "first any-fault: sample ${first_fault_n} fault=0x$(printf '%04x' "$first_fault_flags")"
else
  log_info "first any-fault: none in window"
fi
if [ -n "$first_audio_fault_n" ]; then
  log_info "first FAULT_AUDIO (0x0008): sample ${first_audio_fault_n}"
else
  log_info "first FAULT_AUDIO (0x0008): none"
fi
if [ -n "$first_faultz_n" ]; then
  log_info "first FAULT_AMP_FAULTZ (0x0100): sample ${first_faultz_n}"
else
  log_info "first FAULT_AMP_FAULTZ (0x0100): none"
fi

if [ -n "$first_audio_fault_n" ]; then
  log_info "VERDICT: FAULT_AUDIO latched (current path); compare peaks vs ${THRESH_I_AUDIO_DEFAULT_MA:-800} mA"
elif [ -n "$first_faultz_n" ]; then
  log_info "VERDICT: FAULT_AMP_FAULTZ latched (Faultz pin), not I_AUDIO threshold"
elif [ "$dom_audio_samples" -gt 0 ] && [ "$(( ${last_state:-0} & 8 ))" -eq 8 ]; then
  log_info "VERDICT: AUDIO stayed on in window, no fault — re-run longer or with AUDIO_DIAG_WITH_DISPLAY=1"
elif [ "$dom_audio_samples" -gt 0 ]; then
  log_info "VERDICT: DOM_AUDIO appeared then cleared without latched FAULT_* in polled frames (race or other fault)"
else
  log_info "VERDICT: DOM_AUDIO never seen — ON rejected, delayed, or fault before first sample"
fi

log_pass "AUDIO ON current diag done (${ok} frames)"
