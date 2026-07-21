#!/usr/bin/env bash
# OTA safe-hold: 3 boot-а с pending без подтверждения → FAULT_BOOT_UNCONFIRMED
# (0x8000), авто-старт дисплея не поднимается, PING/GET_STATUS отвечают.
#
# Semi-manual: между boot-ами нужен NRST/power-cycle ДО истечения
# BOOT_META_CONFIRM_STABLE_MS (10 с). BOOTLOADER_ENTER нельзя — он сбрасывает
# счётчик (arm_pending). Не входит в run_all_peripheral.sh.
#
# Usage:
#   FW_BIN=build/POWER_Controller_BNT.bin UART_DEVICE=/dev/ttyUSB0 \
#     OTA_NRST_CMD='your-nrst-pulse.sh' ./35_ota_unconfirmed_safe_hold.sh
#   # Or interactive prompts when OTA_NRST_CMD is unset:
#   ./35_ota_unconfirmed_safe_hold.sh
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FW_BIN="${FW_BIN:-$REPO_ROOT/build/POWER_Controller_BNT.bin}"
OTA_SCRIPT="${OTA_SCRIPT:-$REPO_ROOT/scripts/uart/ota_flash.sh}"

if [[ ! -f "$FW_BIN" ]]; then
  log_skip "firmware bin not found: $FW_BIN (build first)"
  exit 0
fi
if ! command -v stm32flash >/dev/null 2>&1; then
  log_skip "stm32flash not installed"
  exit 0
fi

trap test_cleanup EXIT

nrst_reboot() {
  local label=$1
  if [[ -n "${OTA_NRST_CMD:-}" ]]; then
    log_info "$label: OTA_NRST_CMD"
    uart_close 2>/dev/null || true
    bash -c "$OTA_NRST_CMD"
  else
    log_info "$label: press Enter after NRST/power-cycle (must be <10s since last boot)"
    uart_close 2>/dev/null || true
    read -r _
  fi
  sleep 1
  uart_open
  local hex
  hex="$(cmd_ping)" || die "no PING after reboot ($label)"
  expect_ack_status "$hex" 0xAA || die "PING expected 0xAA ($label)"
}

log_info "Initial OTA flash (arms pending; first app boot = attempt #1)"
UART_DEVICE="$UART_DEVICE" bash "$OTA_SCRIPT" "$FW_BIN" || die "ota_flash failed"

uart_open
hex="$(cmd_ping)" || die "no PING after OTA"
expect_ack_status "$hex" 0xAA || die "PING expected 0xAA"
# Do NOT RESET_FAULT and do NOT wait 10s.
nrst_reboot "attempt #2"
nrst_reboot "attempt #3 (expect safe-hold)"

gs="$(cmd_get_status)" || die "no GET_STATUS in safe-hold"
expect_fault_flags "$gs" "has:0x8000" || die "expected FAULT_BOOT_UNCONFIRMED (0x8000)"

# ETH always-on only — no auto display sequencing.
expect_state_bits "$gs" 0x30 0x0F || die "safe-hold: expected state==0x30"

hex="$(cmd_ping)" || die "PING must work in safe-hold"
expect_ack_status "$hex" 0xAA || die "PING expected 0xAA in safe-hold"

hex="$(cmd_reset_fault)" || die "RESET_FAULT recovery"
expect_ack_status "$hex" 0 || die "RESET_FAULT expected 0"
gs="$(cmd_get_status)" || die "no GET_STATUS after RESET_FAULT"
expect_fault_flags "$gs" "0x0000" || die "RESET_FAULT must clear FAULT_BOOT_UNCONFIRMED"

log_pass "safe-hold: bit15 set, UART alive, RESET_FAULT clears pending"
