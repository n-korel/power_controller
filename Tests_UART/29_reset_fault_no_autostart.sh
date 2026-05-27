#!/usr/bin/env bash
# Fault latch + RESET_FAULT: state must stay 0 (no autostart)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap 'fault_restore_v12_defaults >/dev/null 2>&1 || true; fault_disable_voltage_monitor; cmd_reset_fault >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open
cmd_reset_fault >/dev/null 2>&1 || true
sleep 0.1

fault_enable_voltage_monitor || die "failed to enable TOUCH for voltage monitoring"

hex="$(fault_trigger_v12_range)" || die "SET_THRESHOLDS trap no response"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS trap: expected status=0x00"

gs="$(fault_wait_flags 0400 "${FAULT_WAIT_TRIES:-30}")" || die "expected fault_flags with FAULT_V12_RANGE"
expect_state_bits "$gs" 0 0xff || die "expected safe state=0 after fault trap"

hex="$(cmd_reset_fault)" || die "no RESET_FAULT response"
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"

gs="$(cmd_get_status)" || die "no GET_STATUS after RESET_FAULT"
expect_get_status_clean "$gs" || die "expected state=0x00 and fault=0x0000 after RESET_FAULT"

hex="$(cmd_ping)" || die "PING failed"
expect_ping_aa "$hex" && log_pass "RESET_FAULT clears latch and does not autostart outputs"
