#!/usr/bin/env bash
# Блок 1: POWER_CTRL SCALER+LCD ON — секвенс шагов 1–6, телеметрия шин
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed (need state=0 + current calibration)"

gs="$(periph_display_scaler_lcd_on)" || die "SCALER+LCD ON failed (see fault_flags above)"
export THRESH_V12_MIN_MV THRESH_V12_MAX_MV THRESH_V5_MIN_MV THRESH_V5_MAX_MV
export THRESH_V3V3_MIN_MV THRESH_V3V3_MAX_MV
expect_rails_in_range "$gs" || die "rails out of range (expect ~12V/5V/3.3V on v12/v5/v3v3)"

log_info "verify: SCALER_POWER_M (PB1) and LCD rail — use LA/DMM if needed (not in GET_STATUS)"
log_pass "display: SCALER+LCD ON, state=0x03, fault=0, rails OK"
