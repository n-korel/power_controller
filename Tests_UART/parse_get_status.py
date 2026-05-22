#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Parse GET_STATUS response hex dump (31-byte frame or 26-byte DATA)."""

from __future__ import annotations

import struct
import sys


def parse_frame(raw: bytes) -> dict[str, int]:
    if len(raw) == 26:
        data = raw
    elif len(raw) == 31 and raw[0] == 0x02 and raw[1] == 0x04:
        data = raw[3:29]
    elif len(raw) >= 29 and raw[0] == 0x02 and raw[1] == 0x04:
        data = raw[3:29]
    else:
        raise ValueError(f"expected 31-byte frame or 26-byte DATA, got {len(raw)} bytes")
    if len(data) != 26:
        raise ValueError(f"DATA len={len(data)}, expected 26")

    off = 0
    out: dict[str, int] = {}
    for name in ("v24", "v12", "v5", "v3v3", "i_lcd", "i_backlight", "i_scaler", "i_audio_l", "i_audio_r"):
        out[name] = struct.unpack_from("<H", data, off)[0]
        off += 2
    for name in ("temp0", "temp1"):
        out[name] = struct.unpack_from("<h", data, off)[0]
        off += 2
    out["state"] = data[22]
    out["fault_flags"] = struct.unpack_from("<H", data, 23)[0]
    out["inputs"] = data[25]
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
        if k in ("state", "fault_flags", "inputs"):
            print(f"{k}=0x{v:04x}" if k == "fault_flags" else f"{k}=0x{v:02x}")
        else:
            print(f"{k}={v}")
    print(f"pgood={(fields['inputs'] >> 6) & 1}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
