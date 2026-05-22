#!/usr/bin/env bash
# Bare board without display: POWER_CTRL SCALER+LCD often yields fault 0x2001 (expected).
# PASS = ACK 0x00 and fault_flags has FAULT_SEQ_ABORT|FAULT_SCALER after GET_STATUS.
# Not in run_all by default — documents protection behavior.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
cmd_reset_fault >/dev/null 2>&1 || true
sleep 0.1
log_info "POWER_CTRL SCALER+LCD (no display — expect SEQ_ABORT)"
hex="$(cmd_power_ctrl 0x0003 0x0003)" || die "no ACK"
expect_ack_status "$hex" 0 || die "expected ACK status=0x00 (command accepted)"
gs="$(cmd_get_status)" || die "no GET_STATUS"
parse_get_status_hex "$gs"
fault="$(parse_get_status_hex "$gs" | awk -F= '/^fault_flags=/{print $2}')"
if [[ "$fault" == "0x2001" ]]; then
  log_pass "bare board: fault_flags=0x2001 (FAULT_SCALER|FAULT_SEQ_ABORT) — expected"
  log_info "run 03_reset_fault.sh before other tests"
  exit 0
fi
log_fail "expected 0x2001 on board without display, got ${fault}"
exit 1
