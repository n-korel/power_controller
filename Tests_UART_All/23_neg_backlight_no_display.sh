#!/usr/bin/env bash
# C.7 with peripheral bench: BACKLIGHT only while SCALER/LCD off → status 0x01
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
baseline="$(cmd_get_status)" || die "no baseline GET_STATUS"

log_info "BACKLIGHT only (mask=0x0004 value=0x0004)"
hex="$(cmd_power_ctrl 0x0004 0x0004)" || die "no POWER_CTRL response"
expect_ack_status "$hex" 1 || die "expected status=0x01 (rejected by policy)"
after="$(cmd_get_status)" || die "no GET_STATUS after reject"
expect_state_unchanged "$baseline" "$after" && log_pass "C.7 periph: BACKLIGHT-only rejected, state/fault unchanged" \
  || die "C.7: state or fault changed"
