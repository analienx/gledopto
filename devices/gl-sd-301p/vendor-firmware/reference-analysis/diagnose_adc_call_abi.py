#!/usr/bin/env python3
"""Sanitized ABI diagnostic for the unique vendor ADC-mode call.

Prints only decoded mnemonic/register/immediate text for a tiny instruction
window around the already-established unique direct call, plus compiler-generated
public wrapper windows. Raw vendor opcode bytes are never emitted.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import tempfile

import analyze_adc_flash_guard as base
import analyze_adc_flash_guard_callgraph as cg


def parse_disassembly(objdump: Path, raw: Path) -> list[dict]:
    cp = base.run([str(objdump), "-D", "-b", "binary", "-m", "tc32", str(raw)])
    out: list[dict] = []
    for line in cp.stdout.splitlines():
        m = re.match(
            r"^\s*([0-9a-fA-F]+):\s+(?:(?:[0-9a-fA-F]{4})\s+)+([A-Za-z0-9_.]+)\s*(.*)$",
            line,
        )
        if not m:
            continue
        addr = int(m.group(1), 16)
        mnemonic = m.group(2).lower()
        operands = m.group(3).strip()
        # Remove objdump annotations/symbol decorations while preserving register
        # names, numeric immediates and effective-address expressions.
        operands = operands.split(";", 1)[0].strip()
        operands = re.sub(r"<[^>]*>", "", operands).strip()
        out.append({"addr": addr, "mnemonic": mnemonic, "operands": operands})
    return out


def parse_object_function(objdump: Path, obj: Path, function: str) -> list[dict]:
    cp = base.run([str(objdump), "-dr", str(obj)])
    active = False
    out: list[dict] = []
    for line in cp.stdout.splitlines():
        if re.match(rf"^[0-9a-fA-F]+\s+<{re.escape(function)}>":, line):
            active = True
            continue
        if active and re.match(r"^[0-9a-fA-F]+\s+<[^>]+>:", line):
            break
        if not active:
            continue
        m = re.match(
            r"^\s*([0-9a-fA-F]+):\s+(?:(?:[0-9a-fA-F]{4})\s+)+([A-Za-z0-9_.]+)\s*(.*)$",
            line,
        )
        if not m:
            continue
        operands = m.group(3).split(";", 1)[0].strip()
        operands = re.sub(r"<[^>]*>", "", operands).strip()
        out.append({"addr": int(m.group(1), 16), "mnemonic": m.group(2).lower(), "operands": operands})
    return out


def compile_wrapper(cc: Path, sdk: Path, target: Path, core: Path, root: Path, pin: str) -> Path:
    out = root / pin
    out.mkdir(parents=True)
    cfg = out / "cfg"
    cfg.mkdir()
    base.copy_target_cfg(target, cfg, pin)
    src = out / "wrapper.c"
    src.write_text(
        '#include "tl_common.h"\n'
        '#include "drv_adc.h"\n'
        '__attribute__((noinline)) unsigned int glsd_mode_caller(void) {\n'
        f'    drv_adc_mode_pin_set(DRV_ADC_VBAT_MODE, {pin});\n'
        '    return 0;\n'
        '}\n',
        encoding="utf-8",
    )
    obj = out / "wrapper.o"
    argv = base.common_compile_args(cc, sdk, cfg, core)
    argv += ["-c", str(src), "-o", str(obj)]
    base.run(argv)
    return obj


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ota", type=Path, required=True)
    ap.add_argument("--sdk-root", type=Path, required=True)
    ap.add_argument("--tc32-cc", type=Path, required=True)
    ap.add_argument("--target-dir", type=Path, default=Path("firmware/glsd301p-ed"))
    ap.add_argument("--core-dir", type=Path, default=Path("src"))
    ns = ap.parse_args()

    digest = hashlib.sha256(ns.ota.read_bytes()).hexdigest()
    if digest != base.EXPECTED_VENDOR_SHA256:
        raise SystemExit(f"vendor OTA SHA256 mismatch: {digest}")
    payload = base.extract_ota_payload(ns.ota)

    cc = ns.tc32_cc.resolve()
    sdk = ns.sdk_root.resolve()
    target = ns.target_dir.resolve()
    core = ns.core_dir.resolve()
    objdump = base.tool_sibling(cc, "tc32-elf-objdump")
    objcopy = base.tool_sibling(cc, "tc32-elf-objcopy")

    with tempfile.TemporaryDirectory(prefix="glsd-adc-abi-") as td:
        root = Path(td)
        anchor = cg.compile_exact_anchor(payload, cc, sdk, target, core, objcopy, objdump, root)
        if not anchor.get("strong_anchor"):
            raise SystemExit("exact drv_adc_mode_pin_set anchor missing")
        anchor_off = int(anchor["offset"])

        vendor_raw = root / "vendor.bin"
        vendor_raw.write_bytes(payload)
        vins = parse_disassembly(objdump, vendor_raw)
        callers = []
        for i, ins in enumerate(vins):
            if ins["mnemonic"] != "tjl":
                continue
            nums = {int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", ins["operands"])}
            if not nums:
                nums = {int(x, 16) for x in re.findall(r"(?<![#0-9A-Za-z_])([0-9a-fA-F]{4,})(?![0-9A-Za-z_])", ins["operands"])}
            if anchor_off in nums:
                callers.append((i, ins))
        if len(callers) != 1:
            raise SystemExit(f"expected one vendor direct caller, got {len(callers)}")
        idx, call = callers[0]
        vendor_window = [
            {"relative": row["addr"] - call["addr"], "mnemonic": row["mnemonic"], "operands": row["operands"]}
            for row in vins[max(0, idx - 10):idx + 1]
        ]

        wrappers = {}
        for pin in ("GPIO_PB1", "GPIO_PC4", "GPIO_PC5"):
            obj = compile_wrapper(cc, sdk, target, core, root / "wrappers", pin)
            wrappers[pin] = [
                {"relative": row["addr"], "mnemonic": row["mnemonic"], "operands": row["operands"]}
                for row in parse_object_function(objdump, obj, "glsd_mode_caller")
                if row["mnemonic"] != "tjl" or True
            ]

    print(json.dumps({
        "reference_only": True,
        "vendor_sha256": digest,
        "anchor_offset": anchor_off,
        "caller_offset": call["addr"],
        "vendor_pre_call_semantics": vendor_window,
        "public_compiler_wrappers": wrappers,
        "implementation_dependency_allowed": False,
    }, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
