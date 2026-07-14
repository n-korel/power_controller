#!/usr/bin/env bash
# SET_BRIGHTNESS with SCALER+LCD on, BACKLIGHT off: ACK OK, display bits stay 0x03
# (GET_STATUS.state includes always-on ETH → raw often 0x33, not literal 0x03)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
gs="$(periph_display_scaler_lcd_on)" || die "need SCALER+LCD"
if expect_state_bits "$gs" 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX"; then
  log_info "BACKLIGHT off for no-BL test (auto-start may have left BL on)" >&2
  hex="$(cmd_power_ctrl 0x0004 0x0000)" || die "BACKLIGHT OFF failed"
  expect_ack_status "$hex" 0 || die "BACKLIGHT OFF: expected status=0x00"
  sleep "${SEQ_BL_WAIT_SEC:-1.0}"
  gs="$(wait_get_status_state 0x03 "$PERIPH_PREP_NONDISPLAY_MASK_HEX")" || die "expected state=0x03 after BL OFF"
fi
expect_state_bits "$gs" 0x03 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" \
  || die "expected SCALER+LCD on, BL bit clear (state bits 0x03)"

log_info "SET_BRIGHTNESS pwm=500 with BACKLIGHT off (buffers PWM until BL ON)"
hex="$(cmd_set_brightness 500)" || die "no ACK"
expect_ack_status "$hex" 0 || die "SET_BRIGHTNESS: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS"
expect_state_bits "$gs" 0x03 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" \
  || die "state must keep SCALER+LCD, no BL bit"
log_pass "SET_BRIGHTNESS with BL off: ACK OK, state bits 0x03 unchanged"
