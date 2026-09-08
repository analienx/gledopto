#!/usr/bin/env python3
"""Fail-closed semantic argument-flow proof for the GL-SD-301P ADC flash guard.

This analyzer starts from the independently unique same-model
``drv_adc_mode_pin_set`` machine-code anchor, locates its sole direct TC32 caller,
and symbolically evaluates only the constant-building instructions immediately
before that call. The TC32 ABI and per-pin argument values are learned from
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


def derive_abi(wrapper_states: dict[str, dict[str, int]]) -> tuple[str, str, int, dict[int, list[str]], dict[str, int]]:
    """Infer mode/pin registers without requiring identical codegen for every GPIO.

    TC32 emits a shorter special sequence for some constants (notably PB0), so
    requiring every wrapper to expose the same register set is too strict. The
    pin register must instead be present in a supermajority of wrappers, vary
    across at least three concrete values, and provide a unique GPIO mapping for
    every wrapper where it is present. The mode register must be present in all
    wrappers and carry one invariant non-zero value.
    """
    pins = list(wrapper_states)
    all_regs = sorted({reg for state in wrapper_states.values() for reg in state})

    mode_candidates: list[tuple[str, int]] = []
    pin_candidates: list[tuple[str, int, int]] = []
    register_coverage: dict[str, int] = {}

    for reg in all_regs:
        values = [(pin, wrapper_states[pin][reg]) for pin in pins if reg in wrapper_states[pin]]
        coverage = len(values)
        register_coverage[reg] = coverage
        distinct = {value for _, value in values}

        if coverage == len(pins) and len(distinct) == 1:
            value = next(iter(distinct))
            if value != 0:
                mode_candidates.append((reg, value))

        # Allow one codegen outlier, but demand broad coverage and real variation.
        if coverage >= len(pins) - 1 and len(distinct) >= 3:
            pin_candidates.append((reg, coverage, len(distinct)))

    if "r0" in {reg for reg, _ in mode_candidates}:
        mode_reg, expected_mode = next((reg, value) for reg, value in mode_candidates if reg == "r0")
    elif len(mode_candidates) == 1:
        mode_reg, expected_mode = mode_candidates[0]
    else:
        raise SystemExit(f"could not derive unique mode argument register: {mode_candidates}")

    if len(pin_candidates) != 1:
        raise SystemExit(f"could not derive unique pin argument register: {pin_candidates}")
    pin_reg = pin_candidates[0][0]

    pin_value_map: dict[int, list[str]] = {}
    for pin, state in wrapper_states.items():
        if pin_reg in state:
            pin_value_map.setdefault(state[pin_reg], []).append(pin)

    if any(len(names) != 1 for names in pin_value_map.values()):
        raise SystemExit(f"compiler GPIO argument map is not unique: {pin_value_map}")

    return mode_reg, pin_reg, expected_mode, pin_value_map, register_coverage


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

        wrapper_states: dict[str, dict[str, int]] = {}
        wrapper_lengths: dict[str, int] = {}
        for pin in base.ADC_PINS:
            obj = diag.compile_wrapper(cc, sdk, target, core, root / "wrappers", pin)
            rows = diag.parse_object_function(objdump, obj, "glsd_mode_caller")
            state, used = evaluate_until_call(rows)
            wrapper_states[pin] = state
            wrapper_lengths[pin] = len(used)

        mode_reg, pin_reg, expected_mode, pin_value_map, register_coverage = derive_abi(wrapper_states)

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
            "register_coverage": register_coverage,
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
