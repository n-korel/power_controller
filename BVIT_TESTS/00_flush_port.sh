#!/bin/sh
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

require_tty
uart_flush
uart_open
ok=0
for _ in 1 2 3; do
  if cmd_ping_probe_ready >/dev/null 2>&1; then
    ok=1
    break
  fi
  sleep 0.05
done
uart_close
uart_flush
if [ "$ok" -eq 1 ]; then
  log_pass "Port ${UART_DEVICE} ready (${UART_BAUD} 8N1 raw via socat); PING OK"
else
  die "Port ${UART_DEVICE} configured but no PING response (check UART_DEVICE / ttyS0)"
fi
