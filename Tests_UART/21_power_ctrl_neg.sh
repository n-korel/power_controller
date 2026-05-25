#!/usr/bin/env bash
# POWER_CTRL: bad LEN and unknown domain bits → status 0x01, state unchanged
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
baseline="$(cmd_get_status)" || die "no baseline GET_STATUS"

# Байты — десятичные 0–255: bash съедает пустые аргументы от 0x00
log_info "POWER_CTRL LEN=3"
hex="$(cmd_power_ctrl_len 3 4 0 4)" || die "no response (LEN=3)"
expect_ack_status "$hex" 1 || die "LEN=3: expected status=0x01"

log_info "POWER_CTRL LEN=5"
hex="$(cmd_power_ctrl_len 5 4 0 4 0 0)" || die "no response (LEN=5)"
expect_ack_status "$hex" 1 || die "LEN=5: expected status=0x01"

log_info "POWER_CTRL unknown bit 0x0080"
hex="$(cmd_power_ctrl 0x0080 0x0080)" || die "no response (unknown bits)"
expect_ack_status "$hex" 1 || die "unknown bits: expected status=0x01"

after="$(cmd_get_status)" || die "no GET_STATUS after rejects"
expect_state_unchanged "$baseline" "$after" || die "state changed after rejected POWER_CTRL"

hex="$(cmd_ping)" || die "PING failed"
expect_ping_aa "$hex" && log_pass "POWER_CTRL neg: bad LEN, unknown bits, state unchanged"
