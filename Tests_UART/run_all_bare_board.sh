#!/usr/bin/env bash
# Полный прогон UART-тестов для пустой платы (питание + USB-UART 3.3 В, без Q7 и дисплея).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

TESTS=(
  "00_flush_port.sh"
  "01_ping.sh"
  "02_get_status.sh"
  "04_neg_backlight.sh"
  "05_neg_lcd_no_scaler.sh"
  "06_reset_bridge.sh"
  "07_bad_crc.sh"
  "08_unknown_cmd.sh"
  "09_resync_stx.sh"
  "10_packet_timeout.sh"
  "11_interbyte_gap.sh"
  "12_stress_get_status.sh"
  "03_reset_fault.sh"
)

pass=0
fail=0

printf '\n=== POWER_Controller UART — bare board ===\n'
printf 'Port: %s\n\n' "$UART_DEVICE"

for t in "${TESTS[@]}"; do
  path="$SCRIPT_DIR/$t"
  printf '%s\n' "--- $t ---"
  if bash "$path"; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
    printf '\n'
  fi
  printf '\n'
done

printf '=== Result: %d PASS, %d FAIL ===\n' "$pass" "$fail"
printf 'Optional (no display, expect fault): 13_optional_power_ctrl_seq_fault.sh\n'
printf 'Skipped on bare board: POWER_CTRL/BACKLIGHT/SET_BRIGHTNESS until display is connected\n'

[[ "$fail" -eq 0 ]]
