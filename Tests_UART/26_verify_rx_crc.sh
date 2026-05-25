#!/usr/bin/env bash
# RX frames: CRC-8/ATM on PING ACK and GET_STATUS
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open

hex="$(cmd_ping)" || die "no PING response"
validate_frame_crc "$hex" || die "PING: bad CRC"
log_info "PING CRC OK"

hex="$(cmd_get_status)" || die "no GET_STATUS response"
validate_get_status_hex "$hex" || die "invalid GET_STATUS frame"
validate_frame_crc "$hex" || die "GET_STATUS: bad CRC"
log_info "GET_STATUS CRC OK"

log_pass "RX CRC valid on PING and GET_STATUS"
