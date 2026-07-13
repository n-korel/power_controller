#!/usr/bin/env bash
# Dump MCU flash over application UART (READ_FLASH chunks).
#
# Usage:
#   UART_DEVICE=/dev/ttyUSB0 ./flash_dump.sh backup.bin
#   ./flash_dump.sh backup.bin --elf build/POWER_Controller.elf
#   make flash-dump OUT=backup.bin
#
set -euo pipefail

_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "${_SCRIPT_DIR}/lib.sh"

FLASH_BASE="${FLASH_DUMP_BASE:-0x08000000}"
FLASH_END_DEFAULT="${FLASH_DUMP_END_DEFAULT:-0x08010000}"
CHUNK_MAX=63

usage() {
  cat <<'EOF'
Dump POWER_Controller flash via CMD_READ_FLASH (application UART protocol).

Usage:
  flash_dump.sh <output.bin> [--start ADDR] [--end ADDR] [--elf PATH]

Environment:
  UART_DEVICE=/dev/ttyUSB0     serial port (MCU UART0)
  FLASH_DUMP_BASE=0x08000000   default start address
  FLASH_DUMP_END_DEFAULT=...   default end (exclusive) when --elf is not used
  FLASH_DUMP_VERIFY_CRC=1      compare CRC32 with GET_VERSION when range matches image
EOF
}

parse_hex_addr() {
  local v=$1
  if [[ "$v" =~ ^0[xX] ]]; then
    echo $((v))
  else
    echo $((16#$v))
  fi
}

image_end_from_elf() {
  local elf=$1
  command -v arm-none-eabi-nm >/dev/null 2>&1 || return 1
  local sym
  sym="$(arm-none-eabi-nm "$elf" | awk '/[[:space:]]_flash_image_end$/ {print $1; exit}')"
  [[ -n "$sym" ]] || return 1
  parse_hex_addr "0x${sym}"
}

crc32_image_file() {
  python3 - "$1" <<'PY'
import struct, sys, zlib
from pathlib import Path
path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
if len(data) < 4:
    print('0')
    sys.exit(0)
crc_off = len(data) - 4
payload = bytearray(data)
payload[crc_off:crc_off + 4] = b'\x00\x00\x00\x00'
print(f'{zlib.crc32(bytes(payload)) & 0xFFFFFFFF}')
PY
}

main() {
  local out=""
  local start_addr=""
  local end_addr=""
  local elf=""

  while [[ $# -gt 0 ]]; do
    case "$1" in
      -h|--help)
        usage
        exit 0
        ;;
      --start)
        shift
        start_addr="$(parse_hex_addr "${1:?--start needs address}")"
        ;;
      --end)
        shift
        end_addr="$(parse_hex_addr "${1:?--end needs address}")"
        ;;
      --elf)
        shift
        elf="${1:?--elf needs path}"
        ;;
      -*)
        die "unknown option: $1"
        ;;
      *)
        if [[ -z "$out" ]]; then
          out="$1"
        else
          die "unexpected argument: $1"
        fi
        ;;
    esac
    shift
  done

  [[ -n "$out" ]] || { usage; exit 1; }

  if [[ -z "$start_addr" ]]; then
    start_addr="$(parse_hex_addr "$FLASH_BASE")"
  fi

  if [[ -z "$end_addr" ]]; then
    if [[ -n "$elf" && -f "$elf" ]]; then
      end_addr="$(image_end_from_elf "$elf")" || die "cannot resolve _flash_image_end from ${elf}"
    else
      end_addr="$(parse_hex_addr "$FLASH_END_DEFAULT")"
    fi
  fi

  if (( end_addr <= start_addr )); then
    die "invalid range: start=0x$(printf '%x' "$start_addr") end=0x$(printf '%x' "$end_addr") (end is exclusive)"
  fi

  local total=$((end_addr - start_addr))
  log_info "Dump 0x$(printf '%08x' "$start_addr")..0x$(printf '%08x' "$end_addr") (${total} bytes) → ${out}"

  : >"$out"
  uart_open

  local offset=0
  local addr=$start_addr
  while (( offset < total )); do
    local chunk=$((total - offset))
    if (( chunk > CHUNK_MAX )); then
      chunk=$CHUNK_MAX
    fi

    local hex
    hex="$(cmd_read_flash_block "0x$(printf '%x' "$addr")" "$chunk")" \
      || { uart_close; die "READ_FLASH failed at 0x$(printf '%08x' "$addr") len=${chunk}"; }

    if ! expect_read_flash_ok "$hex" "$chunk"; then
      uart_close
      die "READ_FLASH status error at 0x$(printf '%08x' "$addr")"
    fi

    python3 - "$hex" "$out" <<'PY'
import sys
raw = bytes.fromhex(sys.argv[1].replace(' ', '').strip().lower())
out = sys.argv[2]
data = raw[3:3 + raw[2]]
payload = data[1:]
with open(out, 'ab') as f:
    f.write(payload)
PY

    offset=$((offset + chunk))
    addr=$((addr + chunk))
    if (( offset % 1024 == 0 )) || (( offset == total )); then
      log_info "progress ${offset}/${total} bytes"
    fi
  done

  uart_close

  local size
  size="$(wc -c <"$out" | tr -d ' ')"
  if [[ "$size" != "$total" ]]; then
    die "size mismatch: wrote ${size}, expected ${total}"
  fi
  log_pass "flash dump OK (${size} bytes)"

  if [[ "${FLASH_DUMP_VERIFY_CRC:-1}" == 0 ]]; then
    return 0
  fi

  if [[ -n "$elf" && -f "$elf" ]]; then
    local img_end flash_base_parsed
    img_end="$(image_end_from_elf "$elf")" || return 0
    flash_base_parsed="$(parse_hex_addr "$FLASH_BASE")"
    if (( start_addr == flash_base_parsed && end_addr == img_end )); then
      local ver_hex fw_crc file_crc
      uart_open
      ver_hex="$(cmd_get_version)" || { uart_close; die "GET_VERSION failed during CRC verify"; }
      uart_close
      fw_crc="$(parse_get_version_hex "$ver_hex" | awk -F= '/^firmware_crc=/{print $2}')"
      file_crc="$(crc32_image_file "$out")"
      if [[ "$fw_crc" != "0x$(printf '%08x' "$file_crc")" ]]; then
        die "CRC mismatch: GET_VERSION=${fw_crc} dump=0x$(printf '%08x' "$file_crc")"
      fi
      log_pass "CRC32 matches GET_VERSION (${fw_crc})"
    fi
  fi
}

main "$@"
