#!/usr/bin/env bash
# Test_firmware K.3 — SCALER|BACKLIGHT ON without LCD → reject, state unchanged
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
baseline="$(cmd_get_status)" || die "no baseline GET_STATUS"
log_info "POWER_CTRL SCALER|BACKLIGHT (mask=0x0005 value=0x0005, LCD off)"
hex="$(cmd_power_ctrl 0x0005 0x0005)" || die "no POWER_CTRL response"
echo "$hex" | xxd -r -p | xxd
expect_ack_status "$hex" 1 || die "expected status=0x01 (rejected by policy)"
after="$(cmd_get_status)" || die "no GET_STATUS after reject"
parse_get_status_hex "$after"
if expect_state_unchanged "$baseline" "$after"; then
  log_pass "K.3: state unchanged after atomic reject"
else
  die "K.3: state changed"
fi
