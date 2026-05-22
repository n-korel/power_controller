#!/usr/bin/env bash
# Test_firmware C.11 — RESET_FAULT
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
log_info "RESET_FAULT"
hex="$(cmd_reset_fault)" || die "no response"
echo "$hex" | xxd -r -p | xxd
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"
log_pass "RESET_FAULT ACK OK"
hex2="$(cmd_get_status)" || die "no GET_STATUS after RESET_FAULT"
parse_get_status_hex "$hex2"
expect_get_status_clean "$hex2" && log_pass "fault_flags=0, state=0 (loads did not turn on)"
