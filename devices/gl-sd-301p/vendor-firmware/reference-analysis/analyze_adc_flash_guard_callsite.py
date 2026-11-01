#!/usr/bin/env python3
"""Reference-only relocation-anchored analysis for the GL-SD-301P ADC guard pin.

The whole drv_platform_init implementation differs substantially between the
public pinned SDK and the vendor image, so this pass deliberately ignores most
of the function.  It asks the pinned TC32 compiler how each legal ADC GPIO is
materialized immediately before the public voltage-detect call, masks the call
relocation itself, then searches the same-model vendor payload for the stable
local context and scores only the pin-specific bytes.

Only derived scores/offsets are printed.  Vendor bytes/disassembly are never
emitted and this file remains in the reference-analysis zone.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import tempfile

import analyze_adc_flash_guard as base

PRE = 24
POST = 10
CALL_MASK_BEFORE = 0
CALL_MASK_AFTER = 6


def relocation_entries(objdump: Path, obj: Path, section: str) -> list[dict]:
    cp = base.run([str(objdump), "-r", "-j", section, str(obj)])
    out: list[dict] = []
    for line in cp.stdout.splitlines():
        m = re.match(r"^\s*([0-9a-fA-F]+)\s+(R_\S+)\s+(\S+)", line)
        if m:
            out.append({
                "offset": int(m.group(1), 16),
                "type": m.group(2),
                "target": m.group(3),
            })
    return out


def select_guard_call(entries: list[dict]) -> dict:
    priorities = ("voltage_detect_init", "drv_adc_mode_pin_set", "adc_vbat_init")
    for needle in priorities:
        matches = [e for e in entries if needle in e["target"]]
        if len(matches) == 1:
            e = dict(matches[0])
            e["selected_by"] = needle
            return e
        if len(matches) > 1:
            raise RuntimeError(f"multiple {needle} relocations in drv_platform_init: {matches}")
    interesting = [e for e in entries if "adc" in e["target"].lower() or "voltage" in e["target"].lower()]
    raise RuntimeError(f"no voltage-detect/ADC guard call relocation found; interesting={interesting}")


def compile_windows(cc: Path, sdk: Path, target: Path, core: Path, root: Path) -> tuple[dict[str, bytes], dict[str, dict]]:
    objcopy = base.tool_sibling(cc, "tc32-elf-objcopy")
    objdump = base.tool_sibling(cc, "tc32-elf-objdump")
    windows: dict[str, bytes] = {}
    meta: dict[str, dict] = {}
    for pin in base.ADC_PINS:
        out = root / pin
        out.mkdir(parents=True)
        obj = base.compile_drv_hw_variant(cc, sdk, target, core, pin, out)
        section = base.find_text_section(objdump, obj, "drv_platform_init")
        raw = base.parse_section_bytes(objcopy, obj, section, out / "platform.bin")
        entries = relocation_entries(objdump, obj, section)
        call = select_guard_call(entries)
        off = call["offset"]
        if off < PRE or off + POST > len(raw):
            raise RuntimeError(f"guard call too close to function edge for {pin}: {off}/{len(raw)}")
        windows[pin] = raw[off - PRE:off + POST]
        meta[pin] = {
            "function_length": len(raw),
            "call_offset": off,
            "relocation_type": call["type"],
            "relocation_target_class": call["selected_by"],
        }
    classes = {m["relocation_target_class"] for m in meta.values()}
    types = {m["relocation_type"] for m in meta.values()}
    if len(classes) != 1 or len(types) != 1:
        raise RuntimeError(f"candidate call relocation drift: classes={classes} types={types}")
    return windows, meta


def positions_for_windows(windows: dict[str, bytes]) -> tuple[list[int], list[int], list[int]]:
    lengths = {len(v) for v in windows.values()}
    if lengths != {PRE + POST}:
        raise RuntimeError(f"unexpected call-window lengths: {lengths}")
    call_mask = set(range(PRE - CALL_MASK_BEFORE, min(PRE + CALL_MASK_AFTER, PRE + POST)))
    stable: list[int] = []
    variable: list[int] = []
    for i in range(PRE + POST):
        if i in call_mask:
            continue
        values = {v[i] for v in windows.values()}
        (stable if len(values) == 1 else variable).append(i)
    usable = stable + variable
    return stable, variable, usable


def best_context_offsets(payload: bytes, reference: bytes, positions: list[int], limit: int = 16) -> list[dict]:
    if not positions:
        raise RuntimeError("empty stable callsite context")
    ranked: list[tuple[int, int]] = []
    for off in range(0, len(payload) - len(reference) + 1, 2):
        score = sum(payload[off + i] == reference[i] for i in positions)
        ranked.append((score, off))
    ranked.sort(key=lambda x: (-x[0], x[1]))
    best = ranked[:limit]
    denom = len(positions)
    return [{"offset": off, "matches": score, "score": round(score / denom, 6)} for score, off in best]


def score_pins_at(payload: bytes, off: int, windows: dict[str, bytes], stable: list[int], variable: list[int]) -> list[dict]:
    out: list[dict] = []
    for pin, sig in windows.items():
        stable_hits = sum(payload[off + i] == sig[i] for i in stable)
        variable_hits = sum(payload[off + i] == sig[i] for i in variable)
        out.append({
            "pin": pin,
            "stable_hits": stable_hits,
            "stable_total": len(stable),
            "variable_hits": variable_hits,
            "variable_total": len(variable),
            "variable_score": round(variable_hits / len(variable), 6) if variable else 0.0,
            "total_score": round((stable_hits + variable_hits) / (len(stable) + len(variable)), 6),
        })
    out.sort(key=lambda x: (-x["variable_hits"], -x["total_score"], x["pin"]))
    return out


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

    with tempfile.TemporaryDirectory(prefix="glsd-adc-callsite-") as td:
        root = Path(td)
        windows, meta = compile_windows(
            ns.tc32_cc.resolve(), ns.sdk_root.resolve(), ns.target_dir.resolve(), ns.core_dir.resolve(), root
        )
        stable, variable, usable = positions_for_windows(windows)
        # Every candidate shares stable context at a relocation-relative window,
        # so any one is a valid context template.
        reference = windows[base.ADC_PINS[0]]
        contexts = best_context_offsets(payload, reference, stable, limit=24)
        evaluated = []
        for ctx in contexts:
            pins = score_pins_at(payload, ctx["offset"], windows, stable, variable)
            evaluated.append({
                **ctx,
                "pin_ranking": pins[:4],
            })

    best_ctx = evaluated[0]
    best_pin = best_ctx["pin_ranking"][0]
    second_pin = best_ctx["pin_ranking"][1]
    variable_margin = best_pin["variable_hits"] - second_pin["variable_hits"]
    unique_variable = variable_margin > 0
    strong_context = best_ctx["score"] >= 0.70 and best_ctx["matches"] >= 8
    strong = strong_context and unique_variable and best_pin["variable_score"] >= 0.75

    report = {
        "reference_only": True,
        "vendor_sha256": digest,
        "payload_size": len(payload),
        "call_window": {"pre": PRE, "post": POST},
        "public_call_metadata": meta,
        "stable_context_bytes": len(stable),
        "pin_variable_bytes": len(variable),
        "usable_unmasked_bytes": len(usable),
        "top_contexts": evaluated,
        "derived_pin": best_pin["pin"] if strong else None,
        "derived_pin_variable_score": best_pin["variable_score"],
        "variable_hit_margin": variable_margin,
        "confidence_gate": "PASS" if strong else "INCONCLUSIVE",
        "implementation_dependency_allowed": False,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if strong else 3


if __name__ == "__main__":
    raise SystemExit(main())
