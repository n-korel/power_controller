#!/bin/sh
# OTA pending-confirm: reflash → сразу RESET_FAULT → образ подтверждён.
#
# Semi-manual / OTA: нужен stm32flash. Не входит в run_all.sh.
# Usage on Q7:
#   scp build/POWER_Controller.bin root@q7:/tmp/
#   FW_BIN=/tmp/POWER_Controller.bin UART_DEVICE=/dev/ttyS0 \
#     OTA_NRST_CMD='your-ic17-nrst.sh' ./34_ota_confirm_reset_fault.sh
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

log_info "OTA flash $FW_BIN (arms pending-confirm)"
ota_flash_app "$FW_BIN"

uart_open
hex="$(cmd_ping)" || die "no PING after OTA"
expect_ack_status "$hex" 170 || die "PING expected 0xAA"

# Explicit confirm immediately (before BOOT_META_CONFIRM_STABLE_MS / safe-hold).
hex="$(cmd_reset_fault)" || die "no RESET_FAULT"
expect_ack_status "$hex" 0 || die "RESET_FAULT expected status=0"

gs="$(cmd_get_status)" || die "no GET_STATUS"
expect_fault_flags "$gs" "0x0000" || die "expected fault_flags=0 after RESET_FAULT confirm"

# BOOTLOADER_ENTER would re-arm pending — use NRST-only reboot if available.
if [ -n "${OTA_NRST_CMD:-}" ]; then
  ota_nrst_reboot "post-confirm NRST"
  gs="$(cmd_get_status)" || die "no GET_STATUS after NRST"
  expect_fault_flags "$gs" "0x0000" || die "fault_flags must stay 0 after confirmed reboot"
  log_pass "OTA confirm via RESET_FAULT survived NRST"
else
  log_pass "OTA confirm via RESET_FAULT (set OTA_NRST_CMD to also verify across reboot)"
fi
