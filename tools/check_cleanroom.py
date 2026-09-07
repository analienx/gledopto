#!/usr/bin/env python3
"""Repository interoperability/reference boundary guard.

Third-party firmware is allowed only in the dedicated vendor-firmware reference
zone.  The guard prevents accidental propagation into implementation paths and
ensures a documented interface contract remains the implementation boundary.
"""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

FIRMWARE_SUFFIXES = {".ota", ".bin", ".hex", ".elf", ".dump", ".disasm"}
ENCODED_FW_FRAGMENTS = (".ota.b64.part", ".ota.xz.b64.part")

REQUIRED = [
    "CLEAN_ROOM.md",
    "THIRD_PARTY_FIRMWARE.md",
    "devices/gl-sd-301p/interoperability/INTERFACE.md",
    "devices/gl-sd-301p/interoperability/interface.json",
    "devices/gl-sd-301p/vendor-firmware/README.md",
    "devices/gl-sd-301p/vendor-firmware/MANIFEST.json",
]


def tracked_files() -> list[pathlib.PurePosixPath]:
    result = subprocess.run(
        ["git", "ls-files", "-z"], cwd=ROOT, check=True, stdout=subprocess.PIPE
    )
    return [pathlib.PurePosixPath(p.decode()) for p in result.stdout.split(b"\0") if p]


def is_vendor_reference(p: pathlib.PurePosixPath) -> bool:
    parts = p.parts
    return len(parts) >= 4 and parts[0] == "devices" and parts[2] == "vendor-firmware"


def main() -> int:
    errors: list[str] = []
    files = tracked_files()
    file_set = set(files)

    for p in files:
        name = p.name.lower()
        suffix = p.suffix.lower()

        if suffix in FIRMWARE_SUFFIXES and not is_vendor_reference(p):
            errors.append(f"third-party firmware/RE artifact outside reference zone: {p}")

        if any(fragment in name for fragment in ENCODED_FW_FRAGMENTS):
            errors.append(f"encoded firmware transport is not canonical tracked storage: {p}")

        # Full disassembly/decompiler output belongs only in curated reference analysis.
        if suffix in {".disasm", ".dump"} and is_vendor_reference(p):
            if "reference-analysis" not in p.parts:
                errors.append(f"RE output outside vendor reference-analysis zone: {p}")

        # Never allow vendor binary/object material to masquerade as implementation.
        if "src" in p.parts or "firmware" in p.parts and "vendor-firmware" not in p.parts:
            if suffix in FIRMWARE_SUFFIXES:
                errors.append(f"binary material in implementation path: {p}")

    for required in REQUIRED:
        if pathlib.PurePosixPath(required) not in file_set:
            errors.append(f"required interoperability/reference document missing: {required}")

    manifest_path = ROOT / "devices/gl-sd-301p/vendor-firmware/MANIFEST.json"
    manifest = None
    if manifest_path.exists():
        try:
            manifest = json.loads(manifest_path.read_text())
        except Exception as exc:
            errors.append(f"invalid vendor firmware MANIFEST.json: {exc}")
        else:
            for entry in manifest.get("artifacts", []):
                if not entry.get("filename"):
                    errors.append("vendor manifest artifact without filename")
                if not entry.get("sha256"):
                    errors.append(f"vendor manifest artifact without sha256: {entry.get('filename')}")
                if not entry.get("provenance"):
                    errors.append(f"vendor manifest artifact without provenance: {entry.get('filename')}")
                if entry.get("project_license_applies") is not False:
                    errors.append(
                        f"third-party artifact must explicitly exclude project licence: {entry.get('filename')}"
                    )

            # If a canonical original is tracked, it must be represented in the manifest.
            declared = {entry.get("filename") for entry in manifest.get("artifacts", [])}
            for p in files:
                if (
                    is_vendor_reference(p)
                    and "originals" in p.parts
                    and p.suffix.lower() in {".ota", ".bin"}
                    and p.name not in declared
                ):
                    errors.append(f"vendor original missing manifest entry: {p}")

    spec_path = ROOT / "devices/gl-sd-301p/interoperability/interface.json"
    if spec_path.exists():
        try:
            spec = json.loads(spec_path.read_text())
        except Exception as exc:
            errors.append(f"invalid interface.json: {exc}")
        else:
            unknowns = spec.get("blocking_unknowns", [])
            ready = bool(spec.get("production_encoder_ready"))
            canary = bool(spec.get("first_flashable_canary_allowed"))
            if ready and unknowns:
                errors.append("production_encoder_ready=true while blocking_unknowns remain")
            if canary and not ready:
                errors.append("first_flashable_canary_allowed=true while encoder is not ready")
            if spec.get("uart", {}).get("payload_bytes") != 6:
                errors.append("confirmed UART payload width changed from 6 without spec review")

    if errors:
        print("INTEROP_BOUNDARY_GUARD=FAIL")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"INTEROP_BOUNDARY_GUARD=PASS tracked_files={len(files)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
