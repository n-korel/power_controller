#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Parse GET_STATUS response hex dump (34-byte frame, or 29/27/26-byte DATA)."""

from __future__ import annotations

import struct
import sys


def parse_frame(raw: bytes) -> dict[str, int]:
    if len(raw) == 26:
        data = raw
    elif len(raw) == 31 and raw[0] == 0x02 and raw[1] == 0x04:
        data = raw[3:29]
    elif len(raw) == 32 and raw[0] == 0x02 and raw[1] == 0x04 and raw[2] == 0x1B:
        data = raw[3:30]
    elif len(raw) == 34 and raw[0] == 0x02 and raw[1] == 0x04 and raw[2] == 0x1D:
        data = raw[3:32]
    elif len(raw) >= 29 and raw[0] == 0x02 and raw[1] == 0x04:
        ln = raw[2]
        data = raw[3 : 3 + ln]
    else:
        raise ValueError(f"expected 34-byte frame (LEN=0x1D) or 26-byte DATA, got {len(raw)} bytes")
    if len(data) not in (26, 27, 29):
        raise ValueError(f"DATA len={len(data)}, expected 26, 27 or 29")

    off = 0
    out: dict[str, int] = {}
    for name in ("v24", "v12", "v5", "v3v3"):
        out[name] = struct.unpack_from("<H", data, off)[0]
        off += 2
    for name in ("i_lcd", "i_backlight", "i_scaler", "i_audio_l", "i_audio_r"):
        out[name] = struct.unpack_from("<h", data, off)[0]
        off += 2
    for name in ("temp0", "temp1"):
        out[name] = struct.unpack_from("<h", data, off)[0]
        off += 2
    out["state"] = data[22]
    out["fault_flags"] = struct.unpack_from("<H", data, 23)[0]
    out["inputs"] = data[25]
    out["dseq"] = data[26] if len(data) >= 27 else None
    out["last_power_ctrl_mask_lo"] = data[27] if len(data) >= 29 else None
    out["last_power_ctrl_value_lo"] = data[28] if len(data) >= 29 else None
    return out


def main() -> int:
    if len(sys.argv) > 1:
        raw = bytes.fromhex("".join(sys.argv[1:]).replace(" ", ""))
    else:
        raw = sys.stdin.buffer.read()

    try:
        fields = parse_frame(raw)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2

    for k, v in fields.items():
        if v is None:
            continue
        if k in ("state", "fault_flags", "inputs", "last_power_ctrl_mask_lo", "last_power_ctrl_value_lo"):
            print(f"{k}=0x{v:04x}" if k == "fault_flags" else f"{k}=0x{v:02x}")
        elif k == "dseq":
            print(f"dseq={v}")
        else:
            print(f"{k}={v}")
    print(f"pgood={(fields['inputs'] >> 6) & 1}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
