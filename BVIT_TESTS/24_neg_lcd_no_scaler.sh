#!/bin/sh
# C.8: LCD ON without SCALER → status 0x01
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
baseline="$(cmd_get_status)" || die "no baseline GET_STATUS"

log_info "LCD without SCALER (mask=0x0002 value=0x0002)"
hex="$(cmd_power_ctrl 0x0002 0x0002)" || die "no response"
expect_ack_status "$hex" 1 || die "expected status=0x01"
after="$(cmd_get_status)" || die "no GET_STATUS"
expect_state_unchanged "$baseline" "$after" && log_pass "C.8 periph: LCD-without-SCALER rejected" \
  || die "C.8: state changed"
