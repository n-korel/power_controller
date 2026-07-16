#!/bin/sh
# Block 3: I_AUDIO_* trap → FAULT_AUDIO (ENABLE_AUDIO_HW=1 only)
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

trap 'fault_restore_i_audio_lr_max >/dev/null 2>&1 || true; cmd_reset_fault >/dev/null 2>&1 || true; periph_all_domains_off >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open

if [ "${PERIPH_AUDIO_HW_ENABLED:-0}" == "0" ]; then
  log_skip "I_AUDIO fault trap: PERIPH_AUDIO_HW_ENABLED=0"
  exit 0
fi

periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "need display base before AUDIO"

log_info "POWER_CTRL AUDIO ON"
hex="$(cmd_power_ctrl 0x0008 0x0008)" || die "no ACK (AUDIO ON)"
expect_ack_status "$hex" 0 || die "AUDIO ON: expected status=0x00"
sleep "${AUDIO_SEQ_WAIT_SEC:-0.25}"
gs="$(cmd_get_status)" || die "no GET_STATUS after AUDIO ON"
expect_state_bits "$gs" 0x0b "$PERIPH_PREP_NONDISPLAY_MASK_HEX" || die "expected AUDIO+SCALER+LCD bits"
sleep 0.2

log_info "trap I_AUDIO_L/R max=${THRESH_I_AUDIO_TRAP_MA} mA"
hex="$(fault_set_i_audio_lr_max_ma "${THRESH_I_AUDIO_TRAP_MA}")" || die "SET_THRESHOLDS trap: no response"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS trap: expected status=0x00"

gs="$(fault_wait_flags "${FAULT_AUDIO_FLAG}" "${FAULT_WAIT_TRIES:-40}")" \
  || die "expected FAULT_AUDIO (0x${FAULT_AUDIO_FLAG})"
parse_get_status_hex "$gs"
expect_state_bits "$gs" 0x30 0x4f || die "expected safe state=0x30"
expect_fault_reserved_clear "$gs" || die "FAULT_RESERVED bit 15 must stay 0"

hex="$(fault_restore_i_audio_lr_max)" || die "SET_THRESHOLDS restore: no response"
expect_ack_status "$hex" 0 || die "restore: expected status=0x00"
hex="$(cmd_reset_fault)" || die "no RESET_FAULT"
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS after cleanup"
expect_get_status_clean "$gs" || die "cleanup: expected state=0 fault=0"

log_pass "I_AUDIO overcurrent: FAULT_AUDIO latched, thresholds restored"
