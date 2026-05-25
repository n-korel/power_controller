#!/usr/bin/env bash
# Test_firmware F.1 — SET_THRESHOLDS narrows V12 → FAULT_V12_RANGE (needs state!=0)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
cmd_reset_fault >/dev/null 2>&1 || true
sleep 0.1

baseline="$(cmd_get_status)" || die "no GET_STATUS"
parse_get_status_hex "$baseline"
v12="$(parse_get_status_hex "$baseline" | awk -F= '/^v12=/{print $2}')"
log_info "trap V12: real v12=${v12} mV, thresholds ${FAULT_V12_TRAP_MIN_MV}..${FAULT_V12_TRAP_MAX_MV} mV"
log_info "arm voltage monitor: TOUCH ON (fault_manager checks rails only when state!=0)"

fault_enable_voltage_monitor || die "failed to enable TOUCH for voltage monitoring"

hex="$(fault_trigger_v12_range)" || die "SET_THRESHOLDS (trap) no response"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS trap: expected status=0x00"

gs="$(fault_wait_flags 0400 "${FAULT_WAIT_TRIES:-30}")" || die "expected fault_flags=0x0400 (FAULT_V12_RANGE)"
parse_get_status_hex "$gs"
expect_state_bits "$gs" 0 0xff || die "expected state=0x00 (safe state)"
expect_fault_reserved_clear "$gs" || die "FAULT_RESERVED (bit 15) must stay 0"

hex="$(fault_restore_v12_defaults)" || die "SET_THRESHOLDS (restore) no response"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS restore: expected status=0x00"
fault_disable_voltage_monitor
hex="$(cmd_reset_fault)" || die "no RESET_FAULT response"
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS after cleanup"
expect_get_status_clean "$gs" && log_pass "F.1: V12 range fault latched and cleared" || die "cleanup: expected state=0 fault=0"
