#!/usr/bin/env bash
# Test_firmware C.1 — PING → status 0xAA
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
log_info "PING on ${UART_DEVICE}"
hex="$(cmd_ping)" || die "no response to PING"
echo "$hex" | xxd -r -p | xxd
if expect_ping_aa "$hex"; then
  log_pass "PING: status=0xAA"
else
  die "PING: expected 0xAA in DATA[0]"
fi
