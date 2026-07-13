#!/usr/bin/env bash
# Блок 3: SET_THRESHOLDS I_LCD_MAX=50 mA → FAULT_LCD, safe state, restore
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"

periph_display_scaler_lcd_on >/dev/null || die "need SCALER+LCD load for I_LCD fault"
gs="$(periph_wait_status_load_ma i_lcd 3 0x03)" \
  || die "no stable i_lcd after SCALER+LCD ON"
sleep "${SET_THRESH_TX_DELAY_SEC:-0.2}"

rc=0
hex="$(periph_fault_trap_i_ma "$gs" i_lcd fault_set_i_lcd_max_ma)" || rc=$?
if [[ "$rc" -eq 2 ]]; then exit 0; fi
[[ "$rc" -eq 0 ]] || die "SET_THRESHOLDS trap failed (check i_lcd load)"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS trap: expected status=0x00"
sleep "${SET_THRESH_TX_DELAY_SEC:-0.2}"

gs="$(periph_fault_wait_latched "${FAULT_LCD_FLAG}" "${FAULT_WAIT_TRIES:-40}")" \
  || die "expected FAULT_LCD (${FAULT_LCD_FLAG}) within ~${FAULT_WAIT_TRIES:-40} polls"
parse_get_status_hex "$gs"
expect_state_bits "$gs" 0x30 0x4f || die "expected state=0x30 (safe state) with FAULT_LCD latched"
expect_fault_reserved_clear "$gs" || die "FAULT_RESERVED bit 15 must stay 0"

hex="$(fault_restore_i_lcd_max)" || die "SET_THRESHOLDS restore: no response"
expect_ack_status "$hex" 0 || die "restore I_LCD threshold: expected status=0x00"
hex="$(cmd_reset_fault)" || die "no RESET_FAULT"
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS after cleanup"
expect_get_status_clean "$gs" || die "cleanup: expected state=0 fault=0"

log_pass "I_LCD overcurrent: FAULT_LCD latched, thresholds restored"
