#!/usr/bin/env python3
"""Fail-closed semantic argument-flow proof for the GL-SD-301P ADC flash guard.

This analyzer starts from the independently unique same-model
``drv_adc_mode_pin_set`` machine-code anchor, locates its sole direct TC32 caller,
and symbolically evaluates only the constant-building instructions immediately
before that call.  The TC32 ABI and per-pin argument values are learned from
compiler-controlled public wrappers, not hard-coded.

Only derived register constants, offsets and the symbolic GPIO result are
printed. Raw vendor bytes/disassembly are never emitted.
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
import diagnose_adc_call_abi as diag

CONTROL_FLOW = {
    "tjl", "tj", "tjeq", "tjne", "tjge", "tjgt", "tjle", "tjlt",
    "tjcs", "tjcc", "tjmi", "tjpl", "tjex",
}


def parse_reg(token: str) -> str | None:
    token = token.strip().lower()
    return token if re.fullmatch(r"r\d+", token) else None


def parse_imm(token: str) -> int | None:
    token = token.strip().lower().replace("#", "")
    try:
        return int(token, 0)
    except ValueError:
        return None


def apply_instruction(state: dict[str, int], mnemonic: str, operands: str) -> None:
    parts = [p.strip() for p in operands.split(",")]
    m = mnemonic.lower()
    if m == "tmovs" and len(parts) == 2:
        dst = parse_reg(parts[0])
        imm = parse_imm(parts[1])
        if dst is not None and imm is not None:
            state[dst] = imm & 0xFFFFFFFF
            return
    if m == "tmov" and len(parts) == 2:
        dst = parse_reg(parts[0])
        src = parse_reg(parts[1])
        if dst is not None:
            if src is not None and src in state:
                state[dst] = state[src]
            else:
                state.pop(dst, None)
            return
    if m == "tshftls" and len(parts) == 3:
        dst = parse_reg(parts[0])
        src = parse_reg(parts[1])
        shift = parse_imm(parts[2])
        if dst is not None:
            if src is not None and src in state and shift is not None:
                state[dst] = (state[src] << shift) & 0xFFFFFFFF
            else:
                state.pop(dst, None)
            return
    if m == "tadds" and len(parts) in {2, 3}:
        dst = parse_reg(parts[0])
        if dst is None:
            return
        if len(parts) == 2:
            imm = parse_imm(parts[1])
            if dst in state and imm is not None:
                state[dst] = (state[dst] + imm) & 0xFFFFFFFF
            else:
                state.pop(dst, None)
        else:
            src = parse_reg(parts[1])
            imm = parse_imm(parts[2])
            if src is not None and src in state and imm is not None:
                state[dst] = (state[src] + imm) & 0xFFFFFFFF
            else:
                state.pop(dst, None)
        return

    # Conservatively invalidate an explicit destination register for operations
    # we do not model. Comparison/branch/store operations do not define r0/r1.
    if parts:
        dst = parse_reg(parts[0])
        if dst is not None and m.startswith(("tload", "tneg", "tsub", "tmul", "tand", "tor", "txor")):
            state.pop(dst, None)


def evaluate_until_call(rows: list[dict]) -> tuple[dict[str, int], list[dict]]:
    call_index = next((i for i, row in enumerate(rows) if row["mnemonic"] == "tjl"), None)
    if call_index is None:
        raise RuntimeError("compiler wrapper has no tjl")
    state: dict[str, int] = {}
    used = rows[:call_index]
    for row in used:
        apply_instruction(state, row["mnemonic"], row["operands"])
    return state, used


def find_vendor_caller(vins: list[dict], target: int) -> tuple[int, dict]:
    matches = []
    for i, ins in enumerate(vins):
        if ins["mnemonic"] != "tjl":
            continue
        nums = {int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", ins["operands"])}
        if not nums:
            nums = {
                int(x, 16)
                for x in re.findall(r"(?<![#0-9A-Za-z_])([0-9a-fA-F]{4,})(?![0-9A-Za-z_])", ins["operands"])
            }
        if target in nums:
            matches.append((i, ins))
    if len(matches) != 1:
        raise RuntimeError(f"expected exactly one direct caller to 0x{target:x}, got {len(matches)}")
    return matches[0]


def vendor_basic_block_before(vins: list[dict], call_index: int) -> list[dict]:
    start = max(0, call_index - 16)
    for i in range(call_index - 1, start - 1, -1):
        if vins[i]["mnemonic"] in CONTROL_FLOW:
            start = i + 1
            break
    return vins[start:call_index]


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

    with tempfile.TemporaryDirectory(prefix="glsd-adc-argflow-") as td:
        root = Path(td)
        anchor = cg.compile_exact_anchor(payload, cc, sdk, target, core, objcopy, objdump, root)
        if not anchor.get("strong_anchor"):
            raise SystemExit("exact drv_adc_mode_pin_set anchor is not unique/strong")
        anchor_off = int(anchor["offset"])

        # Learn ABI + concrete per-GPIO argument values from the pinned compiler.
        wrapper_states: dict[str, dict[str, int]] = {}
        wrapper_lengths: dict[str, int] = {}
        for pin in base.ADC_PINS:
            obj = diag.compile_wrapper(cc, sdk, target, core, root / "wrappers", pin)
            rows = diag.parse_object_function(objdump, obj, "glsd_mode_caller")
            state, used = evaluate_until_call(rows)
            wrapper_states[pin] = state
            wrapper_lengths[pin] = len(used)

        common_regs = set.intersection(*(set(s) for s in wrapper_states.values()))
        varying_regs = [r for r in sorted(common_regs) if len({s[r] for s in wrapper_states.values()}) > 1]
        invariant_regs = [r for r in sorted(common_regs) if len({s[r] for s in wrapper_states.values()}) == 1]
        if len(varying_regs) != 1:
            raise SystemExit(f"could not derive unique pin argument register: {varying_regs}")
        pin_reg = varying_regs[0]

        # The mode register is the invariant register carrying the same nonzero
        # constant into every VBAT-mode wrapper. Prefer r0 if the compiler uses it.
        nonzero_invariant = [r for r in invariant_regs if wrapper_states[base.ADC_PINS[0]][r] != 0]
        if "r0" in nonzero_invariant:
            mode_reg = "r0"
        elif len(nonzero_invariant) == 1:
            mode_reg = nonzero_invariant[0]
        else:
            raise SystemExit(f"could not derive unique mode argument register: {nonzero_invariant}")
        expected_mode = wrapper_states[base.ADC_PINS[0]][mode_reg]

        pin_value_map: dict[int, list[str]] = {}
        for pin, state in wrapper_states.items():
            pin_value_map.setdefault(state[pin_reg], []).append(pin)

        vendor_raw = root / "vendor.bin"
        vendor_raw.write_bytes(payload)
        vins = diag.parse_disassembly(objdump, vendor_raw)
        call_index, call = find_vendor_caller(vins, anchor_off)
        block = vendor_basic_block_before(vins, call_index)
        vendor_state: dict[str, int] = {}
        for row in block:
            apply_instruction(vendor_state, row["mnemonic"], row["operands"])

    mode_value = vendor_state.get(mode_reg)
    pin_value = vendor_state.get(pin_reg)
    pins = pin_value_map.get(pin_value, []) if pin_value is not None else []
    derived_pin = pins[0] if len(pins) == 1 else None
    pass_gate = (
        mode_value == expected_mode
        and expected_mode != 0
        and derived_pin is not None
        and len(block) >= 2
    )

    report = {
        "reference_only": True,
        "implementation_dependency_allowed": False,
        "vendor_sha256": digest,
        "payload_size": len(payload),
        "drv_adc_mode_pin_set_anchor": {
            "offset": anchor_off,
            "score": anchor["score"],
            "unmasked_bytes": anchor["unmasked_bytes"],
            "best_score_ties": anchor["best_score_ties"],
        },
        "direct_caller": {
            "offset": call["addr"],
            "mnemonic": call["mnemonic"],
            "basic_block_instruction_count": len(block),
        },
        "compiler_derived_abi": {
            "mode_register": mode_reg,
            "pin_register": pin_reg,
            "vbat_mode_value": expected_mode,
            "wrapper_instruction_counts": wrapper_lengths,
        },
        "vendor_argument_state": {
            "mode_value": mode_value,
            "pin_value": pin_value,
        },
        "pin_value_candidates": pins,
        "derived_pin": derived_pin,
        "confidence_gate": "PASS" if pass_gate else "INCONCLUSIVE",
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if pass_gate else 3


if __name__ == "__main__":
    raise SystemExit(main())
