#!/usr/bin/env bash
# Test_firmware K.1 — GET_STATUS burst without hangs
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
_est_sec="$(python3 -c "print(round(${STRESS_GET_STATUS_COUNT}*(${GET_STATUS_TX_DELAY_SEC}+${GET_STATUS_TIMEOUT_SEC}+${STRESS_GET_STATUS_INTERVAL_SEC}),1))")"
log_info "${STRESS_GET_STATUS_COUNT}x GET_STATUS, gap ${STRESS_GET_STATUS_INTERVAL_SEC}s (expect <= ${_est_sec}s)"
ok=0
for i in $(seq 1 "$STRESS_GET_STATUS_COUNT"); do
  if (( i == 1 || i % 5 == 0 )); then
    log_info "progress: ${i}/${STRESS_GET_STATUS_COUNT}"
  fi
  hex="$(cmd_get_status)" || { log_fail "iteration $i: no response"; exit 1; }
  if ! validate_get_status_hex "$hex"; then
    log_fail "iteration $i: invalid GET_STATUS frame"
    exit 1
  fi
  ok=$((ok + 1))
  sleep "$STRESS_GET_STATUS_INTERVAL_SEC"
done
log_pass "K.1: ${ok}/${STRESS_GET_STATUS_COUNT} responses, 34 bytes each"
