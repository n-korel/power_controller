#!/usr/bin/env bash
# SET_BRIGHTNESS positive: 0, 500, 1000 accepted
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap 'cmd_set_brightness 0 >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open

for pwm in 0 500 1000; do
  hex="$(cmd_set_brightness "$pwm")" || die "no response (SET_BRIGHTNESS ${pwm})"
  expect_ack_status "$hex" 0 || die "SET_BRIGHTNESS ${pwm}: expected status=0x00"
done

hex="$(cmd_ping)" || die "PING failed"
expect_ping_aa "$hex" && log_pass "SET_BRIGHTNESS positive: 0/500/1000 accepted"
