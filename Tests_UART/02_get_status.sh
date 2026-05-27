#!/usr/bin/env bash
# Test_firmware C.2 — GET_STATUS, 34-byte frame, bare-board telemetry
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
ensure_clean_state || die "failed to enter clean state before GET_STATUS"
log_info "GET_STATUS on ${UART_DEVICE}"
hex="$(cmd_get_status)" || die "no GET_STATUS response (34 bytes)"
echo "$hex" | xxd -r -p | xxd
validate_get_status_hex "$hex" || die "invalid GET_STATUS frame (expected 34 bytes: STX CMD LEN=0x1D … CRC ETX)"
parse_get_status_hex "$hex"
if expect_get_status_clean "$hex"; then
  log_pass "GET_STATUS: 34 bytes, state=0, fault=0"
else
  log_fail "GET_STATUS: expected state=0x00 and fault_flags=0x0000 on clean board"
  log_info "If POWER_CTRL was run without display — run 03_reset_fault.sh first"
  exit 1
fi
