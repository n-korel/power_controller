#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

READ_ADDR=0x08000000
READ_LEN=16

trap test_cleanup EXIT
uart_open

hex="$(cmd_read_flash_block "$READ_ADDR" "$READ_LEN")" || die "no READ_FLASH response"
if ! expect_read_flash_ok "$hex" "$READ_LEN"; then
  log_fail "READ_FLASH: status!=0"
  log_info "RX(hex)=${hex}"
  parse_read_flash_hex "$hex" 2>/dev/null | while IFS= read -r line; do log_info "$line"; done || true
  die "READ_FLASH failed at vector table"
fi

python3 - "$hex" <<'PY' || die "vector table sanity check failed"
import struct, sys
raw = bytes.fromhex(sys.argv[1].replace(' ', '').strip().lower())
data = raw[3:3 + raw[2]]
payload = data[1:]
if len(payload) != 16:
    sys.exit(1)
initial_sp, reset_vec = struct.unpack_from('<II', payload, 0)
if not (0x20000000 <= initial_sp <= 0x20002000):
    sys.exit(2)
if not (0x08000000 <= (reset_vec & ~1) <= 0x08010000):
    sys.exit(3)
if (reset_vec & 1) == 0:
    sys.exit(4)
PY

parsed="$(parse_read_flash_hex "$hex")"
status_line="$(printf '%s\n' "$parsed" | awk -F= '/^status=/{print $2}')"
data_line="$(printf '%s\n' "$parsed" | awk -F= '/^data=/{print $2}')"
log_pass "READ_FLASH → ${status_line} data=${data_line:0:32}... (${READ_LEN} bytes @ 0x08000000)"
