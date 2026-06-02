#!/usr/bin/env bash
# I.1 under load: GET_STATUS burst at 100 ms with display on
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "need SCALER+LCD"
gs="$(periph_display_backlight_on)" || die "need BACKLIGHT"
expect_state_bits "$gs" 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" || die "expected state=0x07"

count="${IWDG_STRESS_COUNT:-20}"
gap="${IWDG_STRESS_INTERVAL_SEC:-0.1}"
log_info "${count}x GET_STATUS @ state=0x07, gap ${gap}s (I.1)"
ok=0
for i in $(seq 1 "$count"); do
  hex="$(cmd_get_status)" || die "iteration $i: no response (possible IWDG reset)"
  validate_get_status_hex "$hex" || die "iteration $i: invalid frame"
  ok=$((ok + 1))
  sleep "$gap"
done
log_pass "I.1 load: ${ok}/${count} GET_STATUS without MCU reset"
