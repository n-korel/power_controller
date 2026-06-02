#!/usr/bin/env bash
# Block 3: I_SCALER_MAX trap → FAULT_SCALER under SCALER+LCD load
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap 'fault_restore_i_scaler_max >/dev/null 2>&1 || true; cmd_reset_fault >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "need SCALER+LCD load for I_SCALER fault"
gs="$(periph_wait_status_load_ma i_scaler 3 0x03)" \
  || die "no stable i_scaler after SCALER+LCD ON"
sleep "${SET_THRESH_TX_DELAY_SEC:-0.2}"

rc=0
hex="$(periph_fault_trap_i_ma "$gs" i_scaler fault_set_i_scaler_max_ma)" || rc=$?
if [[ "$rc" -eq 2 ]]; then exit 0; fi
[[ "$rc" -eq 0 ]] || die "SET_THRESHOLDS trap failed"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS trap: expected status=0x00"
sleep "${SET_THRESH_TX_DELAY_SEC:-0.2}"

gs="$(periph_fault_wait_latched "${FAULT_SCALER_FLAG}" "${FAULT_WAIT_TRIES:-40}")" \
  || die "expected FAULT_SCALER (${FAULT_SCALER_FLAG})"
parse_get_status_hex "$gs"
expect_state_bits "$gs" 0 0xff || die "expected safe state=0"
expect_fault_reserved_clear "$gs" || die "FAULT_RESERVED bit 15 must stay 0"

hex="$(fault_restore_i_scaler_max)" || die "SET_THRESHOLDS restore: no response"
expect_ack_status "$hex" 0 || die "restore: expected status=0x00"
hex="$(cmd_reset_fault)" || die "no RESET_FAULT"
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS after cleanup"
expect_get_status_clean "$gs" || die "cleanup: expected state=0 fault=0"

log_pass "I_SCALER overcurrent: FAULT_SCALER latched, thresholds restored"
