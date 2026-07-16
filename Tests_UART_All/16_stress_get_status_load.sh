#!/usr/bin/env bash
# K.1 under load: burst GET_STATUS while display domains are on
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_stress_get_status_under_display_load \
  "${STRESS_GET_STATUS_COUNT:-20}" \
  "${STRESS_GET_STATUS_INTERVAL_SEC:-0.05}" \
  "K.1 load" \
  "GET_STATUS OK with display on"
