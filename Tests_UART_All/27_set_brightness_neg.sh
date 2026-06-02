#!/usr/bin/env bash
# SET_BRIGHTNESS bad LEN / pwm>1000 with BACKLIGHT on
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "need SCALER+LCD"
gs="$(periph_display_backlight_on)" || die "need BACKLIGHT on"
expect_state_bits "$gs" 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" || die "expected state=0x07"

hex="$(cmd_set_brightness_len 1 232)" || die "no response (LEN=1)"
expect_ack_status "$hex" 1 || die "SET_BRIGHTNESS LEN=1: expected status=0x01"

hex="$(cmd_set_brightness 1001)" || die "no response (pwm=1001)"
expect_ack_status "$hex" 1 || die "SET_BRIGHTNESS pwm=1001: expected status=0x01"

hex="$(cmd_ping)" || die "PING after rejects failed"
expect_ping_aa "$hex" && log_pass "SET_BRIGHTNESS neg (load): bad LEN, pwm>1000 rejected"
