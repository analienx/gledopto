#!/usr/bin/env python3
"""Repository interoperability/reference boundary guard.

Third-party firmware is allowed only in the dedicated vendor-firmware reference
zone. The guard prevents accidental propagation into implementation paths and
ensures the machine-readable interoperability contract retains confirmed
safety/compatibility facts.
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
    "devices/gl-sd-301p/interoperability/PUSH_INPUT.md",
    "devices/gl-sd-301p/interoperability/PB4_AUX_INPUT.md",
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

        if suffix in {".disasm", ".dump"} and is_vendor_reference(p):
            if "reference-analysis" not in p.parts:
                errors.append(f"RE output outside vendor reference-analysis zone: {p}")

        if ("src" in p.parts or ("firmware" in p.parts and "vendor-firmware" not in p.parts)):
            if suffix in FIRMWARE_SUFFIXES:
                errors.append(f"binary material in implementation path: {p}")

    for required in REQUIRED:
        if pathlib.PurePosixPath(required) not in file_set:
            errors.append(f"required interoperability/reference document missing: {required}")

    manifest_path = ROOT / "devices/gl-sd-301p/vendor-firmware/MANIFEST.json"
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
            if int(spec.get("schema", 0)) < 5:
                errors.append("interface schema predates solved PB4/local-key contract")

            uart = spec.get("uart", {})
            if uart.get("payload_bytes") != 6:
                errors.append("confirmed UART payload width changed from 6 without spec review")

            frame = spec.get("control_frame", {})
            if frame.get("length") != 6 or frame.get("template_hex") != "A55A000004AA":
                errors.append("confirmed control-frame template changed without spec review")

            pb4 = spec.get("pb4_aux_input", {})
            if pb4.get("gpio") != "PB4" or pb4.get("gpio_encoded") != "0x0110":
                errors.append("PB4 auxiliary GPIO contract changed without review")
            if pb4.get("consecutive_high_polls_before_action") != 11:
                errors.append("PB4 qualification threshold changed from 11 polls")
            if pb4.get("low_resets_qualification") is not True:
                errors.append("PB4 low-reset behavior must remain enabled")
            if pb4.get("logical_currentLevel_is_modified") is not False:
                errors.append("PB4 compatibility path must not mutate logical currentLevel")
            if pb4.get("business_or_physical_role") != "UNKNOWN":
                errors.append("PB4 role was semantically promoted without evidence review")
            if pb4.get("independent_compatibility_module_ready") is not True:
                errors.append("PB4 compatibility module unexpectedly not ready")

            keyboard = spec.get("local_keyboard", {})
            if keyboard.get("pb4_is_keyboard_scan_pin") is not False:
                errors.append("PB4 must not be treated as a keyboard scan pin")

            canary = bool(spec.get("first_flashable_canary_allowed"))
            production = bool(spec.get("production_encoder_ready"))
            if canary and not production:
                errors.append("first_flashable_canary_allowed=true while production encoder is not ready")
            if canary:
                required_caps = (
                    "core_onoff_level_encoder_ready",
                    "physical_push_behavior_ready",
                    "pb4_aux_behavior_ready",
                )
                for cap in required_caps:
                    if not bool(spec.get(cap)):
                        errors.append(f"canary enabled without required capability: {cap}")

    if errors:
        print("INTEROP_BOUNDARY_GUARD=FAIL")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"INTEROP_BOUNDARY_GUARD=PASS tracked_files={len(files)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
