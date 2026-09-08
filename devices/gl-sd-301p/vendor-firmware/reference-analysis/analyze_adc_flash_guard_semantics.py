#!/usr/bin/env python3
"""Reference-only semantic matcher for the GL-SD-301P ADC/flash safety path.

This pass deliberately avoids broad drv_platform_init similarity.  It asks the
pinned TC32 toolchain to expose much smaller semantic units instead:

* public driver-source functions such as drv_adc_mode_pin_set;
* low-level 8258 driver-library symbol ownership where available;
* compiler-controlled wrappers for flash_is_zb() and adc_vbat_init(GPIO_x).

Relocation-bearing bytes are masked before matching against the lawfully
obtained same-model vendor OTA.  Only derived symbol ownership, scores, counts
and offsets are emitted.  Vendor bytes/disassembly are never printed and this
script is not an implementation dependency.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import analyze_adc_flash_guard as base

SOURCE_HELPERS = (
    "drv_adc_init",
    "drv_get_adc_data",
    "drv_adc_mode_pin_set",
    "drv_adc_enable",
)
ARCHIVE_HELPERS = (
    "flash_is_zb",
    "adc_vbat_init",
    "adc_base_init",
    "adc_init",
    "flash_safe_voltage_set",
)
MIN_STRONG_BYTES = 12
STRONG_SCORE = 0.95
PIN_SCORE = 0.90
PIN_MARGIN = 0.08


def safe_run(argv: list[str], *, cwd: Path | None = None, text: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(argv, cwd=cwd, check=False, capture_output=True, text=text)


def parse_nm_archive_line(line: str) -> dict | None:
    # GNU nm -A archive output is normally archive:member:address type symbol.
    m = re.match(r"^(.*\.a):([^:]+):\s*([0-9a-fA-F]+)\s+([A-Za-z])\s+(\S+)\s*$", line)
    if not m:
        return None
    return {
        "archive": m.group(1),
        "member": m.group(2),
        "address": int(m.group(3), 16),
        "type": m.group(4),
        "symbol": m.group(5),
    }


def discover_archive_ownership(sdk: Path, nm: Path) -> tuple[list[Path], dict[str, list[dict]]]:
    archives = sorted(
        p for p in sdk.rglob("*.a")
        if "8258" in p.name.lower() or "driver" in p.name.lower()
    )
    ownership: dict[str, list[dict]] = {h: [] for h in ARCHIVE_HELPERS}
    for archive in archives:
        cp = safe_run([str(nm), "-A", "--defined-only", str(archive)])
        if cp.returncode:
            continue
        for line in cp.stdout.splitlines():
            row = parse_nm_archive_line(line)
            if not row or row["symbol"] not in ownership:
                continue
            ownership[row["symbol"]].append({
                "archive": Path(row["archive"]).name,
                "member": row["member"],
                "type": row["type"],
            })
    return archives, ownership


def compile_drv_adc(cc: Path, sdk: Path, target: Path, core: Path, out: Path) -> Path:
    cfg = out / "cfg"
    cfg.mkdir(parents=True)
    base.copy_target_cfg(target, cfg, "GPIO_PC5")
    obj = out / "drv_adc.o"
    argv = base.common_compile_args(cc, sdk, cfg, core)
    argv.insert(10, "-fpack-struct")
    argv += ["-c", str(sdk / "proj" / "drivers" / "drv_adc.c"), "-o", str(obj)]
    base.run(argv)
    return obj


def extract_function_signature(objcopy: Path, objdump: Path, obj: Path, function: str, out: Path) -> tuple[bytes, set[int]] | None:
    try:
        section = base.find_text_section(objdump, obj, function)
    except RuntimeError:
        return None
    raw = base.parse_section_bytes(objcopy, obj, section, out / f"{function}.bin")
    relocs = base.parse_relocation_offsets(objdump, obj, section)
    return raw, relocs


def match_signature(payload: bytes, sig: bytes, relocs: set[int]) -> dict:
    masked = base.relocation_mask(len(sig), relocs)
    positions = [i for i in range(len(sig)) if i not in masked]
    if not positions:
        return {
            "signature_length": len(sig),
            "unmasked_bytes": 0,
            "score": 0.0,
            "offset": -1,
            "best_score_ties": 0,
            "strong_anchor": False,
        }
    score, off, ties = base.best_window_match(payload, sig, positions)
    strong = len(positions) >= MIN_STRONG_BYTES and score >= STRONG_SCORE and ties == 1
    return {
        "signature_length": len(sig),
        "unmasked_bytes": len(positions),
        "score": round(score, 6),
        "offset": off,
        "best_score_ties": ties,
        "strong_anchor": strong,
    }


def compile_probe(
    cc: Path, sdk: Path, target: Path, core: Path, out: Path,
    name: str, body: str, pin: str = "GPIO_PC5",
) -> tuple[Path | None, str | None]:
    cfg = out / "cfg"
    cfg.mkdir(parents=True, exist_ok=True)
    base.copy_target_cfg(target, cfg, pin)
    src = out / f"{name}.c"
    src.write_text(
        '#include "tl_common.h"\n'
        '__attribute__((noinline)) unsigned int ' + name + '(void) {\n'
        + body + '\n}\n',
        encoding="utf-8",
    )
    obj = out / f"{name}.o"
    argv = base.common_compile_args(cc, sdk, cfg, core)
    argv += ["-Werror=implicit-function-declaration", "-c", str(src), "-o", str(obj)]
    cp = safe_run(argv)
    if cp.returncode:
        # Only expose a compact diagnostic class; compiler source paths/messages can
        # otherwise make reference logs needlessly noisy.
        last = next((ln.strip() for ln in reversed(cp.stderr.splitlines()) if ln.strip()), "compile_failed")
        return None, last[:240]
    return obj, None


def probe_signature(
    cc: Path, sdk: Path, target: Path, core: Path, objcopy: Path, objdump: Path,
    root: Path, name: str, body: str, pin: str = "GPIO_PC5",
) -> tuple[dict, bytes | None, set[int] | None]:
    out = root / name / pin
    out.mkdir(parents=True, exist_ok=True)
    obj, err = compile_probe(cc, sdk, target, core, out, name, body, pin)
    if obj is None:
        return {"compile_status": "UNAVAILABLE", "diagnostic": err}, None, None
    extracted = extract_function_signature(objcopy, objdump, obj, name, out)
    if extracted is None:
        return {"compile_status": "NO_FUNCTION_SECTION"}, None, None
    raw, relocs = extracted
    entries = []
    cp = safe_run([str(objdump), "-r", str(obj)])
    if cp.returncode == 0:
        for line in cp.stdout.splitlines():
            m = re.match(r"^\s*([0-9a-fA-F]+)\s+(R_\S+)\s+(\S+)", line)
            if m:
                entries.append({"type": m.group(2), "target": m.group(3)})
    return {
        "compile_status": "PASS",
        "signature_length": len(raw),
        "relocation_targets": sorted({e["target"] for e in entries}),
    }, raw, relocs


def analyze_pin_family(
    payload: bytes, cc: Path, sdk: Path, target: Path, core: Path,
    objcopy: Path, objdump: Path, root: Path,
) -> dict:
    per_pin: dict[str, dict] = {}
    available: list[tuple[str, bytes, set[int]]] = []
    for pin in base.ADC_PINS:
        meta, raw, relocs = probe_signature(
            cc, sdk, target, core, objcopy, objdump, root,
            "glsd_probe_adc_vbat", f"    adc_vbat_init({pin});\n    return 0;", pin,
        )
        if raw is None or relocs is None:
            per_pin[pin] = meta
            continue
        match = match_signature(payload, raw, relocs)
        per_pin[pin] = {**meta, **match}
        available.append((pin, raw, relocs))

    ranked = sorted(
        (p for p in per_pin if "score" in per_pin[p]),
        key=lambda p: (-per_pin[p]["score"], -per_pin[p]["unmasked_bytes"], per_pin[p]["best_score_ties"], p),
    )
    derived = None
    margin = 0.0
    if len(ranked) >= 2:
        winner, runner = ranked[:2]
        margin = per_pin[winner]["score"] - per_pin[runner]["score"]
        if (
            per_pin[winner]["unmasked_bytes"] >= MIN_STRONG_BYTES
            and per_pin[winner]["score"] >= PIN_SCORE
            and per_pin[winner]["best_score_ties"] == 1
            and margin >= PIN_MARGIN
        ):
            derived = winner
    return {
        "per_pin": per_pin,
        "ranking": ranked,
        "winner": ranked[0] if ranked else None,
        "winner_margin": round(margin, 6),
        "derived_pin": derived,
        "confidence_gate": "PASS" if derived else "INCONCLUSIVE",
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
    nm = base.tool_sibling(cc, "tc32-elf-nm")
    objcopy = base.tool_sibling(cc, "tc32-elf-objcopy")
    objdump = base.tool_sibling(cc, "tc32-elf-objdump")

    archives, ownership = discover_archive_ownership(sdk, nm)

    with tempfile.TemporaryDirectory(prefix="glsd-adc-semantics-") as td:
        root = Path(td)
        drv_out = root / "drv_adc"
        drv_out.mkdir()
        drv_obj = compile_drv_adc(cc, sdk, target, core, drv_out)
        source_matches: dict[str, dict] = {}
        for helper in SOURCE_HELPERS:
            extracted = extract_function_signature(objcopy, objdump, drv_obj, helper, drv_out)
            if extracted is None:
                source_matches[helper] = {"compile_status": "NO_FUNCTION_SECTION"}
                continue
            raw, relocs = extracted
            source_matches[helper] = {
                "compile_status": "PASS",
                **match_signature(payload, raw, relocs),
            }

        flash_meta, flash_raw, flash_relocs = probe_signature(
            cc, sdk, target, core, objcopy, objdump, root,
            "glsd_probe_flash_is_zb", "    return flash_is_zb() ? 1u : 0u;",
        )
        flash_probe = dict(flash_meta)
        if flash_raw is not None and flash_relocs is not None:
            flash_probe.update(match_signature(payload, flash_raw, flash_relocs))

        pin_family = analyze_pin_family(payload, cc, sdk, target, core, objcopy, objdump, root)

    strong_source = sorted(k for k, v in source_matches.items() if v.get("strong_anchor"))
    strong_flash = bool(flash_probe.get("strong_anchor"))
    derived_pin = pin_family["derived_pin"]
    strong = derived_pin is not None

    report = {
        "reference_only": True,
        "implementation_dependency_allowed": False,
        "vendor_sha256": digest,
        "payload_size": len(payload),
        "driver_archives_examined": [p.name for p in archives],
        "archive_symbol_ownership": ownership,
        "source_helper_matches": source_matches,
        "strong_source_anchors": strong_source,
        "flash_is_zb_probe": flash_probe,
        "flash_is_zb_strong_anchor": strong_flash,
        "adc_vbat_pin_family": pin_family,
        "derived_pin": derived_pin,
        "confidence_gate": "PASS" if strong else "INCONCLUSIVE",
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if strong else 3


if __name__ == "__main__":
    raise SystemExit(main())
