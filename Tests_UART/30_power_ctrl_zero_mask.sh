#!/usr/bin/env bash
# POWER_CTRL no-op: mask=0, value=0 must keep state unchanged
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open

baseline="$(cmd_get_status)" || die "no baseline GET_STATUS"

hex="$(cmd_power_ctrl 0x0000 0x0000)" || die "no response (POWER_CTRL mask=0)"
expect_ack_status "$hex" 0 || die "POWER_CTRL mask=0: expected status=0x00"

after="$(cmd_get_status)" || die "no GET_STATUS after POWER_CTRL no-op"
expect_state_unchanged "$baseline" "$after" || die "state changed after POWER_CTRL mask=0 no-op"

hex="$(cmd_ping)" || die "PING failed"
expect_ping_aa "$hex" && log_pass "POWER_CTRL no-op (mask=0) keeps state unchanged"
