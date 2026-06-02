#!/usr/bin/env bash
# After successful BACKLIGHT ON: last_power_ctrl preserved, no BOR marker 0xE1..0xE6
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

if periph_expect_bl_power_ctrl_ok "$gs"; then
  log_pass "BL BOR diag: last_power_ctrl OK, no sequencer BOR marker"
else
  die "BL power-ctrl breadcrumb check failed (see Test_firmware.md BACKLIGHT diagnostics)"
fi
