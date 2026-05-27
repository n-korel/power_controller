#!/usr/bin/env bash
# SET_THRESHOLDS positive path: valid V12 + I_LCD update and restore
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap 'cmd_set_thresholds 0x0102 $((THRESH_V12_MIN_MV & 0xff)) $(((THRESH_V12_MIN_MV >> 8) & 0xff)) $((THRESH_V12_MAX_MV & 0xff)) $(((THRESH_V12_MAX_MV >> 8) & 0xff)) $((THRESH_I_LCD_MAX_MA & 0xff)) $(((THRESH_I_LCD_MAX_MA >> 8) & 0xff)) >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open

v12_min=9500
v12_max=13500
i_lcd_max=1800

log_info "SET_THRESHOLDS valid V12 + I_LCD"
hex="$(cmd_set_thresholds 0x0102 \
  $((v12_min & 0xff)) $(((v12_min >> 8) & 0xff)) \
  $((v12_max & 0xff)) $(((v12_max >> 8) & 0xff)) \
  $((i_lcd_max & 0xff)) $(((i_lcd_max >> 8) & 0xff)))" || die "no response (valid SET_THRESHOLDS)"
expect_ack_status "$hex" 0 || die "valid SET_THRESHOLDS: expected status=0x00"

log_info "Restore default V12 + I_LCD thresholds"
hex="$(cmd_set_thresholds 0x0102 \
  $((THRESH_V12_MIN_MV & 0xff)) $(((THRESH_V12_MIN_MV >> 8) & 0xff)) \
  $((THRESH_V12_MAX_MV & 0xff)) $(((THRESH_V12_MAX_MV >> 8) & 0xff)) \
  $((THRESH_I_LCD_MAX_MA & 0xff)) $(((THRESH_I_LCD_MAX_MA >> 8) & 0xff)))" || die "no response (restore SET_THRESHOLDS)"
expect_ack_status "$hex" 0 || die "restore SET_THRESHOLDS: expected status=0x00"

hex="$(cmd_ping)" || die "PING failed"
expect_ping_aa "$hex" && log_pass "SET_THRESHOLDS positive: valid V12 + I_LCD accepted and restored"
