#!/usr/bin/env bash
# I.1 under load: GET_STATUS burst at 100 ms with display on
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_stress_get_status_under_display_load \
  "${IWDG_STRESS_COUNT:-20}" \
  "${IWDG_STRESS_INTERVAL_SEC:-0.1}" \
  "I.1 load" \
  "GET_STATUS without MCU reset" \
  "(I.1)" \
  " (possible IWDG reset)"
