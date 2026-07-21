#!/bin/sh
# Block 3: I_BACKLIGHT_MAX trap → FAULT_BACKLIGHT (needs BL current sensor U4)
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

trap 'fault_restore_i_bl_max >/dev/null 2>&1 || true; cmd_reset_fault >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open

if [ "${PERIPH_BL_CURRENT_SENSOR_ENABLED:-0}" == "0" ]; then
  log_skip "I_BACKLIGHT fault trap: PERIPH_BL_CURRENT_SENSOR_ENABLED=0 (no NSM2012)"
  exit 0
fi

periph_prepare_zero_load || die "prepare failed"
gs="$(periph_display_backlight_on)" || die "need BACKLIGHT on for I_BL fault"
expect_state_bits "$gs" 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" || die "expected state=0x07"
sleep 0.2

log_info "trap I_BACKLIGHT max=${THRESH_I_BL_TRAP_MA} mA (default ${THRESH_I_BL_DEFAULT_MA})"
hex="$(fault_set_i_bl_max_ma "${THRESH_I_BL_TRAP_MA}")" || die "SET_THRESHOLDS trap: no response"
expect_ack_status "$hex" 0 || die "SET_THRESHOLDS trap: expected status=0x00"

gs="$(fault_wait_flags "${FAULT_BACKLIGHT_FLAG}" "${FAULT_WAIT_TRIES:-40}")" \
  || die "expected FAULT_BACKLIGHT (0x${FAULT_BACKLIGHT_FLAG})"
parse_get_status_hex "$gs"
expect_state_bits "$gs" 0x30 0x4f || die "expected safe state=0x30"
expect_fault_reserved_clear "$gs" || die "FAULT_RESERVED bit 15 must stay 0"

hex="$(fault_restore_i_bl_max)" || die "SET_THRESHOLDS restore: no response"
expect_ack_status "$hex" 0 || die "restore: expected status=0x00"
hex="$(cmd_reset_fault)" || die "no RESET_FAULT"
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS after cleanup"
expect_get_status_clean "$gs" || die "cleanup: expected state=0 fault=0"

log_pass "I_BACKLIGHT overcurrent: FAULT_BACKLIGHT latched, thresholds restored"
