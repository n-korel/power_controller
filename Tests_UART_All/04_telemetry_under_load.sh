#!/usr/bin/env bash
# Блок 2: токи под нагрузкой; при необходимости CALIBRATE_OFFSET при state=0
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "could not enable SCALER+LCD"

# После CALIBRATE_OFFSET и секвенса ON токи в GET_STATUS могут быть 0 несколько опросов (ADC/фильтр).
gs="$(periph_wait_status_load_ma i_lcd "$LOAD_I_MIN_MA" 0x03)" \
  || die "no stable i_lcd under load (waited ${STATE_POLL_TRIES:-40}×${STATE_POLL_INTERVAL_SEC:-0.1}s)"
gs="$(periph_wait_status_load_ma i_scaler "$LOAD_I_MIN_MA" 0x03)" \
  || die "no stable i_scaler under load"
hex="$gs"
parse_get_status_hex "$hex"

export LOAD_I_MIN_MA LOAD_I_MAX_MA LOAD_I_CHANNELS
expect_currents_in_window "$hex" "$LOAD_I_MIN_MA" "$LOAD_I_MAX_MA" "$LOAD_I_CHANNELS" \
  && log_pass "load currents in [${LOAD_I_MIN_MA},${LOAD_I_MAX_MA}] mA for ${LOAD_I_CHANNELS}" \
  || die "currents out of range under load — check calibration and DMM"

log_info "compare with DMM in series if your bench allows"
