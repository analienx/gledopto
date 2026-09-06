#!/usr/bin/env python3
"""Build a quarantined GL-SD-301P Zigbee OTA container offline.

This tool wraps an already-finalized Telink inner application. It performs no
network/device I/O and refuses to run without an explicit quarantine flag.
The CLI additionally requires the TC32 bank manifest and a target-bank label so
an image linked for bank A cannot be accidentally labeled or served as bank B.
The output is a mechanics/test artifact only; it is NOT authorization to serve
or install firmware on the production device.
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
OTA_BASE_HEADER_LEN = 56
OTA_HW_HEADER_LEN = 60
OTA_UPGRADE_TAG = 0x0000
ZIGBEE_STACK_VERSION = 0x0002
TARGET_HW_VERSION = 2
HEADER_STRING = b"GLSD READONLY DUMP STAGER"
EXPECTED_BANK_BASES = {"bank_a": 0x00000, "bank_b": 0x40000}
EXPECTED_BANK_SLOT_ENDS = {"bank_a": 0x34000, "bank_b": 0x74000}


class StagerOtaError(ValueError):
    pass


def _parse_manifest_int(value: str, field: str) -> int:
    try:
        return int(value, 0)
    except ValueError as exc:
        raise StagerOtaError(f"invalid {field} in bank manifest: {value!r}") from exc


def validate_bank_manifest(manifest_path: Path, inner_path: Path, target_bank: str) -> dict:
    """Bind a finalized inner image to the exact TC32 bank-link proof that built it."""
    if target_bank not in EXPECTED_BANK_BASES:
        raise StagerOtaError(f"unsupported target bank: {target_bank}")
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
        "BANK",
        "GLSD_STAGER_LINK_BASE",
        "FINAL_INNER_BINARY_SIZE",
        "PHYSICAL_FLASH_START",
        "PHYSICAL_FLASH_END_EXCLUSIVE",
        "APP_SLOT_END",
    }
    missing = sorted(required - fields.keys())
    if missing:
        raise StagerOtaError(f"bank manifest missing fields: {', '.join(missing)}")
    if fields["MECHANICS_ONLY"] != "YES" or fields["DEPLOYABLE"] != "NO":
        raise StagerOtaError("bank manifest is not an expected quarantined mechanics build")
    if fields["BANK"] != target_bank:
        raise StagerOtaError(
            f"bank manifest mismatch: {fields['BANK']} != requested {target_bank}"
        )

    expected_base = EXPECTED_BANK_BASES[target_bank]
    expected_slot_end = EXPECTED_BANK_SLOT_ENDS[target_bank]
    link_base = _parse_manifest_int(fields["GLSD_STAGER_LINK_BASE"], "GLSD_STAGER_LINK_BASE")
    physical_start = _parse_manifest_int(fields["PHYSICAL_FLASH_START"], "PHYSICAL_FLASH_START")
    physical_end = _parse_manifest_int(
        fields["PHYSICAL_FLASH_END_EXCLUSIVE"], "PHYSICAL_FLASH_END_EXCLUSIVE"
    )
    slot_end = _parse_manifest_int(fields["APP_SLOT_END"], "APP_SLOT_END")
    declared_inner_size = _parse_manifest_int(
        fields["FINAL_INNER_BINARY_SIZE"], "FINAL_INNER_BINARY_SIZE"
    )

    if link_base != expected_base or physical_start != expected_base:
        raise StagerOtaError(
            f"target {target_bank} must be linked at 0x{expected_base:05x}"
        )
    if slot_end != expected_slot_end:
        raise StagerOtaError(
            f"target {target_bank} slot end must be 0x{expected_slot_end:05x}"
        )

    inner = inner_path.read_bytes()
    if declared_inner_size != len(inner):
        raise StagerOtaError(
            f"bank manifest inner size {declared_inner_size} != file size {len(inner)}"
        )
    if physical_end != expected_base + len(inner):
        raise StagerOtaError("bank manifest physical end does not match inner image length")
    if physical_end >= slot_end:
        raise StagerOtaError("bank manifest places finalized image at/inside reserved region")

    inner_sha256 = hashlib.sha256(inner).hexdigest()
    manifest_hash = hashes.get(inner_path.name)
    if manifest_hash is None:
        raise StagerOtaError(f"bank manifest has no SHA-256 entry for {inner_path.name}")
    if manifest_hash != inner_sha256:
        raise StagerOtaError("finalized inner image hash does not match bank manifest")

    return {
        "targetBank": target_bank,
        "targetLinkBase": expected_base,
        "physicalFlashStart": physical_start,
        "physicalFlashEndExclusive": physical_end,
        "appSlotEnd": slot_end,
        "innerBytes": len(inner),
        "innerSha256": inner_sha256,
        "bankManifestSha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
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
    parser.add_argument("inner", type=Path, help="finalized Telink inner application")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--target-bank", choices=tuple(EXPECTED_BANK_BASES), required=True)
    parser.add_argument("--bank-manifest", type=Path, required=True)
    parser.add_argument(
        "--offline-build-quarantined",
        action="store_true",
        help="required acknowledgment: produce an offline mechanics artifact only",
    )
    ns = parser.parse_args(argv)
    if not ns.offline_build_quarantined:
        parser.error("refusing to build bootable OTA wrapper without --offline-build-quarantined")

    bank = validate_bank_manifest(ns.bank_manifest, ns.inner, ns.target_bank)
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
        **bank,
    }
    sidecar = ns.out.with_suffix(ns.out.suffix + ".quarantine.json")
    sidecar.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(metadata, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
