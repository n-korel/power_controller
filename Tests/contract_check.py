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

def extract_c_block_after(text: str, anchor_re: str, label: str) -> str:
    """
    Extracts a C block body for a function-like construct.
    - Finds anchor regex
    - Finds the first '{' after it
    - Returns the substring inside the matching outermost braces.
    """
    m = re.search(anchor_re, text, re.M)
    if not m:
        die(f"{label}: cannot locate anchor")
    i = text.find("{", m.end())
    if i < 0:
        die(f"{label}: cannot locate opening '{{'")
    depth = 0
    start = i + 1
    for j in range(i, len(text)):
        c = text[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[start:j]
    die(f"{label}: cannot locate matching '}}'")
    return ""

def strip_c_comments(text: str) -> str:
    # Remove /* ... */ and // ... comments for simple token checks.
    text = re.sub(r"/\*[\s\S]*?\*/", "", text)
    text = re.sub(r"//.*?$", "", text, flags=re.M)
    return text

def extract_while1_body(main_c: str) -> str:
    m = re.search(r"while\s*\(\s*1\s*\)\s*\{(?P<body>[\s\S]*?)\n\s*\}", main_c)
    if not m:
        die("main.c: cannot locate while(1){...} block")
    return m.group("body")

def list_call_statements(block: str) -> list[str]:
    """
    Extracts top-level-ish call statements from a block:
    - strips comments
    - collapses whitespace
    - returns a list of "NAME(" for every statement that looks like a function call ended by ';'
    This is intentionally strict and used only for the main while(1) contract.
    """
    t = strip_c_comments(block)
    t = re.sub(r"\s+", " ", t).strip()
    out: list[str] = []
    for stmt in t.split(";"):
        s = stmt.strip()
        if not s:
            continue
        # Ignore braces-only fragments
        if s in ("{", "}", "}{"):
            continue
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*\(", s)
        if m:
            out.append(m.group(1) + "(")
    return out


def main() -> int:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    adc_yaml_path = os.path.join(repo_root, "contract", "adc_channels.yaml")
    proto_yaml_path = os.path.join(repo_root, "contract", "protocol.yaml")
    config_h_path = os.path.join(repo_root, "Config", "config.h")
    main_c_path = os.path.join(repo_root, "Core", "Src", "main.c")
    app_c_path = os.path.join(repo_root, "Services", "app.c")

    adc_yaml = read_text(adc_yaml_path)
    proto_yaml = read_text(proto_yaml_path)
    config_h = read_text(config_h_path)
    main_c = read_text(main_c_path)
    app_c = read_text(app_c_path)

    # No-float contract (Rules_POWER.md invariant #9)
    # Disallow float/double usage in application logic paths (Cortex-M0).
    # Exclude Drivers/ and test code; this script checks firmware sources only.
    no_float_dirs = [
        os.path.join(repo_root, "Core"),
        os.path.join(repo_root, "Services"),
        os.path.join(repo_root, "Protocol"),
        os.path.join(repo_root, "Config"),
    ]
    float_pat = re.compile(r"\b(float|double)\b")
    for d in no_float_dirs:
        for root, _, files in os.walk(d):
            for name in files:
                if not (name.endswith(".c") or name.endswith(".h")):
                    continue
                p = os.path.join(root, name)
                t = read_text(p)
                t_nc = strip_c_comments(t)
                if float_pat.search(t_nc):
                    die(f"float/double is forbidden (Rules_POWER.md #9): {os.path.relpath(p, repo_root)}")

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

    # UART TX error handling contract:
    # HAL_UART_Transmit_IT errors must be escalated to Error_Handler (main-loop context)
    uart_c_path = os.path.join(repo_root, "Protocol", "uart_protocol.c")
    uart_c = read_text(uart_c_path)
    if not re.search(
        r"HAL_UART_Transmit_IT\s*\([^;]*\)\s*!=\s*HAL_OK\s*\)\s*\{[\s\S]*?Error_Handler\s*\(\s*\)\s*;",
        uart_c,
        re.M,
    ):
        die("uart_protocol.c: HAL_UART_Transmit_IT error path must call Error_Handler()")

    # And it must be in the main loop (not in init)
    if "while (1)" not in main_c or "HAL_IWDG_Refresh(" not in main_c:
        die("main.c: missing while(1) loop or HAL_IWDG_Refresh")
    if main_c.find("HAL_IWDG_Refresh(") < main_c.find("while (1)"):
        die("main.c: HAL_IWDG_Refresh must be inside main while(1) loop")

    # Main-loop call order (Rules invariant #49; POWER_Controller.md §0.2):
    # uart -> adc -> input -> power -> fault -> bootloader -> iwdg refresh
    #
    # We enforce this by extracting the first while(1) block and checking the
    # relative positions of the call tokens within that block.
    body = extract_while1_body(main_c)

    def must_appear_once_in(block: str, token: str, label: str) -> int:
        n = block.count(token)
        if n != 1:
            die(f"{label}: expected exactly one {token}, got {n}")
        return block.find(token)

    # main loop must contain exactly one IWDG refresh, and it must be after the step.
    p_iwdg = must_appear_once_in(body, "HAL_IWDG_Refresh(", "main.c while(1)")

    if "app_step();" in body:
        p_step = must_appear_once_in(body, "app_step();", "main.c while(1)")
        if not (p_step < p_iwdg):
            die("main.c while(1): app_step() must occur before HAL_IWDG_Refresh")

        # Rules_POWER.md #48: refresh only once per iteration in a single place.
        # Extra calls between app_step and refresh are forbidden: enforce exact while-body calls.
        calls = list_call_statements(body)
        if calls != ["app_step(", "HAL_IWDG_Refresh("]:
            die(f"main.c while(1): expected exactly 'app_step(); HAL_IWDG_Refresh(...);' and nothing else, got calls={calls}")

        # Enforce order inside Services/app.c: app_step must call the services in order.
        step_body = extract_c_block_after(
            app_c,
            r"^\s*void\s+app_step\s*\(\s*void\s*\)\s*$",
            "app.c app_step"
        )

        p_uart = must_appear_once_in(step_body, "uart_protocol_process();", "app_step")
        p_adc  = must_appear_once_in(step_body, "adc_service_process();", "app_step")
        p_inp  = must_appear_once_in(step_body, "input_service_process();", "app_step")
        p_pwr  = must_appear_once_in(step_body, "power_manager_process();", "app_step")
        p_flt  = must_appear_once_in(step_body, "fault_manager_process();", "app_step")
        p_btl  = must_appear_once_in(step_body, "bootloader_process();", "app_step")

        if not (p_uart < p_adc < p_inp < p_pwr < p_flt < p_btl):
            die("app_step: call order must be uart->adc->input->power->fault->bootloader")
    else:
        p_uart = must_appear_once_in(body, "uart_protocol_process();", "main.c while(1)")
        p_adc  = must_appear_once_in(body, "adc_service_process();", "main.c while(1)")
        p_inp  = must_appear_once_in(body, "input_service_process();", "main.c while(1)")
        p_pwr  = must_appear_once_in(body, "power_manager_process();", "main.c while(1)")
        p_flt  = must_appear_once_in(body, "fault_manager_process();", "main.c while(1)")
        p_btl  = must_appear_once_in(body, "bootloader_process();", "main.c while(1)")

        if not (p_uart < p_adc < p_inp < p_pwr < p_flt < p_btl < p_iwdg):
            die("main.c while(1): call order must be uart->adc->input->power->fault->bootloader->iwdg")

    # ADC DMA start contract (Rules invariant #30-#33):
    # must start DMA with ADC_CHANNEL_COUNT and adc_get_dma_buf()
    # Also enforce that DMA is started in init-phase only (never in while/app_step/process).
    adc_dma_hits: list[tuple[str, str]] = []
    for d in scan_dirs:
        for name in os.listdir(d):
            if not name.endswith(".c"):
                continue
            p = os.path.join(d, name)
            t = read_text(p)
            for m in re.finditer(r"HAL_ADC_Start_DMA\s*\((.*?)\)\s*;", t, re.S):
                adc_dma_hits.append((os.path.relpath(p, repo_root), m.group(1)))

    if len(adc_dma_hits) != 1:
        die(f"expected exactly one HAL_ADC_Start_DMA(...) call in firmware sources, got {len(adc_dma_hits)}: {[h[0] for h in adc_dma_hits]}")

    adc_path, adc_args = adc_dma_hits[0]
    if "adc_get_dma_buf()" not in adc_args:
        die("HAL_ADC_Start_DMA must pass adc_get_dma_buf() as buffer")
    if "ADC_CHANNEL_COUNT" not in adc_args:
        die("HAL_ADC_Start_DMA must use ADC_CHANNEL_COUNT as length")

    # Must not appear in main loop body or app_step (init-phase only).
    if "HAL_ADC_Start_DMA(" in body:
        die("main.c while(1): HAL_ADC_Start_DMA is forbidden (init-phase only)")
    step_body_for_adc = extract_c_block_after(
        app_c,
        r"^\s*void\s+app_step\s*\(\s*void\s*\)\s*$",
        "app.c app_step"
    )
    if "HAL_ADC_Start_DMA(" in step_body_for_adc:
        die("app_step: HAL_ADC_Start_DMA is forbidden (init-phase only)")

    # ADC init order contract: calibration must happen before DMA start.
    # Enforce inside app_init() when present; else in main.c USER init block.
    if re.search(r"^\s*void\s+app_init\s*\(\s*void\s*\)\s*$", app_c, re.M):
        init_body = extract_c_block_after(
            app_c,
            r"^\s*void\s+app_init\s*\(\s*void\s*\)\s*$",
            "app.c app_init"
        )
        p_cal = init_body.find("HAL_ADCEx_Calibration_Start(")
        p_dma = init_body.find("HAL_ADC_Start_DMA(")
        if p_cal < 0 or p_dma < 0:
            die("app_init: must call HAL_ADCEx_Calibration_Start and HAL_ADC_Start_DMA")
        if p_dma < p_cal:
            die("app_init: HAL_ADC_Start_DMA must happen after HAL_ADCEx_Calibration_Start")
    else:
        p_cal = main_c.find("HAL_ADCEx_Calibration_Start(")
        p_dma = main_c.find("HAL_ADC_Start_DMA(")
        if p_cal < 0 or p_dma < 0:
            die("main.c: must call HAL_ADCEx_Calibration_Start and HAL_ADC_Start_DMA")
        if p_dma < p_cal:
            die("main.c: HAL_ADC_Start_DMA must happen after HAL_ADCEx_Calibration_Start")

    # Init HAL status checks (coding standard: always check HAL_StatusTypeDef).
    # For critical init calls we require explicit "!= HAL_OK" guard to Error_Handler().
    def require_hal_ok_guard(fn_name: str) -> None:
        # Find "if (FN(...) != HAL_OK)" and ensure Error_Handler() appears shortly after it.
        m = re.search(rf"if\s*\(\s*{re.escape(fn_name)}\s*\([^;]*?\)\s*!=\s*HAL_OK\s*\)", main_c, re.S)
        if not m:
            die(f"main.c: missing 'if ({fn_name}(...) != HAL_OK)' guard")
        # Look ahead a small window for Error_Handler(); (handles both brace and no-brace styles)
        tail = main_c[m.end() : m.end() + 400]
        if "Error_Handler();" not in tail:
            die(f"main.c: {fn_name}() guard must call Error_Handler() on failure")

    # For app.c refactor, accept HAL_OK guards in either main.c or app.c.
    def require_hal_ok_guard_any(fn_name: str) -> None:
        for label, text in (("main.c", main_c), ("app.c", app_c)):
            m = re.search(rf"if\s*\(\s*{re.escape(fn_name)}\s*\([^;]*?\)\s*!=\s*HAL_OK\s*\)", text, re.S)
            if not m:
                continue
            tail = text[m.end() : m.end() + 400]
            if ("fault_set_flag(FAULT_INTERNAL)" not in tail) and ("Error_Handler();" not in tail):
                die(f"{label}: {fn_name}() guard must route to fault_set_flag(FAULT_INTERNAL) or Error_Handler()")
            return
        die(f"missing 'if ({fn_name}(...) != HAL_OK)' guard (main.c or app.c)")

    require_hal_ok_guard_any("HAL_ADCEx_Calibration_Start")
    require_hal_ok_guard_any("HAL_ADC_Start_DMA")
    require_hal_ok_guard_any("HAL_TIM_PWM_Start")

    print("contract_check: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

