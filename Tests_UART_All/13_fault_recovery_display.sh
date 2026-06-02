#!/usr/bin/env bash
# Блок 3: FAULT_LCD latch -> safe state -> RESET_FAULT -> display ON recovery
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap 'fault_restore_i_lcd_max >/dev/null 2>&1 || true; cmd_reset_fault >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "setup: need SCALER+LCD load"
gs="$(periph_wait_status_load_ma i_lcd 3 0x03)" \
  || die "no stable i_lcd after SCALER+LCD ON (retry after long suite)"
sleep "${SET_THRESH_TX_DELAY_SEC:-0.2}"

rc=0
hex="$(periph_fault_trap_i_ma "$gs" i_lcd fault_set_i_lcd_max_ma)" || rc=$?
if [[ "$rc" -eq 2 ]]; then exit 0; fi
[[ "$rc" -eq 0 ]] || die "SET_THRESHOLDS trap failed"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS trap: expected status=0x00"
sleep "${SET_THRESH_TX_DELAY_SEC:-0.2}"

gs="$(periph_fault_wait_latched "${FAULT_LCD_FLAG}" "${FAULT_WAIT_TRIES:-40}")" \
  || die "expected FAULT_LCD (${FAULT_LCD_FLAG}) within ~${FAULT_WAIT_TRIES:-40} polls"
expect_state_bits "$gs" 0 0xff || die "expected safe state=0 after FAULT_LCD"
expect_fault_reserved_clear "$gs" || die "FAULT_RESERVED bit 15 must stay 0"

hex="$(fault_restore_i_lcd_max)" || die "SET_THRESHOLDS restore: no response"
expect_ack_status "$hex" 0 || die "restore I_LCD threshold: expected status=0x00"
hex="$(cmd_reset_fault)" || die "no RESET_FAULT"
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"
wait_get_status_clean "${STATE_POLL_TRIES:-40}" >/dev/null || die "expected clean state after RESET_FAULT"

log_info "POWER_CTRL display all ON after fault reset"
hex="$(cmd_power_ctrl 0x0007 0x0007)" || die "no ACK after RESET_FAULT"
expect_ack_status "$hex" 0 || die "display ON after RESET_FAULT: expected status=0x00"
sleep "${SEQ_BL_WAIT_SEC:-2.0}"

gs="$(wait_get_status_state 0x07 "${PERIPH_PREP_NONDISPLAY_MASK_HEX}" "${STATE_POLL_TRIES:-40}")" || die "expected state=0x07 after recovery POWER_CTRL"
expect_fault_flags "$gs" "0x0000" || die "expected fault=0 after recovery POWER_CTRL"

log_pass "FAULT_LCD recovery: latch->reset->display ON works"
