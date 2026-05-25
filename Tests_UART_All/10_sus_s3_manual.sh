#!/usr/bin/env bash
# Block 6: SUS_S3# -> PWRBTN# — manual check only (UART cannot see PC14/PC15)
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
  log_fail "PGOOD=0 in GET_STATUS — SUS_S3 auto-start requires PGOOD HIGH"
  exit 1
fi

cat <<'EOF'

=== Block 6: SUS_S3# -> PWRBTN# (manual check) ===

Prerequisites:
  - PGOOD = HIGH (confirmed via GET_STATUS)
  - Without Q7: SUS_S3# (PC15) is usually HIGH via R206 to VMCU

Steps:
  1. Pull SUS_S3# (PC15) to GND through ~1 kOhm
  2. Hold for > 500 ms (SUS_S3_THRESHOLD_MS)
  3. DMM or LA on PWRBTN# (PC14): LOW pulse ~150 ms (PWRBTN_PULSE_MS)
  4. Release SUS_S3#; do not repeat sooner than 5 s (SUS_S3_COOLDOWN_MS)

MCU does not expose SUS_S3#/PWRBTN# in GET_STATUS — not automatable over UART.

EOF

if [[ "${RUN_SUS_S3_HW_TEST:-0}" == "1" ]]; then
  log_pass "SUS_S3 manual: PGOOD OK, procedure printed (RUN_SUS_S3_HW_TEST=1)"
  exit 0
fi

log_skip "SUS_S3/PWRBTN: manual steps only (set RUN_SUS_S3_HW_TEST=1 to count as PASS in suite)"
exit 0
