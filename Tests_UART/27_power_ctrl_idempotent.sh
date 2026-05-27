#!/usr/bin/env bash
# POWER_CTRL idempotency: TOUCH ON twice must keep state stable
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap 'cmd_power_ctrl 0x0040 0x0000 >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open
cmd_reset_fault >/dev/null 2>&1 || true
sleep 0.1

log_info "TOUCH ON (first)"
hex="$(cmd_power_ctrl 0x0040 0x0040)" || die "no ACK (first TOUCH ON)"
expect_ack_status "$hex" 0 || die "first TOUCH ON: expected status=0x00"
first="$(cmd_get_status)" || die "no GET_STATUS after first TOUCH ON"
expect_state_bits "$first" 0x40 0 || die "expected TOUCH bit after first TOUCH ON"

log_info "TOUCH ON (second, idempotent)"
hex="$(cmd_power_ctrl 0x0040 0x0040)" || die "no ACK (second TOUCH ON)"
expect_ack_status "$hex" 0 || die "second TOUCH ON: expected status=0x00"
second="$(cmd_get_status)" || die "no GET_STATUS after second TOUCH ON"
expect_state_unchanged "$first" "$second" || die "state changed on idempotent TOUCH ON"

hex="$(cmd_ping)" || die "PING failed"
expect_ping_aa "$hex" && log_pass "POWER_CTRL idempotent: repeated TOUCH ON keeps state stable"
