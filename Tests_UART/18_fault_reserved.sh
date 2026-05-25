#!/usr/bin/env bash
# Test_firmware F.3 — bit 15 (FAULT_RESERVED) never set while other faults are latched
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
cmd_reset_fault >/dev/null 2>&1 || true
sleep 0.1

log_info "provoke V12 range fault (TOUCH ON for voltage monitor), check bit 15"
fault_enable_voltage_monitor || die "failed to enable TOUCH"
hex="$(fault_trigger_v12_range)" || die "SET_THRESHOLDS no response"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS: expected status=0x00"

gs="$(fault_wait_flags 0400 "${FAULT_WAIT_TRIES:-30}")" || die "expected FAULT_V12_RANGE before reserved check"
parse_get_status_hex "$gs"
expect_fault_reserved_clear "$gs" && log_pass "F.3: FAULT_RESERVED (bit 15) = 0" || die "bit 15 must not be set"

fault_restore_v12_defaults >/dev/null || true
fault_disable_voltage_monitor
cmd_reset_fault >/dev/null || true
