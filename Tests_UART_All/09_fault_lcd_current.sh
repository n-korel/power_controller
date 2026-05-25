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
sleep 0.2

log_info "trap I_LCD max=${THRESH_I_LCD_TRAP_MA} mA (default ${THRESH_I_LCD_DEFAULT_MA})"
hex="$(fault_set_i_lcd_max_ma "${THRESH_I_LCD_TRAP_MA}")" || die "SET_THRESHOLDS trap: no response"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS trap: expected status=0x00"

gs="$(fault_wait_flags "${FAULT_LCD_FLAG}" "${FAULT_WAIT_TRIES:-40}")" \
  || die "expected FAULT_LCD (0x${FAULT_LCD_FLAG}) within ~${FAULT_WAIT_TRIES} polls"
parse_get_status_hex "$gs"
expect_state_bits "$gs" 0 0xff || die "expected state=0 (safe state) with FAULT_LCD latched"
expect_fault_reserved_clear "$gs" || die "FAULT_RESERVED bit 15 must stay 0"

hex="$(fault_restore_i_lcd_max)" || die "SET_THRESHOLDS restore: no response"
expect_ack_status "$hex" 0 || die "restore I_LCD threshold: expected status=0x00"
hex="$(cmd_reset_fault)" || die "no RESET_FAULT"
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS after cleanup"
expect_get_status_clean "$gs" || die "cleanup: expected state=0 fault=0"

log_pass "I_LCD overcurrent: FAULT_LCD latched, thresholds restored"
