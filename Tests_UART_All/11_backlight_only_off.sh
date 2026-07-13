#!/usr/bin/env bash
# Блок 1: BL OFF при активных SCALER+LCD не должен гасить остальной display state
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_backlight_on >/dev/null || die "setup: BACKLIGHT ON"

log_info "POWER_CTRL BACKLIGHT OFF only (mask=0x0004 value=0x0000)"
hex="$(cmd_power_ctrl 0x0004 0x0000)" || die "no ACK"
expect_ack_status "$hex" 0 || die "BACKLIGHT OFF only: expected status=0x00"
sleep "${SEQ_DN_WAIT_SEC:-1.0}"

gs="$(wait_get_status_state 0x03 0x4c "${STATE_POLL_TRIES:-40}")" || die "expected state=0x03 (SCALER+LCD ON, BL OFF)"
expect_fault_flags "$gs" "0x0000" || die "expected fault=0 after BL OFF only"

log_pass "BL OFF only: state=0x03 preserved, SCALER/LCD stay on"
