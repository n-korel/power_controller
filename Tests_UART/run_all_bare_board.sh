#!/usr/bin/env bash
# Полный прогон UART-тестов для пустой платы (питание + USB-UART 3.3 В, без Q7 и дисплея).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

TESTS=(
  "01_ping.sh"
  "03_reset_fault.sh"   # clear latched faults (e.g. FAULT_V12_RANGE after PGOOD startup)
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
  "14_neg_scaler_backlight.sh"
  "15_simple_domains.sh"
  "16_telemetry_sanity.sh"
  "20_set_brightness_neg.sh"
  "21_power_ctrl_neg.sh"
  "23_set_thresholds_neg.sh"
  "26_verify_rx_crc.sh"
  "19_iwdg_stress.sh"
  "17_fault_v12_range.sh"
  "24_fault_v5_range.sh"
  "25_fault_v3v3_range.sh"
  "18_fault_reserved.sh"
)

pass=0
fail=0

printf '\n=== POWER_Controller UART — bare board ===\n'
printf 'Port: %s\n\n' "$UART_DEVICE"

printf '%s\n' "--- 00_flush_port.sh ---"
if bash "$SCRIPT_DIR/00_flush_port.sh"; then
  pass=$((pass + 1))
else
  fail=$((fail + 1))
  printf '\n'
fi
printf '\n'

if ! uart_wait_mcu_ready; then
  log_info "Retrying UART preflight once after port re-init..."
  printf '%s\n' "--- 00_flush_port.sh (retry) ---"
  if bash "$SCRIPT_DIR/00_flush_port.sh"; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
    printf '\n'
  fi
  printf '\n'
  if ! uart_wait_mcu_ready; then
    printf '=== Result: %d PASS, %d FAIL (MCU UART not ready) ===\n' "$pass" "$fail"
    exit 1
  fi
fi
printf '\n'

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
