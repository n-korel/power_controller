#!/bin/sh
# GET_VERSION (0x0A): Rules_POWER invariant 52 — LEN=13, CRC OK
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open

hex="$(cmd_get_version)" || die "GET_VERSION: no valid response (LEN=13 + CRC)"
parse_get_version_hex "$hex" || die "GET_VERSION: unexpected frame layout"
validate_frame_crc "$hex" || die "GET_VERSION: CRC mismatch"
log_pass "GET_VERSION: CMD=0x0A LEN=13 CRC OK"
