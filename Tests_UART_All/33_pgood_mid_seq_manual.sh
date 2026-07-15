#!/usr/bin/env bash
# Rules_POWER invariant 44: PGOOD drop mid display sequence → emergency off + fault
# Manual only — UART cannot force PGOOD; document procedure like 10_sus_s3_manual.sh
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open

gs="$(cmd_get_status)" || die "no GET_STATUS"
parse_get_status_hex "$gs"
pgood="$(parse_get_status_hex "$gs" | awk -F= '/^pgood=/{print $2}')"
if [[ "$pgood" != "1" ]]; then
  log_fail "PGOOD=0 in GET_STATUS — mid-sequence drop test requires PGOOD HIGH first"
  exit 1
fi

cat <<'EOF'

=== Invariant 44: PGOOD drop during display sequence (manual) ===

Prerequisites:
  - PGOOD = HIGH (confirmed via GET_STATUS)
  - Bench wiring that can pull PGOOD LOW without killing the MCU rail
  - Optional LA/scope on SCALER/LCD/BL enable lines

Steps:
  1. Start display bring-up: POWER_CTRL SCALER+LCD(+BL) ON (or run 03 / 12)
  2. While sequence is in progress (before state settles at 0x03/0x07),
     pull PGOOD LOW
  3. Expect emergency display shutdown (BL → LCD → RST → SCALER, no delays)
  4. GET_STATUS: display bits clear; fault includes FAULT_PGOOD_LOST (0x0080)
     and typically FAULT_SEQ_ABORT (0x2000)
  5. Restore PGOOD HIGH; RESET_FAULT; confirm clean state before other tests

Not automatable over UART alone — PGOOD is an external input.

EOF

if [[ "${RUN_PGOOD_MID_SEQ_HW_TEST:-0}" == "1" ]]; then
  log_pass "PGOOD mid-seq manual: procedure printed (RUN_PGOOD_MID_SEQ_HW_TEST=1)"
  exit 0
fi

log_skip "PGOOD mid-sequence: manual steps only (set RUN_PGOOD_MID_SEQ_HW_TEST=1 to count as PASS)"
exit 0
