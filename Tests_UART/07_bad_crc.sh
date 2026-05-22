#!/usr/bin/env bash
# Test_firmware C.3 — bad CRC → no response; then PING recovers
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
log_info "GET_STATUS with invalid CRC (FF)"
uart_send_hex "020400ff03"
sleep 0.25
if uart_rx 16 "$NO_RESPONSE_TIMEOUT_SEC" >/dev/null 2>&1; then
  die "got response to bad CRC frame — unexpected"
fi
log_pass "bad CRC: no response"
hex="$(cmd_ping)" || die "PING after bad CRC failed"
expect_ping_aa "$hex" && log_pass "C.3: link recovered via PING"
