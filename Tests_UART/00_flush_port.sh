#!/usr/bin/env bash
# Flush RX and configure serial port.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

require_tty
uart_stty
uart_flush
# Warm up UART right after open: on some starts first frames
# can be dropped until MCU/bridge settles.
uart_open
for _ in 1 2 3; do
  cmd_ping_probe_ready 0.25 >/dev/null 2>&1 || true
  sleep 0.05
done
uart_close
uart_flush
log_pass "Port ${UART_DEVICE} ready (${UART_BAUD} 8N1 raw)"
