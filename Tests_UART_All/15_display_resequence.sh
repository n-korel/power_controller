#!/usr/bin/env bash
# Блок 1: повторные циклы UP/DN/UP без перезапуска питания
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"

log_info "initial UP (display all ON)"
hex="$(cmd_power_ctrl 0x0007 0x0007)" || die "initial UP: no ACK"
expect_ack_status "$hex" 0 || die "initial UP: expected status=0x00"
sleep "${SEQ_BL_WAIT_SEC:-2.0}"
gs="$(wait_get_status_state 0x07 "${PERIPH_PREP_NONDISPLAY_MASK_HEX}" "${STATE_POLL_TRIES:-40}")" || die "initial UP: expected state=0x07"
expect_fault_flags "$gs" "0x0000" || die "initial UP: expected fault=0"

for cycle in 1 2; do
  log_info "cycle ${cycle}: DN (display all OFF)"
  hex="$(cmd_power_ctrl 0x0007 0x0000)" || die "cycle ${cycle}: DN no ACK"
  expect_ack_status "$hex" 0 || die "cycle ${cycle}: DN status!=0"
  sleep "${SEQ_DN_WAIT_SEC:-1.2}"
  wait_get_status_clean "${STATE_POLL_TRIES:-40}" >/dev/null || die "cycle ${cycle}: expected state=0 fault=0 after DN"

  log_info "cycle ${cycle}: UP (display all ON)"
  hex="$(cmd_power_ctrl 0x0007 0x0007)" || die "cycle ${cycle}: UP no ACK"
  expect_ack_status "$hex" 0 || die "cycle ${cycle}: UP status!=0"
  sleep "${SEQ_BL_WAIT_SEC:-2.0}"
  gs="$(wait_get_status_state 0x07 "${PERIPH_PREP_NONDISPLAY_MASK_HEX}" "${STATE_POLL_TRIES:-40}")" || die "cycle ${cycle}: expected state=0x07 after UP"
  expect_fault_flags "$gs" "0x0000" || die "cycle ${cycle}: expected fault=0 after UP"
done

log_pass "display resequence: repeated DN/UP cycles stay stable without power reset"
