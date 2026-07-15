#!/usr/bin/env bash
# Полный прогон UART-тестов с периферией (дисплей/аудио, без Q7).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

TESTS=(
  "01_ping.sh"
  "22_get_version.sh"
  "02_reset_fault.sh"
  "23_neg_backlight_no_display.sh"
  "24_neg_lcd_no_scaler.sh"
  "25_neg_scaler_backlight_no_lcd.sh"
  "03_display_scaler_lcd_on.sh"
  "04_telemetry_under_load.sh"
  "05_backlight_brightness.sh"
  "26_set_brightness_no_bl.sh"
  "27_set_brightness_neg.sh"
  "28_bl_bor_diag.sh"
  "06_reset_bridge_display.sh"
  "07_audio_sequencing.sh"
  "08_display_shutdown.sh"
  "11_backlight_only_off.sh"
  "12_all_at_once_up.sh"
  "14_set_brightness_boundary.sh"
  "15_display_resequence.sh"
  "16_stress_get_status_load.sh"
  "17_iwdg_stress_load.sh"
  "09_fault_lcd_current.sh"
  "13_fault_recovery_display.sh"
  "18_fault_v12_under_load.sh"
  "30_fault_v5_under_load.sh"
  "32_fault_v3v3_under_load.sh"
  "19_fault_scaler_current.sh"
  "20_fault_backlight_current.sh"
  "21_fault_audio_current.sh"
  "29_calibrate_offset_neg_display.sh"
  "31_simple_domains_periph.sh"
  "10_sus_s3_manual.sh"
  "33_pgood_mid_seq_manual.sh"
)

pass=0
fail=0
skip=0
DISPLAY_SEQ_BLOCKED=0

printf '\n=== POWER_Controller UART — peripheral (display, no Q7) ===\n'
printf 'Port: %s\n\n' "$UART_DEVICE"

printf '%s\n' "--- 00_flush_port.sh ---"
if bash "$SCRIPT_DIR/00_flush_port.sh"; then
  pass=$((pass + 1))
else
  fail=$((fail + 1))
fi
printf '\n'

if ! uart_wait_mcu_ready; then
  printf '%s\n' "--- 00_flush_port.sh (retry) ---"
  bash "$SCRIPT_DIR/00_flush_port.sh" || true
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

  if [[ "$DISPLAY_SEQ_BLOCKED" -eq 1 ]] && periph_test_needs_display "$t"; then
    log_skip "display: ${DISPLAY_SKIP_REASON}"
    skip=$((skip + 1))
    printf '\n'
    continue
  fi

  rc=0
  out="$(bash "$path" 2>&1)" || rc=$?
  printf '%s\n' "$out"

  if [[ "$rc" -eq 0 ]]; then
    if echo "$out" | grep -q '\[SKIP\]'; then
      skip=$((skip + 1))
    else
      pass=$((pass + 1))
    fi
  else
    fail=$((fail + 1))
    if [[ "$t" == "03_display_scaler_lcd_on.sh" ]] && periph_output_blocks_display_suite "$out"; then
      DISPLAY_SEQ_BLOCKED=1
      log_info "display suite blocked: remaining display tests → SKIP (${DISPLAY_SKIP_REASON})"
    fi
  fi
  printf '\n'
done

printf '=== Result: %d PASS, %d SKIP, %d FAIL ===\n' "$pass" "$skip" "$fail"
if [[ "$DISPLAY_SEQ_BLOCKED" -eq 1 ]]; then
  printf 'Display blocked: %s\n' "$DISPLAY_SKIP_REASON"
fi
printf 'Unavailable without Q7: BOOTLOADER_ENTER OTA, Linux fault recovery, PGOOD+Q7 power cycle\n'

[[ "$fail" -eq 0 ]]
