#!/usr/bin/env bash
# SET_BRIGHTNESS with SCALER+LCD on, BACKLIGHT off: ACK OK, state stays 0x03
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
gs="$(periph_display_scaler_lcd_on)" || die "need SCALER+LCD"
if expect_state_bits "$gs" 0x07 "$PERIPH_PREP_NONDISPLAY_MASK_HEX"; then
  log_info "BACKLIGHT off for no-BL test (auto-start may have left BL on)" >&2
  hex="$(cmd_power_ctrl 0x0004 0x0000)" || die "BACKLIGHT OFF failed"
  expect_ack_status "$hex" 0 || die "BACKLIGHT OFF: expected status=0x00"
  sleep "${SEQ_BL_WAIT_SEC:-1.0}"
  gs="$(wait_get_status_state 0x03 "$PERIPH_PREP_NONDISPLAY_MASK_HEX")" || die "expected state=0x03 after BL OFF"
fi
python3 - "$gs" <<'PY' || die "expected state=0x03 (BL bit clear)"
import sys
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
state = raw[25] if len(raw) == 42 else 255
sys.exit(0 if state == 0x03 else 1)
PY

log_info "SET_BRIGHTNESS pwm=500 with BACKLIGHT off (buffers PWM until BL ON)"
hex="$(cmd_set_brightness 500)" || die "no ACK"
expect_ack_status "$hex" 0 || die "SET_BRIGHTNESS: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS"
python3 - "$gs" <<'PY' || die "state must stay 0x03 (no BL bit)"
import sys
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
state = raw[25] if len(raw) == 42 else 255
sys.exit(0 if state == 0x03 else 1)
PY
log_pass "SET_BRIGHTNESS with BL off: ACK OK, state=0x03 unchanged"
