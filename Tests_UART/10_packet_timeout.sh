#!/usr/bin/env bash
# Test_firmware C.18 — incomplete frame, pause >50 ms, then PING
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
log_info "STX+CMD+LEN without CRC/ETX, pause ${PACKET_TIMEOUT_PAUSE_MS} ms"
uart_send_hex "020400"
sleep "$(python3 -c "print(${PACKET_TIMEOUT_PAUSE_MS}/1000)")"
hex="$(cmd_ping)" || die "PING after packet timeout failed"
expect_ping_aa "$hex" && log_pass "C.18: incomplete frame dropped, PING OK"
