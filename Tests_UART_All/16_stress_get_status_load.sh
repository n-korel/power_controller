#!/usr/bin/env bash
# K.1 under load: burst GET_STATUS while display domains are on
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "need SCALER+LCD for load stress"
gs="$(periph_display_backlight_on)" || die "need BACKLIGHT for state=0x07"
expect_state_bits "$gs" 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX" || die "expected state=0x07"

count="${STRESS_GET_STATUS_COUNT:-20}"
gap="${STRESS_GET_STATUS_INTERVAL_SEC:-0.05}"
log_info "${count}x GET_STATUS @ state=0x07, gap ${gap}s"
ok=0
for i in $(seq 1 "$count"); do
  hex="$(cmd_get_status)" || die "iteration $i: no response"
  validate_get_status_hex "$hex" || die "iteration $i: invalid frame"
  ok=$((ok + 1))
  sleep "$gap"
done
log_pass "K.1 load: ${ok}/${count} GET_STATUS OK with display on"
