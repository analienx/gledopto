#!/usr/bin/env python3
"""Build a quarantined GL-SD-301P Zigbee OTA container offline.

TLSR8258 normal OTA uses hardware multi-address startup: one application linked
at logical address 0 can physically boot from bank A (0x00000) or bank B
(0x40000). This tool therefore accepts only a bank-neutral TC32 build manifest
and cryptographically binds the OTA sidecar to that exact finalized inner image.

The tool performs no network/device I/O and refuses to run without an explicit
quarantine flag. Output is a mechanics/test artifact only; it is NOT
authorization to serve or install firmware on the production device.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from telink_app_finalize import (
    DEFAULT_FILE_VERSION,
    DEFAULT_IMAGE_TYPE,
    DEFAULT_MANUFACTURER,
    validate_finalized_image,
)
import telink_ota_forensics as forensics

OTA_MAGIC = 0x0BEEF11E
OTA_HEADER_VERSION = 0x0100
OTA_FIELD_HW_RANGE = 0x0004
OTA_HW_HEADER_LEN = 60
OTA_UPGRADE_TAG = 0x0000
ZIGBEE_STACK_VERSION = 0x0002
TARGET_HW_VERSION = 2
HEADER_STRING = b"GLSD READONLY DUMP STAGER"
BANK_A_BASE = 0x00000
BANK_B_BASE = 0x40000
BANK_A_SLOT_END = 0x34000
BANK_B_SLOT_END = 0x74000


class StagerOtaError(ValueError):
    pass


def _parse_manifest_int(value: str, field: str) -> int:
    try:
        return int(value, 0)
    except ValueError as exc:
        raise StagerOtaError(f"invalid {field} in build manifest: {value!r}") from exc


def validate_neutral_manifest(manifest_path: Path, inner_path: Path) -> dict:
    """Bind the OTA to the exact logical-0 multi-address TC32 link proof."""
    text = manifest_path.read_text(encoding="utf-8")
    fields: dict[str, str] = {}
    hashes: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        match = re.fullmatch(r"([0-9a-fA-F]{64})\s+(.+)", line)
        if match:
            hashes[Path(match.group(2)).name] = match.group(1).lower()
            continue
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key.strip()] = value.strip()

    required = {
        "MECHANICS_ONLY",
        "DEPLOYABLE",
        "BANK_NEUTRAL",
        "LOGICAL_LINK_BASE",
        "RUNTIME_BOOT_BANK_DETECTION",
        "PHYSICAL_BOOT_TARGET_A",
        "PHYSICAL_BOOT_TARGET_B",
        "FINAL_INNER_BINARY_SIZE",
        "PHYSICAL_A_END_EXCLUSIVE",
        "PHYSICAL_B_END_EXCLUSIVE",
        "BANK_A_SLOT_END",
        "BANK_B_SLOT_END",
        "TELINK_MULTI_ADDRESS_MODEL",
    }
    missing = sorted(required - fields.keys())
    if missing:
        raise StagerOtaError(f"build manifest missing fields: {', '.join(missing)}")
    if fields["MECHANICS_ONLY"] != "YES" or fields["DEPLOYABLE"] != "NO":
        raise StagerOtaError("build manifest is not an expected quarantined mechanics build")
    if fields["BANK_NEUTRAL"] != "YES" or fields["TELINK_MULTI_ADDRESS_MODEL"] != "PASS":
        raise StagerOtaError("build manifest does not prove the Telink multi-address model")
    if fields["RUNTIME_BOOT_BANK_DETECTION"] != "mcuBootAddrGet":
        raise StagerOtaError("runtime physical-bank detection is not mcuBootAddrGet")

    logical_base = _parse_manifest_int(fields["LOGICAL_LINK_BASE"], "LOGICAL_LINK_BASE")
    target_a = _parse_manifest_int(fields["PHYSICAL_BOOT_TARGET_A"], "PHYSICAL_BOOT_TARGET_A")
    target_b = _parse_manifest_int(fields["PHYSICAL_BOOT_TARGET_B"], "PHYSICAL_BOOT_TARGET_B")
    end_a = _parse_manifest_int(fields["PHYSICAL_A_END_EXCLUSIVE"], "PHYSICAL_A_END_EXCLUSIVE")
    end_b = _parse_manifest_int(fields["PHYSICAL_B_END_EXCLUSIVE"], "PHYSICAL_B_END_EXCLUSIVE")
    slot_a = _parse_manifest_int(fields["BANK_A_SLOT_END"], "BANK_A_SLOT_END")
    slot_b = _parse_manifest_int(fields["BANK_B_SLOT_END"], "BANK_B_SLOT_END")
    declared_inner_size = _parse_manifest_int(
        fields["FINAL_INNER_BINARY_SIZE"], "FINAL_INNER_BINARY_SIZE"
    )

    if logical_base != 0:
        raise StagerOtaError("standard TLSR8258 OTA inner image must be linked at logical address 0")
    if target_a != BANK_A_BASE or target_b != BANK_B_BASE:
        raise StagerOtaError("unexpected TLSR8258 multi-address physical boot targets")
    if slot_a != BANK_A_SLOT_END or slot_b != BANK_B_SLOT_END:
        raise StagerOtaError("unexpected TLSR8258 512K application-slot geometry")

    inner = inner_path.read_bytes()
    if declared_inner_size != len(inner):
        raise StagerOtaError(
            f"build manifest inner size {declared_inner_size} != file size {len(inner)}"
        )
    if end_a != BANK_A_BASE + len(inner) or end_b != BANK_B_BASE + len(inner):
        raise StagerOtaError("physical placement end does not match neutral image length")
    if not (end_a < slot_a and end_b < slot_b):
        raise StagerOtaError("neutral image would reach a reserved region")

    inner_sha256 = hashlib.sha256(inner).hexdigest()
    manifest_hash = hashes.get(inner_path.name)
    if manifest_hash is None:
        raise StagerOtaError(f"build manifest has no SHA-256 entry for {inner_path.name}")
    if manifest_hash != inner_sha256:
        raise StagerOtaError("finalized inner image hash does not match build manifest")

    return {
        "bankNeutral": True,
        "logicalLinkBase": 0,
        "runtimeBootBankDetection": "mcuBootAddrGet",
        "physicalBootTargets": [BANK_A_BASE, BANK_B_BASE],
        "physicalAEndExclusive": end_a,
        "physicalBEndExclusive": end_b,
        "bankASlotEnd": slot_a,
        "bankBSlotEnd": slot_b,
        "innerBytes": len(inner),
        "innerSha256": inner_sha256,
        "buildManifestSha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
    }


def build_stager_ota(
    inner: bytes,
    *,
    manufacturer_code: int = DEFAULT_MANUFACTURER,
    image_type: int = DEFAULT_IMAGE_TYPE,
    file_version: int = DEFAULT_FILE_VERSION,
    hardware_version: int = TARGET_HW_VERSION,
) -> bytes:
    preamble = validate_finalized_image(
        inner,
        manufacturer_code=manufacturer_code,
        image_type=image_type,
        file_version=file_version,
    )
    if preamble.file_version != file_version:
        raise StagerOtaError("inner/outer file version mismatch")
    if hardware_version != TARGET_HW_VERSION:
        raise StagerOtaError("GL-SD stager OTA is restricted to hwVersion 2")

    name = HEADER_STRING[:32].ljust(32, b"\x00")
    subelement = struct.pack("<HI", OTA_UPGRADE_TAG, len(inner)) + inner
    total_size = OTA_HW_HEADER_LEN + len(subelement)
    base = struct.pack(
        "<IHHHHHIH32sI",
        OTA_MAGIC,
        OTA_HEADER_VERSION,
        OTA_HW_HEADER_LEN,
        OTA_FIELD_HW_RANGE,
        manufacturer_code,
        image_type,
        file_version,
        ZIGBEE_STACK_VERSION,
        name,
        total_size,
    )
    ota = base + struct.pack("<HH", hardware_version, hardware_version) + subelement
    if len(ota) != total_size:
        raise AssertionError("internal OTA size mismatch")
    return ota


def validate_stager_ota(path: Path) -> dict:
    report = forensics.analyze(path)
    header = report["header"]
    upgrade = report.get("upgrade_image") or {}
    app = upgrade.get("application_validation") or {}
    failures: list[str] = []

    if not report.get("total_size_matches_header"):
        failures.append("outer size mismatch")
    if header.get("manufacturer_code") != DEFAULT_MANUFACTURER:
        failures.append("outer manufacturer mismatch")
    if header.get("image_type") != DEFAULT_IMAGE_TYPE:
        failures.append("outer image type mismatch")
    if header.get("file_version") != DEFAULT_FILE_VERSION:
        failures.append("outer file version mismatch")
    if header.get("field_control") != OTA_FIELD_HW_RANGE:
        failures.append("unexpected fieldControl")
    if header.get("minimum_hardware_version") != TARGET_HW_VERSION:
        failures.append("minimum hardware version mismatch")
    if header.get("maximum_hardware_version") != TARGET_HW_VERSION:
        failures.append("maximum hardware version mismatch")
    if not upgrade:
        failures.append("missing upgrade-image subelement")
    if not app.get("valid"):
        failures.append("inner Telink application invalid")
    if upgrade.get("manufacturer_code_at_0x12") != DEFAULT_MANUFACTURER:
        failures.append("inner manufacturer mismatch")
    if upgrade.get("image_type_at_0x14") != DEFAULT_IMAGE_TYPE:
        failures.append("inner image type mismatch")
    if upgrade.get("file_version_at_0x02") != DEFAULT_FILE_VERSION:
        failures.append("inner file version mismatch")
    if not upgrade.get("outer_identity_matches_inner"):
        failures.append("inner/outer identity mismatch")

    if failures:
        raise StagerOtaError("; ".join(failures))
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inner", type=Path, help="finalized bank-neutral Telink inner application")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--build-manifest", type=Path, required=True)
    parser.add_argument(
        "--offline-build-quarantined",
        action="store_true",
        help="required acknowledgment: produce an offline mechanics artifact only",
    )
    ns = parser.parse_args(argv)
    if not ns.offline_build_quarantined:
        parser.error("refusing to build bootable OTA wrapper without --offline-build-quarantined")

    placement = validate_neutral_manifest(ns.build_manifest, ns.inner)
    inner = ns.inner.read_bytes()
    ota = build_stager_ota(inner)
    ns.out.write_bytes(ota)
    report = validate_stager_ota(ns.out)
    metadata = {
        "DEPLOYABLE": False,
        "DO_NOT_SERVE_TO_PRODUCTION": True,
        "file": ns.out.name,
        "bytes": len(ota),
        "sha256": hashlib.sha256(ota).hexdigest(),
        "sha512": hashlib.sha512(ota).hexdigest(),
        "manufacturerCode": DEFAULT_MANUFACTURER,
        "imageType": DEFAULT_IMAGE_TYPE,
        "fileVersion": DEFAULT_FILE_VERSION,
        "hardwareVersionMin": TARGET_HW_VERSION,
        "hardwareVersionMax": TARGET_HW_VERSION,
        "innerValid": bool(report["upgrade_image"]["application_validation"]["valid"]),
        **placement,
    }
    sidecar = ns.out.with_suffix(ns.out.suffix + ".quarantine.json")
    sidecar.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(metadata, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
