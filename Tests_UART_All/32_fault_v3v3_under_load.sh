#!/usr/bin/env bash
# F.1 with display load: narrow V3V3 → FAULT_V3V3_RANGE (state!=0 from SCALER+LCD)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap 'fault_restore_v3v3_defaults >/dev/null 2>&1 || true; cmd_reset_fault >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "need SCALER+LCD (state!=0 for rail monitor)"

gs="$(cmd_get_status)" || die "no GET_STATUS"
v3v3="$(parse_get_status_hex "$gs" | awk -F= '/^v3v3=/{print $2}')"
log_info "trap V3V3 under load: v3v3=${v3v3} mV, thresholds ${FAULT_V3V3_TRAP_MIN_MV}..${FAULT_V3V3_TRAP_MAX_MV} mV"

hex="$(fault_trigger_v3v3_range)" || die "SET_THRESHOLDS trap: no response"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS trap: expected status=0x00"

gs="$(periph_fault_wait_latched "${FAULT_V3V3_RANGE_FLAG}" "${FAULT_WAIT_TRIES_VOLT:-30}")" \
  || die "expected FAULT_V3V3_RANGE (${FAULT_V3V3_RANGE_FLAG})"
parse_get_status_hex "$gs"
expect_state_bits "$gs" 0x30 0x4f || die "expected safe state=0x30"
expect_fault_reserved_clear "$gs" || die "FAULT_RESERVED bit 15 must stay 0"

hex="$(fault_restore_v3v3_defaults)" || die "SET_THRESHOLDS restore: no response"
expect_ack_status "$hex" 0 || die "restore: expected status=0x00"
hex="$(cmd_reset_fault)" || die "no RESET_FAULT"
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS after cleanup"
expect_get_status_clean "$gs" || die "cleanup: expected state=0 fault=0"

log_pass "F.1 load: V3V3 range fault latched with display on, cleared"
