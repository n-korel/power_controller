#!/bin/sh
# Dump MCU flash on Q7 without application UART protocol.
# IC17 pca9555 (gpiochip5): line8=P1_0=NRST, line9=P1_1=BOOT0
#
# IMPORTANT: PCA9555 GPIOs are push-pull outputs, not open-drain — there is
# no pull-up that brings a line back up "on release". Every level change
# (including releasing NRST) must be written explicitly.
#
# Also: gpioset --mode=exit (set-then-immediately-release) on this board
# hangs for line8=0. Workaround: always use --mode=time with a small
# nonzero duration, even for a single level change.
#
# Sequence: BOOT0=1 → NRST=0 (assert reset) → NRST=1 (release, BOOT0 still 1)
#           → wait → probe ROM → dump → BOOT0=0 (restore normal boot)
#
# Usage:
#   OTA_STM32FLASH=/opt/stm32flash/stm32flash ./dump_flash_ic17.sh [out.bin]
#
# Env:
#   UART_DEVICE=/dev/ttyS0
#   OTA_STM32FLASH=stm32flash
#   OTA_FLASH_ADDR=0x08000000
#   OTA_DUMP_SIZE=65536
#   IC17_GPIOCHIP=gpiochip5
#   IC17_LINE_NRST=8
#   IC17_LINE_BOOT0=9
#   IC17_PULSE_USEC=20000     # duration for each gpioset --mode=time step
#   IC17_PROBE=1
#   IC17_GO=1
#
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
if [ -f "$SCRIPT_DIR/config.sh" ]; then
  # shellcheck source=config.sh
  . "$SCRIPT_DIR/config.sh"
fi

OUT="${1:-${OUT:-/opt/BVIT_STM32/flash_dump.bin}}"
UART_DEVICE="${UART_DEVICE:-/dev/ttyS0}"
UART_BAUD="${UART_BAUD:-115200}"
OTA_STM32FLASH="${OTA_STM32FLASH:-stm32flash}"
OTA_FLASH_ADDR="${OTA_FLASH_ADDR:-0x08000000}"
OTA_DUMP_SIZE="${OTA_DUMP_SIZE:-65536}"
IC17_GPIOCHIP="${IC17_GPIOCHIP:-gpiochip5}"
IC17_LINE_NRST="${IC17_LINE_NRST:-8}"
IC17_LINE_BOOT0="${IC17_LINE_BOOT0:-9}"
IC17_PULSE_USEC="${IC17_PULSE_USEC:-20000}"
IC17_PROBE="${IC17_PROBE:-1}"
IC17_GO="${IC17_GO:-1}"

STM32FLASH_ROM_MODE=""

log() { printf '[INFO] %s\n' "$*"; }
die() { printf '[FAIL] %s\n' "$*" >&2; exit 1; }

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "$1 not found"
}

msleep() {
  _ms="$1"
  if command -v usleep >/dev/null 2>&1; then
    usleep $((_ms * 1000))
  else
    sleep $(( (_ms + 999) / 1000 ))
  fi
}

# Explicit single-shot write of one or more lines, held for a short duration
# then released. Value persists after release (push-pull output register).
gpioset_write() {
  # $* = LINE=VALUE pairs
  if gpioset --help 2>&1 | grep -q -- '--mode'; then
    gpioset --mode=time --usec="$IC17_PULSE_USEC" "$IC17_GPIOCHIP" "$@"
  else
    gpioset -c "$IC17_GPIOCHIP" -t "${IC17_PULSE_USEC}us" "$@"
  fi
}

ic17_bootloader_enter() {
  need_cmd gpioset
  log "IC17 $IC17_GPIOCHIP BOOT0=line$IC17_LINE_BOOT0 NRST=line$IC17_LINE_NRST"

  log "BOOT0=1"
  gpioset_write "${IC17_LINE_BOOT0}=1" || die "BOOT0=1 write failed"
  msleep 20

  log "NRST=0 (assert reset)"
  gpioset_write "${IC17_LINE_NRST}=0" || die "NRST=0 write failed"
  msleep 50

  log "NRST=1 (release reset, BOOT0 still 1)"
  gpioset_write "${IC17_LINE_NRST}=1" || die "NRST=1 write failed"

  log "wait 200ms for ROM UART init"
  msleep 200
}

ic17_boot0_restore() {
  log "BOOT0=0 (restore normal boot for next reset)"
  gpioset_write "${IC17_LINE_BOOT0}=0" || log "BOOT0=0 write failed (non-fatal)"
}

stm32flash_probe_rom() {
  need_cmd "$OTA_STM32FLASH"

  ROM_BAUDS="${ROM_BAUDS:-115200 57600}"
  ROM_MODES="${ROM_MODES:-8e1 8n1}"

  for _b in $ROM_BAUDS; do
    for _m in $ROM_MODES; do
      log "Probe ROM try: baud=${_b} mode=${_m}"
      if "$OTA_STM32FLASH" -b "$_b" -m "$_m" "$UART_DEVICE" >/dev/null 2>&1; then
        UART_BAUD="$_b"
        STM32FLASH_ROM_MODE="$_m"
        log "stm32flash probe OK (baud=${_b} mode=${_m})"
        return 0
      fi
    done
  done

  return 1
}

dump_flash() {
  if [ "$IC17_PROBE" = 1 ]; then
    if ! stm32flash_probe_rom; then
      die "stm32flash probe failed (not in ROM?)"
    fi
  fi
  [ -n "$STM32FLASH_ROM_MODE" ] || STM32FLASH_ROM_MODE="8e1"

  log "Dump ${OTA_DUMP_SIZE} bytes → ${OUT}"
  if [ "$IC17_GO" = 1 ]; then
    "$OTA_STM32FLASH" -b "$UART_BAUD" -m "$STM32FLASH_ROM_MODE" \
      -r "$OUT" \
      -S "${OTA_FLASH_ADDR}:${OTA_DUMP_SIZE}" \
      -g "$OTA_FLASH_ADDR" \
      "$UART_DEVICE" || die "stm32flash -r failed"
  else
    "$OTA_STM32FLASH" -b "$UART_BAUD" -m "$STM32FLASH_ROM_MODE" \
      -r "$OUT" \
      -S "${OTA_FLASH_ADDR}:${OTA_DUMP_SIZE}" \
      "$UART_DEVICE" || die "stm32flash -r failed"
  fi
}

main() {
  case "${1:-}" in
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
  esac

  need_cmd "$OTA_STM32FLASH"
  [ -c "$UART_DEVICE" ] || die "UART not found: $UART_DEVICE"

  out_dir="$(dirname -- "$OUT")"
  [ -d "$out_dir" ] || mkdir -p "$out_dir"

  ic17_bootloader_enter
  dump_flash
  ic17_boot0_restore

  if command -v md5sum >/dev/null 2>&1; then
    md5sum "$OUT"
  fi
  printf '[PASS] dump OK → %s\n' "$OUT"
}

main "$@"
