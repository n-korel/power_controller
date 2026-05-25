#!/usr/bin/env bash
# Test_firmware I.1 — burst GET_STATUS at 100 ms; MCU must not IWDG-reset
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
count="${IWDG_STRESS_COUNT:-20}"
gap="${IWDG_STRESS_INTERVAL_SEC:-0.1}"
_est_sec="$(python3 -c "print(round(${count}*(${GET_STATUS_TX_DELAY_SEC}+${GET_STATUS_TIMEOUT_SEC}+${gap}),1))")"
log_info "${count}x GET_STATUS, gap ${gap}s (I.1, expect <= ${_est_sec}s)"
ok=0
for i in $(seq 1 "$count"); do
  hex="$(cmd_get_status)" || { log_fail "iteration $i: no response (possible IWDG reset)"; exit 1; }
  validate_get_status_hex "$hex" || { log_fail "iteration $i: invalid frame"; exit 1; }
  ok=$((ok + 1))
  sleep "$gap"
done
log_pass "I.1: ${ok}/${count} GET_STATUS without MCU reset"
