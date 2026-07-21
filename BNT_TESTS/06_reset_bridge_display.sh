#!/bin/sh
# Блок 4: RESET_BRIDGE при включённых SCALER+LCD (~10 ms LOW на PB8)
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed"
periph_display_scaler_lcd_on >/dev/null || die "need SCALER+LCD for RESET_BRIDGE"

log_info "RESET_BRIDGE (~10 ms LOW RST_CH7511B / PB8 — verify with LA)"
hex="$(cmd_reset_bridge)" || die "no RESET_BRIDGE response"
expect_ack_status "$hex" 0 || die "RESET_BRIDGE: expected status=0x00"
sleep 0.05
gs="$(cmd_get_status)" || die "no GET_STATUS after RESET_BRIDGE"
expect_fault_flags "$gs" "0x0000" || die "unexpected fault after RESET_BRIDGE"
log_pass "RESET_BRIDGE: ACK 0x00, no fault latched"
