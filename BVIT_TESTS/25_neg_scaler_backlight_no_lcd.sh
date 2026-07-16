#!/bin/sh
# K.3: SCALER|BACKLIGHT without LCD → atomic reject
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
baseline="$(cmd_get_status)" || die "no baseline GET_STATUS"

log_info "POWER_CTRL SCALER|BACKLIGHT (mask=0x0005 value=0x0005)"
hex="$(cmd_power_ctrl 0x0005 0x0005)" || die "no POWER_CTRL response"
expect_ack_status "$hex" 1 || die "expected status=0x01"
after="$(cmd_get_status)" || die "no GET_STATUS after reject"
expect_state_unchanged "$baseline" "$after" && log_pass "K.3 periph: SCALER|BACKLIGHT without LCD rejected" \
  || die "K.3: state changed"
