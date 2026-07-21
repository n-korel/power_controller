#!/bin/sh
# Блок 2: токи под нагрузкой; при необходимости CALIBRATE_OFFSET при state=0
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "could not enable SCALER+LCD"

# Scaler current is the reliable under-load signature on this revision.
gs="$(periph_wait_status_load_ma i_scaler "$LOAD_I_MIN_MA" 0x03)" \
  || die "no stable i_scaler under load (waited ${STATE_POLL_TRIES:-40}×${STATE_POLL_INTERVAL_SEC:-0.1}s)"
hex="$gs"
parse_get_status_hex "$hex"

export LOAD_I_MIN_MA LOAD_I_MAX_MA LOAD_I_CHANNELS
expect_currents_in_window "$hex" "$LOAD_I_MIN_MA" "$LOAD_I_MAX_MA" "$LOAD_I_CHANNELS" \
  && log_pass "load currents in [${LOAD_I_MIN_MA},${LOAD_I_MAX_MA}] mA for ${LOAD_I_CHANNELS}" \
  || die "currents out of range under load — check calibration and DMM"

# Soft bound for i_lcd (often near-zero on this panel/sense path).
_ilcd="$(hex_i16le "$hex" 11)"
if [ "$_ilcd" -lt "${LOAD_I_LCD_MIN_MA:-0}" ] || [ "$_ilcd" -gt "${LOAD_I_LCD_MAX_MA:-3200}" ]; then
  die "i_lcd=${_ilcd} mA not in [${LOAD_I_LCD_MIN_MA:-0},${LOAD_I_LCD_MAX_MA:-3200}]"
fi

log_info "compare with DMM in series if your bench allows"
