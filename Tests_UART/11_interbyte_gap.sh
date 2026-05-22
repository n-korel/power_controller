#!/usr/bin/env bash
# Test_firmware C.19 — PING bytes with >10 ms gap between them
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
log_info "PING byte-by-byte, gap ${INTERBYTE_GAP_MS} ms"
bytes=(02 01 00 15 03)
for i in "${!bytes[@]}"; do
  printf "\\x$(printf '%02x' "${bytes[$i]}")" >&3
  if [[ "$i" -lt $((${#bytes[@]} - 1)) ]]; then
    sleep "$(python3 -c "print(${INTERBYTE_GAP_MS}/1000)")"
  fi
done
sleep 0.15
if uart_rx 6 0.25 >/dev/null 2>&1; then
  die "got response to split PING — frame should not assemble"
fi
log_pass "split PING: no response"
hex="$(cmd_ping)" || die "valid PING failed"
expect_ping_aa "$hex" && log_pass "C.19: next full frame OK"
