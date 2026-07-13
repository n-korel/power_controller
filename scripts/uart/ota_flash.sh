#!/usr/bin/env bash
# OTA: BOOTLOADER_ENTER → stm32flash → uart_wait_mcu_ready
#
# Usage:
#   UART_DEVICE=/dev/ttyUSB0 ./ota_flash.sh build/POWER_Controller.bin
#   make ota-flash UART_DEVICE=/dev/ttyACM0
#
# Use the .bin produced by `make all` (post-processed by scripts/fw_sign.py).
# A raw objcopy .bin without the CRC footer will fail boot_meta_confirm() on device.
#
# Hardware recovery (IC17 on Q7) after a failed attempt:
#   OTA_IC17_RECOVERY_CMD='your-q7-ic17-boot0-nrst.sh' ./ota_flash.sh firmware.bin
#
set -euo pipefail

_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "${_SCRIPT_DIR}/lib.sh"

usage() {
  cat <<'EOF'
Flash POWER_Controller firmware over UART0 (BOOTLOADER_ENTER + stm32flash).

Usage:
  ota_flash.sh <firmware.bin>

Environment:
  UART_DEVICE=/dev/ttyUSB0       serial port (MCU UART0)
  OTA_MAX_RETRIES=3              software + hardware recovery attempts
  OTA_RESET_DELAY_SEC=0.5        pause after BOOTLOADER_ENTER ACK
  OTA_HARDWARE_RESET_DELAY_SEC=0.5  pause after IC17 BOOT0+NRST
  OTA_FLASH_ADDR=0x08000000      flash base address
  OTA_STM32FLASH=stm32flash      flasher binary
  OTA_IC17_RECOVERY_CMD=         Q7 command: BOOT0=HIGH, NRST pulse, BOOT0=LOW
  OTA_VERIFY_GET_STATUS=1        GET_STATUS after successful PING probe

Requires: stm32flash, xxd, python3
EOF
}

ota_require_tools() {
  command -v "$OTA_STM32FLASH" >/dev/null 2>&1 || die "${OTA_STM32FLASH} not found"
}

ota_hardware_bootloader_entry() {
  if [[ -z "${OTA_IC17_RECOVERY_CMD}" ]]; then
    log_fail "OTA_IC17_RECOVERY_CMD is not set — cannot force hardware bootloader entry"
    return 1
  fi
  log_info "IC17 recovery: ${OTA_IC17_RECOVERY_CMD}"
  bash -c "$OTA_IC17_RECOVERY_CMD"
}

ota_flash_image() {
  require_tty
  uart_stty
  uart_flush
  log_info "Flashing ${FW_BIN} → ${OTA_FLASH_ADDR} via ${OTA_STM32FLASH}"
  "$OTA_STM32FLASH" -b "$UART_BAUD" \
    -w "$FW_BIN" -v -g "$OTA_FLASH_ADDR" "$UART_DEVICE"
}

ota_verify_app() {
  log_info "Waiting for application UART after flash..."
  uart_wait_mcu_ready || return 1

  if [[ "${OTA_VERIFY_GET_STATUS}" == 0 ]]; then
    return 0
  fi

  local hex
  uart_open
  hex="$(cmd_get_status)" || { uart_close; return 1; }
  uart_close
  validate_get_status_hex "$hex" && validate_frame_crc "$hex" || return 1
  log_pass "GET_STATUS after OTA OK"
}

# via_hardware=0: BOOTLOADER_ENTER then flash
# via_hardware=1: MCU already in ROM bootloader (IC17 path)
ota_attempt() {
  local via_hardware=${1:-0}

  if [[ "$via_hardware" -eq 0 ]]; then
    local hex
    uart_open
    hex="$(cmd_bootloader_enter)" || { uart_close; return 1; }
    expect_bootloader_enter_ack "$hex" || { uart_close; return 1; }
    uart_close
    log_pass "BOOTLOADER_ENTER ACK received"
    sleep "$OTA_RESET_DELAY_SEC"
  else
    sleep "$OTA_HARDWARE_RESET_DELAY_SEC"
  fi

  ota_flash_image || return 1
  ota_verify_app || return 1
}

main() {
  local FW_BIN=${1:-}
  if [[ -z "$FW_BIN" || "$FW_BIN" == -h || "$FW_BIN" == --help ]]; then
    usage
    [[ -z "$FW_BIN" ]] && exit 1
    exit 0
  fi
  [[ -f "$FW_BIN" ]] || die "firmware not found: ${FW_BIN}"
  if [[ "$(basename "$FW_BIN")" == *.elf ]]; then
    die "pass the signed .bin from 'make all', not the .elf"
  fi

  ota_require_tools

  local attempt via_hardware=0
  for ((attempt = 1; attempt <= OTA_MAX_RETRIES; attempt++)); do
    log_info "OTA attempt ${attempt}/${OTA_MAX_RETRIES} (via_hardware=${via_hardware})"
    if ota_attempt "$via_hardware"; then
      log_pass "OTA OK (attempt ${attempt}/${OTA_MAX_RETRIES})"
      exit 0
    fi
    log_fail "OTA attempt ${attempt}/${OTA_MAX_RETRIES} failed"

    if (( attempt >= OTA_MAX_RETRIES )); then
      break
    fi

    if ota_hardware_bootloader_entry; then
      via_hardware=1
    else
      via_hardware=0
    fi
  done

  die "OTA failed after ${OTA_MAX_RETRIES} attempts — device may need manual recovery"
}

main "$@"
