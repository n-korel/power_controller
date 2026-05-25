#!/usr/bin/env bash
# SET_BRIGHTNESS: bad LEN and pwm>1000 → status 0x01
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open

hex="$(cmd_set_brightness_len 1 232)" || die "no response (LEN=1)"
expect_ack_status "$hex" 1 || die "SET_BRIGHTNESS LEN=1: expected status=0x01"

hex="$(cmd_set_brightness 1001)" || die "no response (pwm=1001)"
expect_ack_status "$hex" 1 || die "SET_BRIGHTNESS pwm=1001: expected status=0x01"

hex="$(cmd_ping)" || die "PING after SET_BRIGHTNESS rejects failed"
expect_ping_aa "$hex" && log_pass "SET_BRIGHTNESS neg: bad LEN, pwm>1000 rejected, link OK"
