#!/usr/bin/env bash
# CALIBRATE_OFFSET: reject when state != 0
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

cleanup() {
  cmd_power_ctrl 0x0040 0x0000 >/dev/null 2>&1 || true
  test_cleanup
}

trap cleanup EXIT
uart_open

log_info "TOUCH ON (state must become non-zero)"
hex="$(cmd_power_ctrl 0x0040 0x0040)" || die "no ACK (TOUCH ON)"
expect_ack_status "$hex" 0 || die "TOUCH ON: expected status=0x00"
before="$(cmd_get_status)" || die "no GET_STATUS after TOUCH ON"
expect_state_bits "$before" 0x40 0 || die "expected TOUCH bit set before CALIBRATE_OFFSET"

log_info "CALIBRATE_OFFSET with state!=0"
hex="$(cmd_calibrate_offset)" || die "no response (CALIBRATE_OFFSET)"
expect_ack_status "$hex" 1 || die "CALIBRATE_OFFSET state!=0: expected status=0x01"

after="$(cmd_get_status)" || die "no GET_STATUS after CALIBRATE_OFFSET reject"
expect_state_bits "$after" 0x40 0 || die "state changed unexpectedly after CALIBRATE_OFFSET reject"

hex="$(cmd_ping)" || die "PING failed"
expect_ping_aa "$hex" && log_pass "CALIBRATE_OFFSET neg: rejected when state!=0"
