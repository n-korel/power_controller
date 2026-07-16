#!/usr/bin/env bash
# OTA pending-confirm: reflash → сразу RESET_FAULT → образ подтверждён
# (следующий reboot не уходит в FAULT_BOOT_UNCONFIRMED из-за этого цикла).
#
# Semi-manual / OTA: нужен stm32flash. Не входит в run_all_peripheral.sh.
# Usage:
#   FW_BIN=build/POWER_Controller.bin UART_DEVICE=/dev/ttyUSB0 \
#     ./34_ota_confirm_reset_fault.sh
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FW_BIN="${FW_BIN:-$REPO_ROOT/build/POWER_Controller.bin}"
OTA_SCRIPT="${OTA_SCRIPT:-$REPO_ROOT/scripts/uart/ota_flash.sh}"

if [[ ! -f "$FW_BIN" ]]; then
  log_skip "firmware bin not found: $FW_BIN (build first)"
  exit 0
fi
if [[ ! -f "$OTA_SCRIPT" ]]; then
  log_skip "ota_flash.sh not found: $OTA_SCRIPT"
  exit 0
fi
if ! command -v stm32flash >/dev/null 2>&1; then
  log_skip "stm32flash not installed"
  exit 0
fi

trap test_cleanup EXIT

log_info "OTA flash $FW_BIN (arms pending-confirm)"
UART_DEVICE="$UART_DEVICE" bash "$OTA_SCRIPT" "$FW_BIN" || die "ota_flash failed"

uart_open
hex="$(cmd_ping)" || die "no PING after OTA"
expect_ack_status "$hex" 0xAA || die "PING expected 0xAA"

# Explicit confirm immediately (before BOOT_META_CONFIRM_STABLE_MS / safe-hold).
hex="$(cmd_reset_fault)" || die "no RESET_FAULT"
expect_ack_status "$hex" 0 || die "RESET_FAULT expected status=0"

gs="$(cmd_get_status)" || die "no GET_STATUS"
expect_fault_flags "$gs" "0x0000" || die "expected fault_flags=0 after RESET_FAULT confirm"

# Power-cycle / NRST within or after the window — pending must stay cleared.
# Soft reboot via stm32flash -g without re-arming: need NRST without BOOTLOADER_ENTER.
# BOOTLOADER_ENTER would re-arm pending; use hardware reset if available, else skip check.
if [[ -n "${OTA_NRST_CMD:-}" ]]; then
  log_info "hardware reset via OTA_NRST_CMD"
  uart_close 2>/dev/null || true
  bash -c "$OTA_NRST_CMD"
  sleep 2
  uart_open
  hex="$(cmd_ping)" || die "no PING after NRST"
  expect_ack_status "$hex" 0xAA || die "PING expected 0xAA"
  gs="$(cmd_get_status)" || die "no GET_STATUS after NRST"
  expect_fault_flags "$gs" "0x0000" || die "fault_flags must stay 0 after confirmed reboot"
  log_pass "OTA confirm via RESET_FAULT survived NRST"
else
  log_pass "OTA confirm via RESET_FAULT (set OTA_NRST_CMD to also verify across reboot)"
fi
