#!/usr/bin/env bash
# K.5: TOUCH / ETH1 / ETH2 toggle (when ENABLE_TOUCH_HW=1 on bench)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap 'cmd_power_ctrl 0x0070 0x0000 >/dev/null 2>&1 || true; test_cleanup' EXIT
uart_open

if [[ "${PERIPH_TOUCH_HW_ENABLED:-0}" == "0" ]]; then
  log_skip "TOUCH/ETH domains: PERIPH_TOUCH_HW_ENABLED=0"
  exit 0
fi

periph_prepare_zero_load || die "prepare failed"

for spec in "TOUCH:0x0040" "ETH1:0x0010" "ETH2:0x0020"; do
  label="${spec%%:*}"
  m="${spec##*:}"
  log_info "POWER_CTRL ${label} ON (${m})"
  hex="$(cmd_power_ctrl "$m" "$m")" || die "no ACK (${label} ON)"
  expect_ack_status "$hex" 0 || die "${label} ON: expected status=0x00"
  sleep 0.15
  gs="$(cmd_get_status)" || die "no GET_STATUS after ${label} ON"
  expect_fault_flags "$gs" "0x0000" || die "fault after ${label} ON"
  log_info "POWER_CTRL ${label} OFF"
  hex="$(cmd_power_ctrl "$m" 0x0000)" || die "no ACK (${label} OFF)"
  expect_ack_status "$hex" 0 || die "${label} OFF: expected status=0x00"
  sleep 0.15
done

gs="$(cmd_get_status)" || die "no final GET_STATUS"
expect_get_status_clean "$gs" || die "expected clean state after domain toggles"
log_pass "K.5 periph: TOUCH/ETH1/ETH2 toggle OK"
