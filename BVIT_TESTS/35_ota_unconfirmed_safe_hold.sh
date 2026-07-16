#!/bin/sh
# OTA safe-hold: 3 boot-а с pending без подтверждения → FAULT_BOOT_UNCONFIRMED
# (0x8000), авто-старт дисплея не поднимается, PING/GET_STATUS отвечают.
#
# Semi-manual: между boot-ами нужен NRST/power-cycle ДО истечения
# BOOT_META_CONFIRM_STABLE_MS (10 с). BOOTLOADER_ENTER нельзя — он сбрасывает
# счётчик (arm_pending). Не входит в run_all.sh.
#
# Usage on Q7:
#   scp build/POWER_Controller.bin root@q7:/tmp/
#   FW_BIN=/tmp/POWER_Controller.bin UART_DEVICE=/dev/ttyS0 \
#     OTA_NRST_CMD='your-ic17-nrst.sh' ./35_ota_unconfirmed_safe_hold.sh
set -eu
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
# shellcheck source=lib.sh
. "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT

if [ ! -f "$FW_BIN" ]; then
  log_skip "firmware bin not found: $FW_BIN (scp to Q7, set FW_BIN=...)"
  exit 0
fi
if ! command -v "${OTA_STM32FLASH:-stm32flash}" >/dev/null 2>&1; then
  log_skip "stm32flash not installed"
  exit 0
fi

log_info "Initial OTA flash (arms pending; first app boot = attempt #1)"
ota_flash_app "$FW_BIN"

uart_open
hex="$(cmd_ping)" || die "no PING after OTA"
expect_ack_status "$hex" 170 || die "PING expected 0xAA"
# Do NOT RESET_FAULT and do NOT wait 10s.
ota_nrst_reboot "attempt #2"
ota_nrst_reboot "attempt #3 (expect safe-hold)"

gs="$(cmd_get_status)" || die "no GET_STATUS in safe-hold"
expect_fault_flags "$gs" "has:0x8000" || die "expected FAULT_BOOT_UNCONFIRMED (0x8000)"

# ETH always-on only — no auto display sequencing.
expect_state_bits "$gs" 0x30 0x0F || die "safe-hold: expected state==0x30"

hex="$(cmd_ping)" || die "PING must work in safe-hold"
expect_ack_status "$hex" 170 || die "PING expected 0xAA in safe-hold"

hex="$(cmd_reset_fault)" || die "RESET_FAULT recovery"
expect_ack_status "$hex" 0 || die "RESET_FAULT expected 0"
gs="$(cmd_get_status)" || die "no GET_STATUS after RESET_FAULT"
expect_fault_flags "$gs" "0x0000" || die "RESET_FAULT must clear FAULT_BOOT_UNCONFIRMED"

log_pass "safe-hold: bit15 set, UART alive, RESET_FAULT clears pending"
