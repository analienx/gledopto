#!/usr/bin/env python3
"""Build a quarantined GL-SD-301P Zigbee OTA container offline.

This tool wraps an already-finalized Telink inner application. It performs no
network/device I/O and refuses to run without an explicit quarantine flag.
The output is a mechanics/test artifact only; it is NOT authorization to serve
or install firmware on the production device.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
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


class StagerOtaError(ValueError):
    pass


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
    parser.add_argument(
        "--offline-build-quarantined",
        action="store_true",
        help="required acknowledgment: produce an offline mechanics artifact only",
    )
    ns = parser.parse_args(argv)
    if not ns.offline_build_quarantined:
        parser.error("refusing to build bootable OTA wrapper without --offline-build-quarantined")

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
    }
    sidecar = ns.out.with_suffix(ns.out.suffix + ".quarantine.json")
    sidecar.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(metadata, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
