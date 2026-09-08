#!/usr/bin/env python3
"""Reference-only caller analysis for the exact vendor ADC-mode helper anchor.

The semantic pass independently establishes a unique same-model machine-code
anchor for drv_adc_mode_pin_set().  This tool uses that anchor as a destination,
disassembles the vendor payload only inside the reference-analysis process,
locates direct TC32 callers, and compares the argument-materialization bytes
immediately before each call with compiler-controlled GPIO variants.

No vendor bytes or raw disassembly are printed.  Output is restricted to derived
addresses, mnemonic classes and comparison scores.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import tempfile

import analyze_adc_flash_guard as base
import analyze_adc_flash_guard_semantics as sem

PRE = 12
CALL_MASK = 6


def compile_mode_callers(
    cc: Path, sdk: Path, target: Path, core: Path, objcopy: Path, objdump: Path, root: Path,
) -> tuple[dict[str, bytes], dict[str, dict]]:
    windows: dict[str, bytes] = {}
    meta: dict[str, dict] = {}
    for pin in base.ADC_PINS:
        out = root / "caller" / pin
        out.mkdir(parents=True)
        cfg = out / "cfg"
        cfg.mkdir()
        base.copy_target_cfg(target, cfg, pin)
        src = out / "caller.c"
        src.write_text(
            '#include "tl_common.h"\n'
            '#include "drv_adc.h"\n'
            '__attribute__((noinline)) unsigned int glsd_mode_caller(void) {\n'
            f'    drv_adc_mode_pin_set(DRV_ADC_VBAT_MODE, {pin});\n'
            '    return 0;\n'
            '}\n',
            encoding="utf-8",
        )
        obj = out / "caller.o"
        argv = base.common_compile_args(cc, sdk, cfg, core)
        argv += ["-c", str(src), "-o", str(obj)]
        base.run(argv)
        section = base.find_text_section(objdump, obj, "glsd_mode_caller")
        raw = base.parse_section_bytes(objcopy, obj, section, out / "caller.bin")
        entries = []
        cp = base.run([str(objdump), "-r", "-j", section, str(obj)])
        for line in cp.stdout.splitlines():
            m = re.match(r"^\s*([0-9a-fA-F]+)\s+(R_\S+)\s+(\S+)", line)
            if m and "drv_adc_mode_pin_set" in m.group(3):
                entries.append((int(m.group(1), 16), m.group(2)))
        if len(entries) != 1:
            raise RuntimeError(f"expected one drv_adc_mode_pin_set relocation for {pin}, got {entries}")
        call_off, reloc_type = entries[0]
        if call_off < PRE:
            raise RuntimeError(f"caller too short before relocation for {pin}: {call_off}")
        windows[pin] = raw[call_off - PRE:call_off]
        meta[pin] = {
            "call_offset": call_off,
            "function_length": len(raw),
            "relocation_type": reloc_type,
        }
    return windows, meta


def variable_positions(windows: dict[str, bytes]) -> tuple[list[int], list[int]]:
    if {len(v) for v in windows.values()} != {PRE}:
        raise RuntimeError("caller pre-windows differ in length")
    stable, variable = [], []
    for i in range(PRE):
        vals = {v[i] for v in windows.values()}
        (stable if len(vals) == 1 else variable).append(i)
    return stable, variable


def compile_exact_anchor(
    payload: bytes, cc: Path, sdk: Path, target: Path, core: Path,
    objcopy: Path, objdump: Path, root: Path,
) -> dict:
    out = root / "anchor"
    out.mkdir()
    obj = sem.compile_drv_adc(cc, sdk, target, core, out)
    extracted = sem.extract_function_signature(objcopy, objdump, obj, "drv_adc_mode_pin_set", out)
    if extracted is None:
        raise RuntimeError("drv_adc_mode_pin_set function section missing")
    raw, relocs = extracted
    return sem.match_signature(payload, raw, relocs)


def parse_vendor_instructions(objdump: Path, payload: bytes, root: Path) -> list[dict]:
    raw = root / "vendor.bin"
    raw.write_bytes(payload)
    cp = base.run([str(objdump), "-D", "-b", "binary", "-m", "tc32", str(raw)])
    insns: list[dict] = []
    for line in cp.stdout.splitlines():
        m = re.match(r"^\s*([0-9a-fA-F]+):\s+[0-9a-fA-F]+\s+([A-Za-z0-9_.]+)\s*(.*)$", line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        mnemonic = m.group(2)
        operands = m.group(3)
        targets: set[int] = set()
        for hx in re.findall(r"0x([0-9a-fA-F]+)", operands):
            targets.add(int(hx, 16))
        # TC32 objdump commonly prints a bare absolute hexadecimal target.
        if not targets:
            for hx in re.findall(r"(?<![#0-9A-Za-z_])([0-9a-fA-F]{4,})(?![0-9A-Za-z_])", operands):
                targets.add(int(hx, 16))
        insns.append({"addr": addr, "mnemonic": mnemonic, "targets": targets})
    return insns


def find_direct_callers(insns: list[dict], target: int) -> list[dict]:
    callers = []
    for ins in insns:
        mnem = ins["mnemonic"].lower()
        if target in ins["targets"] and ("call" in mnem or mnem in {"tcall", "call"}):
            callers.append({"offset": ins["addr"], "mnemonic": ins["mnemonic"]})
    return callers


def score_callsite(payload: bytes, call_off: int, windows: dict[str, bytes], stable: list[int], variable: list[int]) -> dict:
    if call_off < PRE:
        return {"offset": call_off, "usable": False}
    observed = payload[call_off - PRE:call_off]
    ranked = []
    for pin, sig in windows.items():
        stable_hits = sum(observed[i] == sig[i] for i in stable)
        variable_hits = sum(observed[i] == sig[i] for i in variable)
        ranked.append({
            "pin": pin,
            "stable_hits": stable_hits,
            "stable_total": len(stable),
            "variable_hits": variable_hits,
            "variable_total": len(variable),
            "variable_score": round(variable_hits / len(variable), 6) if variable else 0.0,
            "total_score": round((stable_hits + variable_hits) / PRE, 6),
        })
    ranked.sort(key=lambda x: (-x["variable_hits"], -x["stable_hits"], -x["total_score"], x["pin"]))
    margin = ranked[0]["variable_hits"] - ranked[1]["variable_hits"] if len(ranked) > 1 else 0
    return {
        "offset": call_off,
        "usable": True,
        "ranking": ranked,
        "variable_hit_margin": margin,
    }


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
    objcopy = base.tool_sibling(cc, "tc32-elf-objcopy")
    objdump = base.tool_sibling(cc, "tc32-elf-objdump")

    with tempfile.TemporaryDirectory(prefix="glsd-adc-callgraph-") as td:
        root = Path(td)
        anchor = compile_exact_anchor(payload, cc, sdk, target, core, objcopy, objdump, root)
        if not anchor.get("strong_anchor"):
            report = {
                "reference_only": True,
                "vendor_sha256": digest,
                "anchor": anchor,
                "confidence_gate": "INCONCLUSIVE",
                "derived_pin": None,
                "implementation_dependency_allowed": False,
            }
            print(json.dumps(report, indent=2, sort_keys=True))
            return 3

        anchor_offset = int(anchor["offset"])
        windows, caller_meta = compile_mode_callers(cc, sdk, target, core, objcopy, objdump, root)
        stable, variable = variable_positions(windows)
        insns = parse_vendor_instructions(objdump, payload, root)
        callers = find_direct_callers(insns, anchor_offset)
        evaluated = [score_callsite(payload, c["offset"], windows, stable, variable) | {"mnemonic": c["mnemonic"]} for c in callers]

    candidates = []
    for row in evaluated:
        if not row.get("usable") or not row.get("ranking"):
            continue
        best = row["ranking"][0]
        second = row["ranking"][1]
        strong_ctx = best["stable_total"] == 0 or best["stable_hits"] / best["stable_total"] >= 0.60
        unique = row["variable_hit_margin"] > 0
        strong_pin = best["variable_total"] >= 2 and best["variable_score"] >= 0.75 and unique and strong_ctx
        candidates.append({
            "call_offset": row["offset"],
            "mnemonic": row["mnemonic"],
            "best_pin": best["pin"],
            "best_variable_score": best["variable_score"],
            "variable_hit_margin": row["variable_hit_margin"],
            "stable_score": round(best["stable_hits"] / best["stable_total"], 6) if best["stable_total"] else 1.0,
            "strong_pin": strong_pin,
            "runner_up": second["pin"],
        })

    strong_rows = [r for r in candidates if r["strong_pin"]]
    strong_pins = {r["best_pin"] for r in strong_rows}
    derived = next(iter(strong_pins)) if len(strong_pins) == 1 else None
    strong = derived is not None and bool(strong_rows)

    report = {
        "reference_only": True,
        "implementation_dependency_allowed": False,
        "vendor_sha256": digest,
        "payload_size": len(payload),
        "drv_adc_mode_pin_set_anchor": anchor,
        "anchor_offset": anchor_offset,
        "direct_call_count": len(callers),
        "direct_callers": candidates,
        "caller_variant_metadata": caller_meta,
        "pre_call_window_bytes": PRE,
        "stable_pre_call_bytes": len(stable),
        "pin_variable_pre_call_bytes": len(variable),
        "derived_pin": derived,
        "confidence_gate": "PASS" if strong else "INCONCLUSIVE",
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if strong else 3


if __name__ == "__main__":
    raise SystemExit(main())
