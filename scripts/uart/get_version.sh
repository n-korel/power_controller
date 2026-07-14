#!/usr/bin/env bash
# GET_VERSION (0x0A) over UART0 — git hash / dirty / build epoch from running firmware.
#
# Usage:
#   UART_DEVICE=/dev/ttyUSB0 ./get_version.sh
#   make get-version
#   make get-version UART_DEVICE=/dev/ttyACM0
#
set -euo pipefail

_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "${_SCRIPT_DIR}/lib.sh"

usage() {
  cat <<'EOF'
Query running firmware version via GET_VERSION (0x0A).

Usage:
  get_version.sh

Environment:
  UART_DEVICE=/dev/ttyUSB0   serial port (MCU UART0)

Response DATA (LEN=13):
  git_hash_ascii[8] + dirty:u8 + build_epoch:uint32_le

Requires: xxd, python3
EOF
}

parse_version() {
  local hex=$1
  python3 - "$hex" <<'PY'
import datetime
import sys

b = bytes.fromhex(sys.argv[1].replace(" ", ""))
if len(b) < 18 or b[0] != 0x02 or b[1] != 0x0A or b[2] != 13 or b[-1] != 0x03:
    raise SystemExit(f"unexpected frame: {sys.argv[1]!r}")
d = b[3:16]
git_hash = d[0:8].decode("ascii", errors="replace")
dirty = d[8]
epoch = int.from_bytes(d[9:13], "little")
ts = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc).isoformat() if epoch else "n/a"
print(f"git_hash={git_hash}")
print(f"dirty={dirty}")
print(f"build_epoch={epoch} ({ts})")
PY
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

trap uart_close EXIT
uart_open
hex="$(cmd_get_version)" || die "GET_VERSION: no valid response"
log_pass "GET_VERSION OK"
parse_version "$hex"
printf 'frame: %s\n' "$hex"
