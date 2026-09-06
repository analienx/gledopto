#!/usr/bin/env python3
"""Build an OFFLINE-ONLY return-to-stock OTA from a guarded GL-SD dump.

The reconstructed stock Telink application is kept byte-for-byte unchanged.
Only the *outer Zigbee OTA container* uses a transport fileVersion newer than
the temporary stager so the stager's standard OTA client can accept it.

This tool performs no network/device I/O and never grants authorization.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import telink_ota_forensics as forensics
from telink_app_finalize import validate_finalized_image
from make_glsd_stager_ota import (
    OTA_MAGIC,
    OTA_HEADER_VERSION,
    OTA_FIELD_HW_RANGE,
    OTA_HW_HEADER_LEN,
    OTA_UPGRADE_TAG,
    ZIGBEE_STACK_VERSION,
    TARGET_HW_VERSION,
    DEFAULT_MANUFACTURER,
    DEFAULT_IMAGE_TYPE,
    DEFAULT_FILE_VERSION as STAGER_FILE_VERSION,
)

TARGET_IEEE = "0xa4c13850cfcdb3a4"
STOCK_FILE_VERSION = 0x26013001
RETURN_OUTER_FILE_VERSION = 0x7F010002
HEADER_STRING = b"GLSD STOCK RETURN TRANSPORT"


class StockReturnError(ValueError):
    pass


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha512(data: bytes) -> str:
    return hashlib.sha512(data).hexdigest()


def validate_dump_binding(inner: bytes, validation: dict) -> None:
    if validation.get("pass") is not True:
        raise StockReturnError("dump validation is not PASS")
    if validation.get("target_ieee") != TARGET_IEEE:
        raise StockReturnError("dump validation is not bound to the production target IEEE")
    if int(validation.get("total_len", -1)) != len(inner):
        raise StockReturnError("dump validation length does not match reconstructed stock bytes")
    if validation.get("reconstructed_sha256") != _sha256(inner):
        raise StockReturnError("reconstructed stock SHA256 does not match validation.json")

    diffs = validation.get("reconstruction_diffs")
    expected = [{"offset": forensics.TELINK_MARKER_OFFSET, "before": 0, "after": 0x4B}]
    if diffs != expected:
        raise StockReturnError("expected exactly the standard Telink +0x08 marker reconstruction")

    app = validation.get("telink_application") or {}
    if app.get("valid") is not True:
        raise StockReturnError("validation.json does not attest a valid reconstructed Telink application")

    try:
        preamble = validate_finalized_image(
            inner,
            manufacturer_code=DEFAULT_MANUFACTURER,
            image_type=DEFAULT_IMAGE_TYPE,
            file_version=STOCK_FILE_VERSION,
        )
    except ValueError as exc:
        raise StockReturnError(f"reconstructed stock inner application is invalid: {exc}") from exc
    if preamble.declared_size != len(inner):
        raise StockReturnError("reconstructed stock declared size mismatch")


def build_stock_return_ota(
    inner: bytes,
    validation: dict,
    *,
    outer_file_version: int = RETURN_OUTER_FILE_VERSION,
) -> tuple[bytes, dict]:
    validate_dump_binding(inner, validation)
    if not (STAGER_FILE_VERSION < outer_file_version <= 0xFFFFFFFF):
        raise StockReturnError("return outer fileVersion must be newer than the temporary stager")

    name = HEADER_STRING[:32].ljust(32, b"\x00")
    subelement = struct.pack("<HI", OTA_UPGRADE_TAG, len(inner)) + inner
    total_size = OTA_HW_HEADER_LEN + len(subelement)
    base = struct.pack(
        "<IHHHHHIH32sI",
        OTA_MAGIC,
        OTA_HEADER_VERSION,
        OTA_HW_HEADER_LEN,
        OTA_FIELD_HW_RANGE,
        DEFAULT_MANUFACTURER,
        DEFAULT_IMAGE_TYPE,
        outer_file_version,
        ZIGBEE_STACK_VERSION,
        name,
        total_size,
    )
    ota = base + struct.pack("<HH", TARGET_HW_VERSION, TARGET_HW_VERSION) + subelement
    if len(ota) != total_size:
        raise AssertionError("internal OTA size mismatch")

    header = forensics.parse_ota_header(ota)
    subs = forensics.parse_subelements(ota, header.header_length)
    if len(subs) != 1 or subs[0].tag_id != OTA_UPGRADE_TAG:
        raise StockReturnError("return OTA does not contain exactly one upgrade-image subelement")
    payload = ota[subs[0].data_offset:subs[0].data_end]
    if payload != inner:
        raise StockReturnError("return OTA modified reconstructed stock payload bytes")
    app = forensics.validate_telink_application(payload)
    if app.get("valid") is not True:
        raise StockReturnError("return OTA payload no longer validates as Telink application")
    if header.manufacturer_code != DEFAULT_MANUFACTURER or header.image_type != DEFAULT_IMAGE_TYPE:
        raise StockReturnError("return OTA outer identity drifted")
    if header.file_version != outer_file_version:
        raise StockReturnError("return OTA outer fileVersion drifted")
    if header.minimum_hardware_version != TARGET_HW_VERSION or header.maximum_hardware_version != TARGET_HW_VERSION:
        raise StockReturnError("return OTA hardware-version envelope drifted")

    meta = {
        "AUTHORIZATION_GRANTED": False,
        "DO_NOT_SERVE_WITHOUT_SEPARATE_SUPERVISOR_AUTHORIZATION": True,
        "targetIeee": TARGET_IEEE,
        "manufacturerCode": DEFAULT_MANUFACTURER,
        "imageType": DEFAULT_IMAGE_TYPE,
        "hardwareVersionMin": TARGET_HW_VERSION,
        "hardwareVersionMax": TARGET_HW_VERSION,
        "outerTransportFileVersion": outer_file_version,
        "innerStockFileVersion": STOCK_FILE_VERSION,
        "outerInnerVersionMismatchIntentional": True,
        "payloadUnmodified": True,
        "innerBytes": len(inner),
        "innerSha256": _sha256(inner),
        "innerSha512": _sha512(inner),
        "otaBytes": len(ota),
        "otaSha256": _sha256(ota),
        "otaSha512": _sha512(ota),
        "reconstructionDiffs": validation["reconstruction_diffs"],
        "telinkApplicationValid": True,
    }
    return ota, meta


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reconstructed_stock", type=Path)
    parser.add_argument("--validation", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--outer-file-version", type=lambda x: int(x, 0), default=RETURN_OUTER_FILE_VERSION)
    parser.add_argument(
        "--offline-build-return-quarantined",
        action="store_true",
        help="required acknowledgment: build a quarantined offline return OTA only",
    )
    ns = parser.parse_args(argv)
    if not ns.offline_build_return_quarantined:
        parser.error("refusing to build return OTA without --offline-build-return-quarantined")

    inner = ns.reconstructed_stock.read_bytes()
    validation = json.loads(ns.validation.read_text(encoding="utf-8"))
    ota, meta = build_stock_return_ota(
        inner,
        validation,
        outer_file_version=ns.outer_file_version,
    )
    ns.out.write_bytes(ota)
    ns.out.with_suffix(ns.out.suffix + ".quarantine.json").write_text(
        json.dumps(meta, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(meta, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
