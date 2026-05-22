#!/usr/bin/env bash
# Test_firmware C.8 — LCD=ON without SCALER → status 0x01
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
baseline="$(cmd_get_status)" || die "no baseline GET_STATUS"
log_info "LCD without SCALER (mask=0x0002 value=0x0002)"
hex="$(cmd_power_ctrl 0x0002 0x0002)" || die "no response"
expect_ack_status "$hex" 1 || die "expected status=0x01"
after="$(cmd_get_status)" || die "no GET_STATUS"
b_state="$(parse_get_status_hex "$baseline" | awk -F= '/^state=/{print $2}')"
a_state="$(parse_get_status_hex "$after" | awk -F= '/^state=/{print $2}')"
[[ "$b_state" == "$a_state" ]] && log_pass "C.8: state unchanged" || die "C.8: state changed"
