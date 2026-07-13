#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
hex="$(cmd_get_version)" || die "no GET_VERSION response"
if ! expect_get_version "$hex"; then
  log_fail "GET_VERSION: unexpected fw_version or reserved"
  log_info "RX(hex)=${hex}"
  parse_get_version_hex "$hex" 2>/dev/null | while IFS= read -r line; do log_info "$line"; done || true
  die "GET_VERSION: expected v${FW_VERSION_MAJOR_EXPECT}.${FW_VERSION_MINOR_EXPECT}"
fi
parsed="$(parse_get_version_hex "$hex")"
crc_line="$(printf '%s\n' "$parsed" | awk -F= '/^firmware_crc=/{print $2}')"
log_pass "GET_VERSION → v${FW_VERSION_MAJOR_EXPECT}.${FW_VERSION_MINOR_EXPECT} crc=${crc_line}"
