#!/usr/bin/env bash
# Блок 1: BACKLIGHT ON + SET_BRIGHTNESS 500/1000/0
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "SCALER+LCD must be on before BACKLIGHT"

gs="$(periph_display_backlight_on)" || die "BACKLIGHT ON failed"
log_info "BACKLIGHT on: check ~12V on BACKLIGHT_POWER_M (PB0) with DMM/LA"

for pwm in "$BRIGHTNESS_MID" "$BRIGHTNESS_MAX" "$BRIGHTNESS_MIN"; do
  log_info "SET_BRIGHTNESS pwm=${pwm}"
  hex="$(cmd_set_brightness "$pwm")" || die "no ACK for pwm=${pwm}"
  expect_ack_status "$hex" 0 || die "SET_BRIGHTNESS pwm=${pwm}: expected status=0x00"
  sleep 0.1
done

log_info "visual: ~50%% at pwm=${BRIGHTNESS_MID}, max at ${BRIGHTNESS_MAX}, min at ${BRIGHTNESS_MIN} (BL GPIO stays on)"
log_pass "BACKLIGHT ON + SET_BRIGHTNESS ${BRIGHTNESS_MID}/${BRIGHTNESS_MAX}/${BRIGHTNESS_MIN} ACK OK"
