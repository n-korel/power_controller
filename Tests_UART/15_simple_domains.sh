#!/usr/bin/env bash
# Test_firmware K.5 — TOUCH / ETH1 / ETH2 toggle without sequencing
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
ensure_clean_state || die "failed to enter clean state before K.5"
baseline="$(cmd_get_status)" || die "no baseline GET_STATUS"
expect_get_status_clean "$baseline" || die "expected clean state=0 fault=0 before K.5"

log_info "TOUCH ON (mask=0x0040)"
hex="$(cmd_power_ctrl 0x0040 0x0040)" || die "no ACK"
expect_ack_status "$hex" 0 || die "TOUCH: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS"
expect_state_bits "$gs" 0x40 0 && log_pass "K.5: TOUCH bit set" || die "K.5: TOUCH bit not set"

log_info "ETH1+ETH2 ON (mask=0x0030)"
hex="$(cmd_power_ctrl 0x0030 0x0030)" || die "no ACK"
expect_ack_status "$hex" 0 || die "ETH: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS"
expect_state_bits "$gs" 0x70 0 || die "K.5: expected TOUCH|ETH1|ETH2 (0x70)"

log_info "ETH1 OFF (mask=0x0010 value=0)"
hex="$(cmd_power_ctrl 0x0010 0x0000)" || die "no ACK"
expect_ack_status "$hex" 0 || die "ETH1 off: expected status=0x00"
gs="$(cmd_get_status)" || die "no GET_STATUS"
expect_state_bits "$gs" 0x60 0x10 || die "K.5: ETH1 cleared, ETH2+TOUCH still on"

log_info "cleanup: TOUCH|ETH off"
cmd_power_ctrl 0x0070 0x0000 >/dev/null || true
gs="$(cmd_get_status)" || die "no GET_STATUS after cleanup"
expect_state_bits "$gs" 0 0x70 && log_pass "K.5: simple domains toggled OK" || die "K.5: cleanup failed"
