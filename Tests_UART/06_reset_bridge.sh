#!/usr/bin/env bash
# Test_firmware C.12 — RESET_BRIDGE → ACK 0x00
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
ensure_clean_state || die "failed to enter clean state before C.12"
log_info "RESET_BRIDGE (~10 ms LOW on PB8 — verify with LA)"
hex="$(cmd_reset_bridge)" || die "no response"
echo "$hex" | xxd -r -p | xxd
if expect_ack_status "$hex" 0; then
  log_pass "RESET_BRIDGE: status=0x00"
elif expect_ack_status "$hex" 1; then
  log_pass "RESET_BRIDGE: status=0x01 when display domains are OFF"
else
  die "unexpected RESET_BRIDGE status"
fi
