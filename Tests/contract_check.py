#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

from __future__ import annotations

import os
import re
import sys


def die(msg: str) -> None:
    print(f"contract_check: ERROR: {msg}", file=sys.stderr)
    raise SystemExit(1)


def read_text(path: str) -> str:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read()
    except OSError as e:
        die(f"cannot read {path}: {e}")


def parse_simple_yaml_map(text: str) -> dict[str, str]:
    """
    Very small YAML subset parser:
    - top-level "key: value" scalars
    - ignores nested blocks (e.g. "raw_to_mv:")
    - ignores lists ("- ...")
    Used for grabbing a few scalar keys from our contract YAMLs.
    """
    out: dict[str, str] = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("- "):
            continue
        if ":" not in line:
            continue
        k, v = line.split(":", 1)
        k = k.strip()
        v = v.strip()
        if not k:
            continue
        # Skip block openers like "raw_to_mv:"
        if v == "":
            continue
        out[k] = v
    return out


def parse_inline_object(line: str) -> dict[str, str]:
    """
    Parses: "- {a: b, c: d}" into {"a":"b","c":"d"}.
    Assumes no nested braces and no commas inside values.
    """
    s = line.strip()
    if not s.startswith("- {") or not s.endswith("}"):
        die(f"unexpected list item format: {line!r}")
    inner = s[len("- {") : -1].strip()
    if not inner:
        return {}
    parts = [p.strip() for p in inner.split(",")]
    out: dict[str, str] = {}
    for p in parts:
        if ":" not in p:
            die(f"bad inline field: {p!r} in line {line!r}")
        k, v = p.split(":", 1)
        out[k.strip()] = v.strip()
    return out


def parse_adc_channels_yaml(text: str) -> tuple[dict[str, str], list[dict[str, str]]]:
    scalars = parse_simple_yaml_map(text)
    channels: list[dict[str, str]] = []
    in_channels = False
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line == "channels:":
            in_channels = True
            continue
        if not in_channels:
            continue
        if not line.startswith("- "):
            continue
        channels.append(parse_inline_object(line))
    return scalars, channels


def parse_protocol_yaml(text: str) -> dict[str, object]:
    """
    Minimal parser for our known structure:
    - uart0.framing.stx/etx as hex
    - crc fields
    - commands list with inline objects
    - get_status.data_len and get_status.layout list with inline objects
    - domain_bits mapping
    """
    lines = [ln.rstrip("\n") for ln in text.splitlines()]

    def get_hex_value(key: str) -> int:
        m = re.search(rf"^\s*{re.escape(key)}:\s*(0x[0-9A-Fa-f]+)\s*$", text, re.M)
        if not m:
            die(f"missing key {key!r} in protocol.yaml")
        return int(m.group(1), 16)

    def get_int_value(key: str) -> int:
        m = re.search(rf"^\s*{re.escape(key)}:\s*([0-9]+)\s*$", text, re.M)
        if not m:
            die(f"missing key {key!r} in protocol.yaml")
        return int(m.group(1), 10)

    def get_str_value(key: str) -> str:
        m = re.search(rf"^\s*{re.escape(key)}:\s*(.+?)\s*$", text, re.M)
        if not m:
            die(f"missing key {key!r} in protocol.yaml")
        v = m.group(1).strip()
        if v.startswith('"') and v.endswith('"'):
            v = v[1:-1]
        return v

    # commands
    commands: list[dict[str, str]] = []
    in_commands = False
    for ln in lines:
        s = ln.strip()
        if s == "commands:":
            in_commands = True
            continue
        if in_commands:
            if s and not s.startswith("- "):
                # next section
                if not ln.startswith(" "):
                    in_commands = False
                    continue
            if s.startswith("- "):
                commands.append(parse_inline_object(s))

    # get_status layout
    gs_layout: list[dict[str, str]] = []
    in_layout = False
    for ln in lines:
        s = ln.strip()
        if s == "layout:" and re.search(r"^\s*get_status:\s*$", text, re.M):
            # not enough to disambiguate, so use indentation scan below
            pass
    # indentation-based scan: locate "get_status:" then "layout:" under it
    for i, ln in enumerate(lines):
        if ln.strip() == "get_status:":
            # search forward for "layout:"
            for j in range(i + 1, len(lines)):
                if lines[j].strip() == "layout:":
                    in_layout = True
                    for k in range(j + 1, len(lines)):
                        s = lines[k].strip()
                        if not s:
                            continue
                        if not lines[k].startswith("    - "):
                            in_layout = False
                            break
                        gs_layout.append(parse_inline_object(s))
                    break
            break

    # domain bits mapping
    domain_bits: dict[str, int] = {}
    in_domain = False
    for ln in lines:
        if ln.strip() == "domain_bits:":
            in_domain = True
            continue
        if in_domain:
            if not ln.startswith("  ") or ln.strip().startswith("-"):
                if ln and not ln.startswith("  "):
                    in_domain = False
                continue
            s = ln.strip()
            if ":" not in s:
                continue
            k, v = s.split(":", 1)
            k = k.strip()
            v = v.strip()
            if not v:
                continue
            domain_bits[k] = int(v, 10)

    return {
        "stx": get_hex_value("stx"),
        "etx": get_hex_value("etx"),
        "crc_poly": get_hex_value("poly"),
        "crc_init": get_hex_value("init"),
        "crc_refin": get_str_value("refin"),
        "crc_refout": get_str_value("refout"),
        "crc_xorout": get_hex_value("xorout"),
        "get_status_len": get_int_value("data_len"),
        "commands": commands,
        "get_status_layout": gs_layout,
        "domain_bits": domain_bits,
    }


def parse_config_h_defines(text: str) -> dict[str, int]:
    out: dict[str, int] = {}
    for m in re.finditer(r"^\s*#define\s+([A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|[0-9]+)U?\s*$", text, re.M):
        name = m.group(1)
        val_s = m.group(2)
        base = 16 if val_s.lower().startswith("0x") else 10
        out[name] = int(val_s, base)
    return out


def parse_config_h_adc_enum(text: str) -> dict[str, int]:
    out: dict[str, int] = {}
    for m in re.finditer(r"^\s*(ADC_IDX_[A-Z0-9_]+)\s*=\s*([0-9]+)\s*,?\s*(?:/\*.*\*/)?\s*$", text, re.M):
        out[m.group(1)] = int(m.group(2), 10)
    return out


def assert_eq(label: str, a, b) -> None:
    if a != b:
        die(f"{label}: expected {b!r}, got {a!r}")


def main() -> int:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    adc_yaml_path = os.path.join(repo_root, "contract", "adc_channels.yaml")
    proto_yaml_path = os.path.join(repo_root, "contract", "protocol.yaml")
    config_h_path = os.path.join(repo_root, "Config", "config.h")
    main_c_path = os.path.join(repo_root, "Core", "Src", "main.c")

    adc_yaml = read_text(adc_yaml_path)
    proto_yaml = read_text(proto_yaml_path)
    config_h = read_text(config_h_path)
    main_c = read_text(main_c_path)

    defs = parse_config_h_defines(config_h)
    adc_enum = parse_config_h_adc_enum(config_h)

    adc_scalars, adc_channels = parse_adc_channels_yaml(adc_yaml)

    # ADC scalar invariants
    assert_eq("adc.count", int(adc_scalars.get("count", "0")), defs.get("ADC_CHANNEL_COUNT"))
    assert_eq("adc.vref_mv", int(adc_scalars.get("vref_mv", "0")), defs.get("ADC_VREF_MV"))
    assert_eq("adc.alignment", adc_scalars.get("alignment"), "right")
    assert_eq("adc.resolution_bits", int(adc_scalars.get("resolution_bits", "0")), 12)

    # ADC channels order invariants vs config.h enum indices
    assert_eq("adc.channels.len", len(adc_channels), defs.get("ADC_CHANNEL_COUNT"))
    for i, ch in enumerate(adc_channels):
        rank = int(ch.get("rank", "-1"))
        dma_idx = int(ch.get("dma_idx", "-1"))
        cfg = ch.get("config_adc_idx")
        if cfg not in adc_enum:
            die(f"adc channel {i}: unknown config_adc_idx {cfg!r}")
        assert_eq(f"adc.channel[{i}].rank", rank, i + 1)
        assert_eq(f"adc.channel[{i}].dma_idx", dma_idx, i)
        assert_eq(f"adc.channel[{i}].config_adc_idx", adc_enum[cfg], i)

    # Protocol invariants vs config.h
    proto = parse_protocol_yaml(proto_yaml)
    assert_eq("proto.stx", proto["stx"], defs.get("PROTO_STX"))
    assert_eq("proto.etx", proto["etx"], defs.get("PROTO_ETX"))
    assert_eq("proto.get_status_len", proto["get_status_len"], defs.get("GET_STATUS_DATA_LEN"))

    # CRC fixed parameters
    assert_eq("proto.crc.poly", proto["crc_poly"], 0x07)
    assert_eq("proto.crc.init", proto["crc_init"], 0x00)
    assert_eq("proto.crc.xorout", proto["crc_xorout"], 0x00)
    assert_eq("proto.crc.refin", proto["crc_refin"], "false")
    assert_eq("proto.crc.refout", proto["crc_refout"], "false")

    # Command codes
    cmd_defs = {k: v for k, v in defs.items() if k.startswith("CMD_")}
    yaml_cmds = {c.get("name"): int(c.get("code", "0"), 16) for c in proto["commands"]}  # type: ignore[index]
    for name, code in yaml_cmds.items():
        if name == "NACK":
            assert_eq("CMD_NACK", code, cmd_defs.get("CMD_NACK"))
            continue
        key = f"CMD_{name}"
        if key not in cmd_defs:
            die(f"protocol.yaml command {name!r} has no matching {key} in config.h")
        assert_eq(key, code, cmd_defs[key])

    # GET_STATUS layout sanity: offsets contiguous and final size == data_len
    layout = proto["get_status_layout"]  # type: ignore[index]
    if not layout:
        die("get_status.layout is empty or not parsed")
    last_end = 0
    sizes = {"uint8": 1, "uint16_le": 2, "int16_le": 2, "uint16": 2, "int16": 2}
    for item in layout:
        off = int(item.get("offset", "-1"))
        typ = item.get("type")
        if typ not in sizes:
            die(f"get_status.layout: unknown type {typ!r}")
        if off != last_end:
            die(f"get_status.layout: non-contiguous offset at {item.get('field')}: offset={off}, expected={last_end}")
        last_end = off + sizes[typ]
    assert_eq("get_status.layout.total", last_end, proto["get_status_len"])

    # Domain bits vs DOM_* masks (bit positions)
    dom_map = {
        "SCALER": defs.get("DOM_SCALER"),
        "LCD": defs.get("DOM_LCD"),
        "BACKLIGHT": defs.get("DOM_BACKLIGHT"),
        "AUDIO": defs.get("DOM_AUDIO"),
        "ETH1": defs.get("DOM_ETH1"),
        "ETH2": defs.get("DOM_ETH2"),
        "TOUCH": defs.get("DOM_TOUCH"),
    }
    for dom_name, bitpos in proto["domain_bits"].items():  # type: ignore[union-attr]
        if dom_name not in dom_map or dom_map[dom_name] is None:
            die(f"domain_bits: no DOM_* for {dom_name!r} in config.h")
        expected_mask = 1 << int(bitpos)
        assert_eq(f"DOM_{dom_name}", dom_map[dom_name], expected_mask)

    # Runtime/implementation guards (Rules invariant #7, #48-#50)
    # - No HAL_Delay() in sequencing/state-machine modules
    # - IWDG refresh is called exactly once in main loop and nowhere else
    guard_files = [
        os.path.join(repo_root, "Services", "power_manager.c"),
        os.path.join(repo_root, "Services", "fault_manager.c"),
        os.path.join(repo_root, "Protocol", "uart_protocol.c"),
        os.path.join(repo_root, "Core", "Src", "main.c"),
        os.path.join(repo_root, "Core", "Src", "stm32f0xx_it.c"),
    ]
    for p in guard_files:
        t = read_text(p)
        if "HAL_Delay(" in t:
            die(f"HAL_Delay() is forbidden here: {os.path.relpath(p, repo_root)}")

    # IWDG refresh location/uniqueness check
    iwdg_hits: list[str] = []
    scan_dirs = [
        os.path.join(repo_root, "Core", "Src"),
        os.path.join(repo_root, "Services"),
        os.path.join(repo_root, "Protocol"),
    ]
    for d in scan_dirs:
        for name in os.listdir(d):
            if not name.endswith(".c"):
                continue
            p = os.path.join(d, name)
            t = read_text(p)
            if "HAL_IWDG_Refresh(" in t:
                iwdg_hits.append(os.path.relpath(p, repo_root))

    if iwdg_hits != ["Core/Src/main.c"]:
        die(f"IWDG refresh must appear only in Core/Src/main.c, found: {iwdg_hits}")

    # And it must be in the main loop (not in init)
    if "while (1)" not in main_c or "HAL_IWDG_Refresh(" not in main_c:
        die("main.c: missing while(1) loop or HAL_IWDG_Refresh")
    if main_c.find("HAL_IWDG_Refresh(") < main_c.find("while (1)"):
        die("main.c: HAL_IWDG_Refresh must be inside main while(1) loop")

    print("contract_check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

