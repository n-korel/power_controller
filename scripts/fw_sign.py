#!/usr/bin/env python3
"""
Patches the .fw_crc footer (see STM32F030XX_FLASH.ld / Services/boot_meta.c)
into build/POWER_Controller.bin and regenerates the .hex from the patched .bin.

CRC32 here is the plain zlib/CRC-32-ISO-HDLC algorithm — verified bit-identical
to sw_crc32() in boot_meta.c/flash_cal.c (same check value for "123456789":
0xCBF43926). Both this script and compute_image_crc() treat the 4 footer
bytes as zero while scanning, then the real value gets written into that slot.

Usage: fw_sign.py <build_dir> <elf_name_without_ext>
"""
import subprocess
import struct
import sys
import zlib
from pathlib import Path

FLASH_BASE = 0x08000000


def read_symbol_addr(elf_path: Path, symbol: str) -> int:
    out = subprocess.check_output(["arm-none-eabi-nm", str(elf_path)], text=True)
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == symbol:
            return int(parts[0], 16)
    raise SystemExit(f"fw_sign: symbol '{symbol}' not found in {elf_path}")


def main() -> None:
    build_dir = Path(sys.argv[1])
    target = sys.argv[2]
    elf_path = build_dir / f"{target}.elf"
    bin_path = build_dir / f"{target}.bin"
    hex_path = build_dir / f"{target}.hex"

    crc_addr = read_symbol_addr(elf_path, "g_fw_image_crc")
    end_addr = read_symbol_addr(elf_path, "_flash_image_end")
    crc_off = crc_addr - FLASH_BASE
    image_len = end_addr - FLASH_BASE

    data = bytearray(bin_path.read_bytes())
    if len(data) < image_len:
        data.extend(b"\xff" * (image_len - len(data)))
    data = data[:image_len]

    payload = bytearray(data)
    payload[crc_off : crc_off + 4] = b"\x00\x00\x00\x00"
    crc = zlib.crc32(bytes(payload)) & 0xFFFFFFFF

    data[crc_off : crc_off + 4] = struct.pack("<I", crc)
    bin_path.write_bytes(data)

    subprocess.check_call([
        "arm-none-eabi-objcopy",
        "-I",
        "binary",
        "-O",
        "ihex",
        "--change-addresses",
        hex(FLASH_BASE),
        str(bin_path),
        str(hex_path),
    ])

    print(
        f"fw_sign: image=[0x{FLASH_BASE:08X}..0x{end_addr:08X}) "
        f"({image_len} bytes), crc_footer@0x{crc_addr:08X}, crc32=0x{crc:08X}"
    )


if __name__ == "__main__":
    main()
