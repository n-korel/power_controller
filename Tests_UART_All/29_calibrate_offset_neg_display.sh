#!/usr/bin/env bash
# CALIBRATE_OFFSET rejected when display domains are on (state!=0)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap 'periph_display_all_off >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
gs="$(periph_display_scaler_lcd_on)" || die "need SCALER+LCD on"
expect_state_bits "$gs" 0x03 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" || die "expected state=0x03"

log_info "CALIBRATE_OFFSET with display on (state!=0)"
hex="$(cmd_calibrate_offset)" || die "no response"
expect_ack_status "$hex" 1 || die "CALIBRATE_OFFSET: expected status=0x01"
gs="$(cmd_get_status)" || die "no GET_STATUS after reject"
expect_state_bits "$gs" 0x03 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" || die "state changed after reject"

hex="$(cmd_ping)" || die "PING failed"
expect_ping_aa "$hex" && log_pass "CALIBRATE_OFFSET neg: rejected with SCALER+LCD on"
