#!/usr/bin/env bash
# Блок 1: POWER_CTRL одним кадром поднимает SCALER+LCD+BL из state=0
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"

log_info "POWER_CTRL display all ON (mask=0x0007 value=0x0007)"
hex="$(cmd_power_ctrl 0x0007 0x0007)" || die "no ACK"
expect_ack_status "$hex" 0 || die "display all ON: expected status=0x00"
sleep "${SEQ_BL_WAIT_SEC:-2.0}"

gs="$(wait_get_status_state 0x07 "${PERIPH_PREP_NONDISPLAY_MASK_HEX}" "${STATE_POLL_TRIES:-40}")" || die "expected state=0x07 after all-at-once UP"
expect_fault_flags "$gs" "0x0000" || die "expected fault=0 after all-at-once UP"

log_pass "display all-at-once UP: state=0x07, fault=0"
