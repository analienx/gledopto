#!/usr/bin/env python3
"""Reference-zone analysis of the same-model GLEDOPTO ADC/flash guard pin.

This tool is deliberately confined to vendor-firmware/reference-analysis. It
uses public Telink SDK source + the pinned TC32 compiler to create differential
machine-code signatures for each TLSR8258 ADC-capable GPIO, then compares those
signatures against a lawfully obtained same-model vendor OTA.

It prints only derived facts/scores. It never emits the vendor payload,
disassembly, or reconstructed source, and is not an implementation dependency.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
from typing import Iterable

EXPECTED_VENDOR_SHA256 = "16595a38ab9783d3afc4eb58ab4ec32625249bd569c1fa6dbaf468bddc76dd72"
OTA_MAGIC = 0x0BEEF11E
ADC_PINS = (
    "GPIO_PB0", "GPIO_PB1", "GPIO_PB2", "GPIO_PB3",
    "GPIO_PB4", "GPIO_PB5", "GPIO_PB6", "GPIO_PB7",
    "GPIO_PC4", "GPIO_PC5",
)


def run(argv: list[str], *, cwd: Path | None = None, text: bool = True) -> subprocess.CompletedProcess:
    cp = subprocess.run(argv, cwd=cwd, check=False, capture_output=True, text=text)
    if cp.returncode:
        # Tool output here is public SDK/compiler diagnostics only. Never print
        # vendor payload/disassembly from this helper.
        stderr = cp.stderr if isinstance(cp.stderr, str) else "<binary stderr>"
        stdout = cp.stdout if isinstance(cp.stdout, str) else "<binary stdout>"
        raise RuntimeError(
            f"command failed rc={cp.returncode}: {' '.join(argv[:4])}\n"
            f"stdout:\n{stdout[-4000:]}\nstderr:\n{stderr[-8000:]}"
        )
    return cp


def extract_ota_payload(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) < 62:
        raise ValueError("OTA too short")
    magic, = struct.unpack_from("<I", data, 0)
    if magic != OTA_MAGIC:
        raise ValueError(f"unexpected OTA magic 0x{magic:08x}")
    header_len, = struct.unpack_from("<H", data, 6)
    if header_len < 56 or header_len + 6 > len(data):
        raise ValueError("invalid OTA header length")
    tag, length = struct.unpack_from("<HI", data, header_len)
    if tag != 0x0000:
        raise ValueError(f"first OTA subelement is not upgrade image: 0x{tag:04x}")
    start = header_len + 6
    end = start + length
    if end != len(data):
        raise ValueError("expected exactly one upgrade-image subelement")
    return data[start:end]


def tool_sibling(cc: Path, name: str) -> Path:
    p = cc.with_name(name)
    if not p.exists():
        raise FileNotFoundError(p)
    return p


def parse_section_bytes(objcopy: Path, obj: Path, section: str, out: Path) -> bytes:
    run([str(objcopy), "-O", "binary", "--only-section", section, str(obj), str(out)])
    return out.read_bytes()


def parse_relocation_offsets(objdump: Path, obj: Path, section: str) -> set[int]:
    cp = run([str(objdump), "-r", "-j", section, str(obj)])
    offsets: set[int] = set()
    for line in cp.stdout.splitlines():
        m = re.match(r"^\s*([0-9a-fA-F]+)\s+R_", line)
        if m:
            offsets.add(int(m.group(1), 16))
    return offsets


def find_text_section(objdump: Path, obj: Path, function: str) -> str:
    cp = run([str(objdump), "-h", str(obj)])
    preferred = f".text.{function}"
    for line in cp.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[1] == preferred:
            return preferred
    raise RuntimeError(f"section {preferred!r} not found in {obj}")


def relocation_mask(length: int, relocations: Iterable[int]) -> set[int]:
    """Mask conservatively around each TC32 relocation-bearing instruction."""
    masked: set[int] = set()
    for off in relocations:
        for i in range(max(0, off - 2), min(length, off + 6)):
            masked.add(i)
    return masked


def differential_positions(variants: dict[str, bytes]) -> set[int]:
    lengths = {len(v) for v in variants.values()}
    if len(lengths) != 1:
        raise RuntimeError(f"variant lengths differ: {sorted(lengths)}")
    n = next(iter(lengths))
    return {i for i in range(n) if len({v[i] for v in variants.values()}) > 1}


def best_window_match(payload: bytes, sig: bytes, compare_positions: list[int]) -> tuple[float, int, int]:
    if not compare_positions:
        raise ValueError("empty signature mask")
    if len(payload) < len(sig):
        return 0.0, -1, 0
    best_score = -1
    best_off = -1
    exact_count = 0
    required = len(compare_positions)
    for off in range(0, len(payload) - len(sig) + 1, 2):
        score = sum(payload[off + i] == sig[i] for i in compare_positions)
        if score > best_score:
            best_score = score
            best_off = off
            exact_count = 1
        elif score == best_score:
            exact_count += 1
    return best_score / required, best_off, exact_count


def copy_target_cfg(target: Path, dst: Path, pin: str) -> None:
    for name in ("app_cfg.h", "stack_cfg.h", "version_cfg.h", "glsd301p_target_contract.h"):
        shutil.copy2(target / name, dst / name)
    p = dst / "app_cfg.h"
    text = p.read_text(encoding="utf-8")
    text, count = re.subn(
        r"^#define\s+VOLTAGE_DETECT_ADC_PIN\s+GPIO_[A-Z0-9]+\s*$",
        f"#define VOLTAGE_DETECT_ADC_PIN                  {pin}",
        text,
        flags=re.MULTILINE,
    )
    if count != 1:
        raise RuntimeError("could not replace VOLTAGE_DETECT_ADC_PIN exactly once")
    p.write_text(text, encoding="utf-8")


def sdk_include_args(sdk: Path, cfg: Path, core: Path) -> list[str]:
    roots = [sdk / "proj", sdk / "platform", sdk / "zigbee", sdk / "apps" / "common"]
    dirs = {cfg, core, sdk / "proj"}
    for root in roots:
        for p in root.rglob("*"):
            if p.is_dir():
                dirs.add(p)
    rest = sorted((p for p in dirs if p != cfg), key=lambda p: str(p))
    return [f"-I{cfg}"] + [f"-I{p}" for p in rest]


def common_compile_args(cc: Path, sdk: Path, cfg: Path, core: Path) -> list[str]:
    return [
        str(cc),
        "-O2", "-ffunction-sections", "-fdata-sections", "-fshort-enums",
        "-finline-small-functions", "-std=gnu99", "-funsigned-char", "-fshort-wchar",
        "-fms-extensions", "-nostartfiles", "-nostdlib",
        "-DMCU_CORE_8258=1", "-DEND_DEVICE=1", "-DROUTER=0", "-DCOORDINATOR=0",
        "-DMCU_STARTUP_8258=1", "-D_SIZE_T", "-D_SIZE_T_", "-D__SIZE_T", "-D__SIZE_T__",
        *sdk_include_args(sdk, cfg, core),
    ]


def compile_drv_hw_variant(cc: Path, sdk: Path, target: Path, core: Path, pin: str, out: Path) -> Path:
    cfg = out / "cfg"
    cfg.mkdir(parents=True)
    copy_target_cfg(target, cfg, pin)
    obj = out / "drv_hw.o"
    argv = common_compile_args(cc, sdk, cfg, core)
    argv.insert(10, "-fpack-struct")
    argv += ["-c", str(sdk / "proj" / "drivers" / "drv_hw.c"), "-o", str(obj)]
    run(argv)
    return obj


def compile_helper_variant(cc: Path, sdk: Path, target: Path, core: Path, pin: str, out: Path) -> Path:
    cfg = out / "cfg"
    cfg.mkdir(parents=True)
    copy_target_cfg(target, cfg, pin)
    src = out / "helper.c"
    src.write_text(
        "#include \"tl_common.h\"\n"
        "#include \"drv_adc.h\"\n"
        "__attribute__((noinline)) void glsd_adc_flash_guard_signature(void) {\n"
        "    drv_adc_init();\n"
        f"    drv_adc_mode_pin_set(DRV_ADC_VBAT_MODE, {pin});\n"
        "    drv_adc_enable(1);\n"
        "}\n",
        encoding="utf-8",
    )
    obj = out / "helper.o"
    argv = common_compile_args(cc, sdk, cfg, core)
    argv += ["-c", str(src), "-o", str(obj)]
    run(argv)
    return obj


def build_signatures(kind: str, cc: Path, sdk: Path, target: Path, core: Path, root: Path) -> tuple[dict[str, bytes], set[int]]:
    objcopy = tool_sibling(cc, "tc32-elf-objcopy")
    objdump = tool_sibling(cc, "tc32-elf-objdump")
    variants: dict[str, bytes] = {}
    relocation_union: set[int] = set()
    function = "drv_platform_init" if kind == "platform" else "glsd_adc_flash_guard_signature"
    for pin in ADC_PINS:
        out = root / kind / pin
        out.mkdir(parents=True)
        obj = (compile_drv_hw_variant if kind == "platform" else compile_helper_variant)(
            cc, sdk, target, core, pin, out
        )
        section = find_text_section(objdump, obj, function)
        raw = parse_section_bytes(objcopy, obj, section, out / "section.bin")
        variants[pin] = raw
        relocation_union |= parse_relocation_offsets(objdump, obj, section)
    return variants, relocation_union


def analyze_family(payload: bytes, variants: dict[str, bytes], relocations: set[int]) -> dict:
    variable = differential_positions(variants)
    length = len(next(iter(variants.values())))
    masked = relocation_mask(length, relocations)
    positions = [i for i in range(length) if i not in masked]
    if len(variable - masked) < 1:
        raise RuntimeError("pin variants produced no unmasked differentiating byte")
    scores = {}
    for pin, sig in variants.items():
        score, off, ties = best_window_match(payload, sig, positions)
        scores[pin] = {"score": round(score, 6), "offset": off, "best_score_ties": ties}
    ranking = sorted(scores, key=lambda p: (-scores[p]["score"], p))
    winner, runner = ranking[:2]
    return {
        "signature_length": length,
        "compared_bytes": len(positions),
        "pin_differentiating_bytes": len(variable - masked),
        "winner": winner,
        "winner_score": scores[winner]["score"],
        "runner_up": runner,
        "runner_up_score": scores[runner]["score"],
        "margin": round(scores[winner]["score"] - scores[runner]["score"], 6),
        "scores": scores,
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
    if digest != EXPECTED_VENDOR_SHA256:
        raise SystemExit(f"vendor OTA SHA256 mismatch: {digest}")
    payload = extract_ota_payload(ns.ota)

    with tempfile.TemporaryDirectory(prefix="glsd-adc-guard-") as td:
        root = Path(td)
        results = {}
        for kind in ("helper", "platform"):
            variants, relocs = build_signatures(
                kind, ns.tc32_cc.resolve(), ns.sdk_root.resolve(),
                ns.target_dir.resolve(), ns.core_dir.resolve(), root,
            )
            results[kind] = analyze_family(payload, variants, relocs)

    helper = results["helper"]
    platform = results["platform"]
    same = helper["winner"] == platform["winner"]
    strong = (
        same
        and helper["winner_score"] >= 0.90
        and platform["winner_score"] >= 0.75
        and helper["margin"] >= 0.01
        and platform["margin"] >= 0.002
    )
    report = {
        "reference_only": True,
        "vendor_sha256": digest,
        "payload_size": len(payload),
        "candidate_pins": list(ADC_PINS),
        "helper_signature": helper,
        "platform_signature": platform,
        "signatures_agree": same,
        "derived_pin": helper["winner"] if strong else None,
        "confidence_gate": "PASS" if strong else "INCONCLUSIVE",
        "implementation_dependency_allowed": False,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if strong else 3


if __name__ == "__main__":
    raise SystemExit(main())
