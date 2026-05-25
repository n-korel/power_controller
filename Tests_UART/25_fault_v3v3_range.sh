#!/usr/bin/env bash
# FAULT_V3V3_RANGE (0x1000) via SET_THRESHOLDS trap
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
cmd_reset_fault >/dev/null 2>&1 || true
sleep 0.1

baseline="$(cmd_get_status)" || die "no GET_STATUS"
v3="$(parse_get_status_hex "$baseline" | awk -F= '/^v3v3=/{print $2}')"
log_info "trap V3V3: real v3v3=${v3} mV, thresholds ${FAULT_V3V3_TRAP_MIN_MV}..${FAULT_V3V3_TRAP_MAX_MV} mV"

fault_enable_voltage_monitor || die "failed to enable TOUCH for voltage monitoring"

hex="$(fault_trigger_v3v3_range)" || die "SET_THRESHOLDS (trap) no response"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS trap: expected status=0x00"

gs="$(fault_wait_flags 1000 "${FAULT_WAIT_TRIES:-30}")" || die "expected fault_flags=0x1000 (FAULT_V3V3_RANGE)"
parse_get_status_hex "$gs"
expect_state_bits "$gs" 0 0xff || die "expected state=0x00 (safe state)"

fault_restore_v3v3_defaults >/dev/null || true
fault_disable_voltage_monitor
hex="$(cmd_reset_fault)" || die "no RESET_FAULT response"
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS after cleanup"
expect_get_status_clean "$gs" && log_pass "FAULT_V3V3_RANGE latched and cleared" || die "cleanup: expected state=0 fault=0"
