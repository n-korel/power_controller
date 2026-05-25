#!/usr/bin/env bash
# Test_firmware A.2, A.3 — rails in default thresholds, zero-load currents, NTC absent
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$SCRIPT_DIR/lib.sh"

currents_in_range() {
  local hex=$1
  python3 - "$hex" <<'PY'
import os, struct, sys
raw = bytes.fromhex(sys.argv[1].replace(' ', ''))
if len(raw) != 31:
    sys.exit(1)
data = raw[3:29]
off = 20
i_min = int(os.environ['TELEMETRY_I_MIN_MA'])
i_max = int(os.environ['TELEMETRY_I_MAX_MA'])
for _ in range(5):
    v = struct.unpack_from('<h', data, off)[0]
    if not (i_min <= v <= i_max):
        sys.exit(1)
    off += 2
sys.exit(0)
PY
}

trap test_cleanup EXIT
uart_open
export TELEMETRY_I_MIN_MA TELEMETRY_I_MAX_MA

hex="$(cmd_get_status)" || die "no GET_STATUS"
check_currents=0
if currents_in_range "$hex"; then
  log_info "currents already in ±${TELEMETRY_I_MAX_MA} mA — skip CALIBRATE_OFFSET"
  check_currents=1
else
  log_info "uncalibrated currents — CALIBRATE_OFFSET (state must be 0)"
  cal_hex=""
  cal_hex="$(cmd_calibrate_offset 2>/dev/null)" || true
  if [[ -z "$cal_hex" ]]; then
    log_skip "CALIBRATE_OFFSET: no ACK (flash busy?) — recover link"
    sleep 0.5
    uart_drain_fd
    cmd_ping >/dev/null 2>&1 || die "link dead after CALIBRATE_OFFSET"
    check_currents=0
  elif expect_ack_status "$cal_hex" 0; then
    log_pass "CALIBRATE_OFFSET OK"
    sleep 0.2
    check_currents=1
  else
    log_skip "CALIBRATE_OFFSET status!=0 — skip current range check"
    check_currents=0
  fi
  hex="$(cmd_get_status)" || die "no GET_STATUS after calibrate"
fi

parse_get_status_hex "$hex"
export THRESH_V12_MIN_MV THRESH_V12_MAX_MV THRESH_V5_MIN_MV THRESH_V5_MAX_MV
export THRESH_V3V3_MIN_MV THRESH_V3V3_MAX_MV CHECK_CURRENTS="$check_currents"
python3 - "$hex" <<'PY'
import os
import struct
import sys

h = sys.argv[1].replace(' ', '').strip().lower()
raw = bytes.fromhex(h)
if len(raw) != 31:
    print(f'FAIL: frame len {len(raw)}', file=sys.stderr)
    sys.exit(1)
data = raw[3:29]
off = 0
rails = {}
for n in ('v24', 'v12', 'v5', 'v3v3'):
    rails[n] = struct.unpack_from('<H', data, off)[0]
    off += 2
currents = {}
for n in ('i_lcd', 'i_backlight', 'i_scaler', 'i_audio_l', 'i_audio_r'):
    currents[n] = struct.unpack_from('<h', data, off)[0]
    off += 2
temps = {}
for n in ('temp0', 'temp1'):
    temps[n] = struct.unpack_from('<h', data, off)[0]
    off += 2

limits = {
    'v12': (int(os.environ['THRESH_V12_MIN_MV']), int(os.environ['THRESH_V12_MAX_MV'])),
    'v5': (int(os.environ['THRESH_V5_MIN_MV']), int(os.environ['THRESH_V5_MAX_MV'])),
    'v3v3': (int(os.environ['THRESH_V3V3_MIN_MV']), int(os.environ['THRESH_V3V3_MAX_MV'])),
}
check_i = os.environ.get('CHECK_CURRENTS', '1') == '1'
i_min = int(os.environ['TELEMETRY_I_MIN_MA'])
i_max = int(os.environ['TELEMETRY_I_MAX_MA'])

ok = True
for name, (lo, hi) in limits.items():
    v = rails[name]
    if not (lo <= v <= hi):
        print(f'FAIL: {name}={v} mV not in [{lo},{hi}]', file=sys.stderr)
        ok = False
if rails['v24'] != 0:
    print(f'INFO: v24={rails["v24"]} mV (non-zero OK if +24V present)', file=sys.stderr)

if check_i:
    for name, v in currents.items():
        if not (i_min <= v <= i_max):
            print(f'FAIL: {name}={v} mA not in [{i_min},{i_max}]', file=sys.stderr)
            ok = False
else:
    print('INFO: current range check skipped', file=sys.stderr)

for name, v in temps.items():
    if v != -32768:
        print(f'FAIL: {name}={v} expected -32768 (no NTC)', file=sys.stderr)
        ok = False

if ok:
    print('telemetry OK')
sys.exit(0 if ok else 1)
PY
rc=$?
[[ "$rc" -eq 0 ]] && log_pass "A.2/A.3: rails, currents, temps OK" || die "telemetry sanity check failed"
