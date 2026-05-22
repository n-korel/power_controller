#!/usr/bin/env bash
# Flush RX and configure serial port.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

require_tty
uart_stty
uart_flush
log_pass "Port ${UART_DEVICE} ready (${UART_BAUD} 8N1 raw)"
