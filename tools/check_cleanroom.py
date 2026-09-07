#!/usr/bin/env python3
"""Repository clean-room guard.

This guard intentionally checks tracked repository material, not developers'
private analysis workspaces.  Its job is to prevent proprietary firmware and
reverse-engineering output from crossing into the implementation repository.
"""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

PROHIBITED_COMPONENTS = {"vendor-firmware", ".incoming", "re-private", "analysis-private"}
PROHIBITED_SUFFIXES = {
    ".ota",
    ".bin",
    ".hex",
    ".elf",
    ".dump",
    ".disasm",
}
PROHIBITED_NAME_FRAGMENTS = (
    ".ota.b64.part",
    ".ota.xz.b64.part",
)

REQUIRED = [
    "CLEAN_ROOM.md",
    "devices/gl-sd-301p/interoperability/INTERFACE.md",
    "devices/gl-sd-301p/interoperability/interface.json",
]


def tracked_files() -> list[pathlib.PurePosixPath]:
    result = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    return [pathlib.PurePosixPath(p.decode()) for p in result.stdout.split(b"\0") if p]


def main() -> int:
    errors: list[str] = []
    files = tracked_files()

    for p in files:
        parts = set(p.parts)
        if parts & PROHIBITED_COMPONENTS:
            errors.append(f"prohibited tracked path: {p}")
        if p.suffix.lower() in PROHIBITED_SUFFIXES:
            errors.append(f"prohibited tracked firmware/RE artifact: {p}")
        name = p.name.lower()
        if any(fragment in name for fragment in PROHIBITED_NAME_FRAGMENTS):
            errors.append(f"prohibited encoded firmware chunk: {p}")

    for required in REQUIRED:
        if pathlib.PurePosixPath(required) not in files:
            errors.append(f"required clean-room document missing: {required}")

    spec_path = ROOT / "devices/gl-sd-301p/interoperability/interface.json"
    if spec_path.exists():
        try:
            spec = json.loads(spec_path.read_text())
        except Exception as exc:  # pragma: no cover - CI diagnostic
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
        print("CLEAN_ROOM_GUARD=FAIL")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"CLEAN_ROOM_GUARD=PASS tracked_files={len(files)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
