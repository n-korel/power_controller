#!/bin/sh
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
hex="$(cmd_ping)" || die "no PING response"
if expect_ping_aa "$hex"; then
  log_pass "PING → 0xAA"
else
  log_fail "PING: expected 0xAA in ACK"
  log_info "RX(hex)=${hex}"
  die "PING: expected 0xAA in ACK"
fi
