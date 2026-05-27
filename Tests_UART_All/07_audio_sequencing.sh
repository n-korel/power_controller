#!/usr/bin/env bash
# Блок 5: POWER_CTRL AUDIO ON/OFF (SDZ/MUTE tail при уже поднятом POWER_AUDIO)
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

trap test_cleanup EXIT
uart_open
periph_prepare_zero_load || die "prepare failed (calibrate before AUDIO — else FAULT_AUDIO from ~1.5 A offset)"

if [[ "${PERIPH_AUDIO_HW_ENABLED:-0}" == "0" ]]; then
  log_info "POWER_CTRL AUDIO ON must be rejected on this revision (mask=0x0008 value=0x0008)"
  hex="$(cmd_power_ctrl 0x0008 0x0008)" || die "no ACK"
  expect_ack_status "$hex" 1 || die "AUDIO ON: expected status=0x01 (disabled by revision)"
  sleep "${AUDIO_SEQ_WAIT_SEC:-0.25}"
  gs="$(cmd_get_status)" || die "no GET_STATUS after rejected AUDIO ON"
  expect_state_bits "$gs" 0 0x08 || die "rejected AUDIO ON: DOM_AUDIO bit must stay clear"
  expect_fault_flags "$gs" "0x0000" || die "fault after rejected AUDIO ON"

  log_info "POWER_CTRL AUDIO OFF (mask=0x0008 value=0x0000)"
  hex="$(cmd_power_ctrl 0x0008 0x0000)" || die "no ACK for AUDIO OFF"
  expect_ack_status "$hex" 0 || die "AUDIO OFF: expected status=0x00"
  sleep "${AUDIO_SEQ_WAIT_SEC:-0.25}"
  gs="$(cmd_get_status)" || die "no GET_STATUS after AUDIO OFF"
  expect_state_bits "$gs" 0 0x08 || die "AUDIO OFF: DOM_AUDIO bit must be clear"
  expect_fault_flags "$gs" "0x0000" || die "fault after AUDIO OFF"

  log_pass "audio sequencing: revision-disabled AUDIO is rejected as expected"
  exit 0
fi

log_info "POWER_CTRL AUDIO ON (mask=0x0008 value=0x0008)"
hex="$(cmd_power_ctrl 0x0008 0x0008)" || die "no ACK"
expect_ack_status "$hex" 0 || die "AUDIO ON: expected status=0x00"
sleep "${AUDIO_SEQ_WAIT_SEC:-0.25}"
gs="$(cmd_get_status)" || die "no GET_STATUS after AUDIO ON"
if ! expect_state_bits "$gs" 0x08 0; then
  periph_log_status "$gs" "AUDIO ON failed"
  if expect_fault_flags "$gs" "has:0x0008"; then
    die "FAULT_AUDIO — audio current >800 mA without calibration or Faultz active"
  fi
  die "AUDIO ON: expected DOM_AUDIO bit (0x08)"
fi
expect_fault_flags "$gs" "0x0000" || die "fault after AUDIO ON"
log_info "manual: PC8 (SDZ)=3.3V, PC6 (MUTE)=0V after amp unmute"

log_info "POWER_CTRL AUDIO OFF (mask=0x0008 value=0x0000)"
hex="$(cmd_power_ctrl 0x0008 0x0000)" || die "no ACK for AUDIO OFF"
expect_ack_status "$hex" 0 || die "AUDIO OFF: expected status=0x00"
sleep "${AUDIO_SEQ_WAIT_SEC:-0.25}"
gs="$(cmd_get_status)" || die "no GET_STATUS after AUDIO OFF"
expect_state_bits "$gs" 0 0x08 || die "AUDIO OFF: DOM_AUDIO bit must be clear"
expect_fault_flags "$gs" "0x0000" || die "fault after AUDIO OFF"
log_info "manual: after OFF — MUTE high, SDZ low, POWER_AUDIO low"

log_pass "audio sequencing: ON/OFF ACK OK, state bit follows"
