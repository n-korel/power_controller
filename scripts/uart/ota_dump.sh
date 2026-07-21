#!/usr/bin/env bash
# Dump MCU flash via BOOTLOADER_ENTER → stm32flash -r
#
# Usage:
#   UART_DEVICE=/dev/ttyUSB0 ./ota_dump.sh flash_dump.bin
#   make ota-dump OUT=flash_dump.bin
#
# Hardware recovery (IC17 on Q7) after a failed attempt:
#   OTA_IC17_RECOVERY_CMD='your-q7-ic17-boot0-nrst.sh' ./ota_dump.sh flash_dump.bin
#
set -euo pipefail

_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "${_SCRIPT_DIR}/lib.sh"

OTA_DUMP_SIZE="${OTA_DUMP_SIZE:-65536}"

usage() {
  cat <<'EOF'
Dump POWER_Controller_BNT flash over UART0 (BOOTLOADER_ENTER + stm32flash -r).

Usage:
  ota_dump.sh <output.bin>

Environment:
  UART_DEVICE=/dev/ttyUSB0       serial port (MCU UART0)
  OTA_MAX_RETRIES=3              software + hardware recovery attempts
  OTA_RESET_DELAY_SEC=0.5        pause after BOOTLOADER_ENTER ACK
  OTA_HARDWARE_RESET_DELAY_SEC=0.5  pause after IC17 BOOT0+NRST
  OTA_FLASH_ADDR=0x08000000      flash base address
  OTA_DUMP_SIZE=65536            bytes to read (64 KiB default)
  OTA_STM32FLASH=stm32flash      flasher binary
  OTA_IC17_RECOVERY_CMD=         Q7 command: BOOT0=HIGH, NRST pulse, BOOT0=LOW
  OTA_VERIFY_GET_STATUS=1        GET_STATUS after return to application

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

ota_dump_image() {
  require_tty
  uart_stty
  uart_flush
  log_info "Reading ${OTA_DUMP_SIZE} bytes from ${OTA_FLASH_ADDR} → ${OUT_BIN}"
  "$OTA_STM32FLASH" -b "$UART_BAUD" \
    -r "$OUT_BIN" \
    -S "${OTA_FLASH_ADDR}:${OTA_DUMP_SIZE}" \
    -g "$OTA_FLASH_ADDR" \
    "$UART_DEVICE"
}

ota_verify_app() {
  log_info "Waiting for application UART after dump..."
  uart_wait_mcu_ready || return 1

  if [[ "${OTA_VERIFY_GET_STATUS}" == 0 ]]; then
    return 0
  fi

  local hex
  uart_open
  hex="$(cmd_get_status)" || { uart_close; return 1; }
  uart_close
  validate_get_status_hex "$hex" && validate_frame_crc "$hex" || return 1
  log_pass "GET_STATUS after dump OK"
}

# via_hardware=0: BOOTLOADER_ENTER then dump
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

  ota_dump_image || return 1
  ota_verify_app || return 1
}

main() {
  local OUT_BIN=${1:-}
  if [[ -z "$OUT_BIN" || "$OUT_BIN" == -h || "$OUT_BIN" == --help ]]; then
    usage
    [[ -z "$OUT_BIN" ]] && exit 1
    exit 0
  fi

  ota_require_tools

  local attempt via_hardware=0
  for ((attempt = 1; attempt <= OTA_MAX_RETRIES; attempt++)); do
    log_info "Dump attempt ${attempt}/${OTA_MAX_RETRIES} (via_hardware=${via_hardware})"
    if ota_attempt "$via_hardware"; then
      log_pass "Dump OK → ${OUT_BIN} (attempt ${attempt}/${OTA_MAX_RETRIES})"
      exit 0
    fi
    log_fail "Dump attempt ${attempt}/${OTA_MAX_RETRIES} failed"

    if (( attempt >= OTA_MAX_RETRIES )); then
      break
    fi

    if ota_hardware_bootloader_entry; then
      via_hardware=1
    else
      via_hardware=0
    fi
  done

  die "Dump failed after ${OTA_MAX_RETRIES} attempts — device may need manual recovery"
}

main "$@"
