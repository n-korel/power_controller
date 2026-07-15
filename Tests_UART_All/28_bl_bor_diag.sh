#!/usr/bin/env bash
# Behavioral regression for BL inrush workarounds:
#   ENABLE_BL_POWER_VERIFY=0, SEQ_BL_PGOOD_GRACE_MS, ENABLE_BOR_DIAG_MARKER.
# After BACKLIGHT ON: state=0x07, fault=0, no MCU reset (state!=0x4B).
# Note: last_power_ctrl / BOR markers 0xE1..0xE6 are no longer in GET_STATUS (LEN=22).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "need SCALER+LCD before BL"

gs="$(periph_display_backlight_on)" || die "BACKLIGHT ON failed"
expect_state_bits "$gs" 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" || die "expected state=0x07"
expect_fault_flags "$gs" "0x0000" || die "expected fault=0 after BL ON"
if periph_state_is_autostart_tail "$gs"; then
  die "state=0x4B after BL ON — likely MCU reset (BOR/IWDG) during backlight inrush"
fi

# Second cycle: OFF then ON again must stay clean (catches flaky PGOOD grace / verify).
hex="$(cmd_power_ctrl 0x0004 0x0000)" || die "BACKLIGHT OFF: no ACK"
expect_ack_status "$hex" 0 || die "BACKLIGHT OFF: expected status=0x00"
sleep "${SEQ_DN_WAIT_SEC:-1.2}"
gs="$(wait_get_status_state 0x03 "$PERIPH_PREP_NONDISPLAY_MASK_HEX")" \
  || die "expected SCALER+LCD after BL OFF"
expect_fault_flags "$gs" "0x0000" || die "fault set after BL OFF"

gs="$(periph_display_backlight_on)" || die "BACKLIGHT ON (2nd) failed"
expect_state_bits "$gs" 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" || die "expected state=0x07 after 2nd ON"
expect_fault_flags "$gs" "0x0000" || die "expected fault=0 after 2nd BL ON"
if periph_state_is_autostart_tail "$gs"; then
  die "state=0x4B after 2nd BL ON — MCU reset during backlight"
fi

log_pass "BL BOR diag: two BACKLIGHT ON cycles clean (no fault, no reset)"
