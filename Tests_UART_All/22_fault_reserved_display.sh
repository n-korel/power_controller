#!/usr/bin/env bash
# F.3 with display on: bit 15 never set when V12 range fault is latched
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap 'fault_restore_v12_defaults >/dev/null 2>&1 || true; cmd_reset_fault >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "need SCALER+LCD (state!=0)"

hex="$(fault_trigger_v12_range)" || die "SET_THRESHOLDS trap: no response"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS: expected status=0x00"

gs="$(periph_fault_wait_latched "${FAULT_V12_RANGE_FLAG}" "${FAULT_WAIT_TRIES_VOLT:-30}")" \
  || die "expected FAULT_V12_RANGE before reserved check"
parse_get_status_hex "$gs"
expect_fault_reserved_clear "$gs" && log_pass "F.3 load: FAULT_RESERVED (bit 15) = 0" \
  || die "bit 15 must not be set"

fault_restore_v12_defaults >/dev/null || true
cmd_reset_fault >/dev/null || true
