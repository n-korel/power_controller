#!/usr/bin/env bash
# Блок 1: shutdown — LCD/SCALER OFF при включённой подсветке → полный DN-секвенс
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"

periph_display_scaler_lcd_on >/dev/null || die "setup: SCALER+LCD ON"
periph_display_backlight_on >/dev/null || die "setup: BACKLIGHT ON"
cmd_set_brightness "$BRIGHTNESS_MID" >/dev/null || die "setup: SET_BRIGHTNESS"

log_info "POWER_CTRL LCD OFF with BACKLIGHT on (mask=0x0002 value=0) — expect full shutdown"
hex="$(cmd_power_ctrl 0x0002 0x0000)" || die "no ACK"
expect_ack_status "$hex" 0 || die "LCD OFF: expected status=0x00"
sleep "${SEQ_DN_WAIT_SEC:-1.0}"

gs="$(wait_get_status_state 0 0x07 "${STATE_POLL_TRIES:-40}")" \
  || die "shutdown: display bits not cleared in time"
parse_get_status_hex "$gs"
expect_fault_flags "$gs" "0x0000" || die "shutdown: expected fault=0"
log_pass "display shutdown: SCALER|LCD|BL off, fault=0 after LCD OFF with BL on"
