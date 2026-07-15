#!/usr/bin/env bash
# GET_VERSION (0x0A): Rules_POWER invariant 52 — LEN=13, CRC OK
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open

hex="$(cmd_get_version)" || die "GET_VERSION: no valid response (LEN=13 + CRC)"

python3 - "$hex" <<'PY' || die "GET_VERSION: unexpected frame layout"
import datetime
import sys

b = bytes.fromhex(sys.argv[1].replace(" ", ""))
if len(b) != 18:
    raise SystemExit(f"frame_len={len(b)} expected=18")
if b[0] != 0x02 or b[1] != 0x0A or b[2] != 13 or b[-1] != 0x03:
    raise SystemExit(f"bad header: stx={b[0]:02x} cmd={b[1]:02x} len={b[2]} etx={b[-1]:02x}")
d = b[3:16]
git_hash = d[0:8].decode("ascii", errors="replace")
dirty = d[8]
epoch = int.from_bytes(d[9:13], "little")
ts = datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc).isoformat() if epoch else "n/a"
print(f"git_hash={git_hash} dirty={dirty} build_epoch={epoch} ({ts})")
PY

validate_frame_crc "$hex" || die "GET_VERSION: CRC mismatch"
log_pass "GET_VERSION: CMD=0x0A LEN=13 CRC OK"
