#!/usr/bin/env bash
# Блок 1: Границы SET_BRIGHTNESS внутри диапазона (1 и 999)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap 'cmd_set_brightness "$BRIGHTNESS_MIN" >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_backlight_on >/dev/null || die "setup: BACKLIGHT ON"

for pwm in 1 999; do
  log_info "SET_BRIGHTNESS pwm=${pwm}"
  hex="$(cmd_set_brightness "$pwm")" || die "no ACK for pwm=${pwm}"
  expect_ack_status "$hex" 0 || die "SET_BRIGHTNESS pwm=${pwm}: expected status=0x00"
  gs="$(cmd_get_status)" || die "no GET_STATUS after pwm=${pwm}"
  expect_state_bits "$gs" 0x07 "${PERIPH_PREP_NONDISPLAY_MASK_HEX}" || die "display state changed unexpectedly after pwm=${pwm}"
  expect_fault_flags "$gs" "0x0000" || die "fault set unexpectedly after pwm=${pwm}"
done

log_info "manual check: PWM duty on PB9 should be neither 0%% nor 100%% at pwm=1/999"
log_pass "SET_BRIGHTNESS boundary (1,999): status=0x00, display state stable"
