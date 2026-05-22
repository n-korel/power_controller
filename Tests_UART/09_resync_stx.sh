#!/usr/bin/env bash
# Test_firmware C.20 — truncated GET_STATUS + valid PING
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
log_info "truncated GET_STATUS (02 04 00) + PING"
uart_send_hex "020400"
uart_tx_frame 0x01
sleep 0.2
hex="$(uart_rx 6 "$ACK_TIMEOUT_SEC")" || die "no PING response after truncated frame"
expect_ping_aa "$hex" && log_pass "C.20: STX resync, PING OK"
