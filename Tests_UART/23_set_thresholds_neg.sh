#!/usr/bin/env bash
# SET_THRESHOLDS: invalid mask, min>=max, truncated payload → status 0x01
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open

expect_reject() {
  local label=$1
  local hex=$2
  expect_ack_status "$hex" 1 || die "${label}: expected status=0x01"
  log_info "${label}: rejected OK"
}

hex="$(cmd_set_thresholds 0x8000 0x00 0x00)" || die "no response (unknown mask)"
expect_reject "unknown mask bit 0x8000" "$hex"

hex="$(cmd_set_thresholds 0x0002 0x10 0x36 0x10 0x27)" || die "no response (min>=max)"
expect_reject "V12 min>=max" "$hex"

uart_drain_fd
uart_tx_frame_len 0x07 4 0x02 0x00 0xC8 0x32
sleep "${SET_THRESH_TX_DELAY_SEC:-0.2}"
hex="$(uart_rx "$ACK_FRAME_LEN" "$ACK_TIMEOUT_SEC")" || die "no response (truncated V12)"
expect_reject "V12 mask truncated" "$hex"

hex="$(cmd_ping)" || die "PING failed"
expect_ping_aa "$hex" && log_pass "SET_THRESHOLDS neg: invalid mask, min>=max, truncated"
