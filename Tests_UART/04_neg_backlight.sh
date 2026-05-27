#!/usr/bin/env bash
# Test_firmware C.7 — BACKLIGHT=ON with SCALER/LCD off → status 0x01
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
ensure_clean_state || die "failed to enter clean state before C.7"
baseline="$(cmd_get_status)" || die "no baseline GET_STATUS"
log_info "BACKLIGHT only (mask=0x0004 value=0x0004)"
hex="$(cmd_power_ctrl 0x0004 0x0004)" || die "no POWER_CTRL response"
echo "$hex" | xxd -r -p | xxd
expect_ack_status "$hex" 1 || die "expected status=0x01 (rejected by policy)"
after="$(cmd_get_status)" || die "no GET_STATUS after reject"
parse_get_status_hex "$after"
b_state="$(parse_get_status_hex "$baseline" | awk -F= '/^state=/{print $2}')"
a_state="$(parse_get_status_hex "$after" | awk -F= '/^state=/{print $2}')"
b_fault="$(parse_get_status_hex "$baseline" | awk -F= '/^fault_flags=/{print $2}')"
a_fault="$(parse_get_status_hex "$after" | awk -F= '/^fault_flags=/{print $2}')"
[[ "$b_state" == "$a_state" && "$b_fault" == "$a_fault" ]] \
  && log_pass "C.7: state/fault unchanged" \
  || die "C.7: state or fault changed"
