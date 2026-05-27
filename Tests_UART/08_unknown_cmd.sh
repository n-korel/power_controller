#!/usr/bin/env bash
# Test_firmware C.4 — CMD=0xFF (NACK opcode)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
log_info "CMD=0xFF"
uart_tx_frame 0xFF
sleep 0.2
hex=""
if hex="$(uart_rx 8 0.3)"; then
  echo "$hex" | xxd -r -p | xxd
  log_info "got NACK or response (see CMD=0xFF, error_code)"
else
  log_info "silence — acceptable per README"
fi
hex2="$(cmd_ping)" || die "PING after unknown CMD failed"
expect_ping_aa "$hex2" && log_pass "C.4: link alive after 0xFF (PING OK)"
