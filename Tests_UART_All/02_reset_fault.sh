#!/usr/bin/env bash
# Сброс fault + подготовка стенда (все домены OFF, CALIBRATE_OFFSET при необходимости)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
hex="$(cmd_reset_fault)" || die "no RESET_FAULT response"
expect_ack_status "$hex" 0 || die "RESET_FAULT: expected status=0x00"
periph_prepare_zero_load || die "baseline prepare failed (all off + calibrate)"
gs="$(cmd_get_status)" || die "no GET_STATUS"
expect_get_status_clean "$gs" && log_pass "baseline: state=0 fault=0, currents calibrated" \
  || die "baseline: expected clean GET_STATUS after prepare"
