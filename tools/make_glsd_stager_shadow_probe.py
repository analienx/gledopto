#!/usr/bin/env python3
"""Build a full-size, deliberately CRC-invalid shadow of the GL-SD stager OTA.

The input must be an already-valid quarantined GL-SD stager OTA. The output keeps
its exact size, hardware-version envelope and application bytes, except for:

* outer Zigbee OTA fileVersion -> SHADOW_FILE_VERSION;
* inner Telink fileVersion     -> SHADOW_FILE_VERSION;
* Telink xcrc32 trailer        -> recomputed value with bit 0 flipped.

The result is intentionally non-bootable under the Telink validator while
exercising the same inactive-bank byte range as the real candidate. No network
or device I/O is performed here. Live use requires separate explicit approval.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import telink_ota_forensics as forensics
from make_glsd_stager_ota import (
    DEFAULT_FILE_VERSION,
    DEFAULT_IMAGE_TYPE,
    DEFAULT_MANUFACTURER,
    OTA_FIELD_HW_RANGE,
    OTA_HW_HEADER_LEN,
    OTA_UPGRADE_TAG,
    TARGET_HW_VERSION,
)

STOCK_FILE_VERSION = 0x26013001
SHADOW_FILE_VERSION = 0x7F00FFFF
OUTER_FILE_VERSION_OFFSET = 0x0E
INNER_FILE_VERSION_OFFSET = 0x02


class ShadowProbeError(ValueError):
    pass


def _validate_candidate(candidate: bytes) -> tuple[forensics.OtaHeader, forensics.SubElement, bytes]:
    header = forensics.parse_ota_header(candidate)
    if header.header_length != OTA_HW_HEADER_LEN:
        raise ShadowProbeError("candidate does not use the expected 60-byte hw-range OTA header")
    if header.field_control != OTA_FIELD_HW_RANGE:
        raise ShadowProbeError("candidate does not carry the expected hardware-version range")
    if header.manufacturer_code != DEFAULT_MANUFACTURER:
        raise ShadowProbeError("candidate manufacturer mismatch")
    if header.image_type != DEFAULT_IMAGE_TYPE:
        raise ShadowProbeError("candidate imageType mismatch")
    if header.file_version != DEFAULT_FILE_VERSION:
        raise ShadowProbeError("candidate fileVersion is not the frozen stager version")
    if header.minimum_hardware_version != TARGET_HW_VERSION or header.maximum_hardware_version != TARGET_HW_VERSION:
        raise ShadowProbeError("candidate is not locked to hwVersion 2")
    if header.total_image_size != len(candidate):
        raise ShadowProbeError("candidate outer size mismatch")

    subs = forensics.parse_subelements(candidate, header.header_length)
    if len(subs) != 1 or subs[0].tag_id != OTA_UPGRADE_TAG:
        raise ShadowProbeError("candidate must contain exactly one upgrade-image subelement")
    sub = subs[0]
    payload = candidate[sub.data_offset:sub.data_end]
    app = forensics.validate_telink_application(payload)
    if not app.get("valid"):
        raise ShadowProbeError(f"candidate inner application is not valid: {app.get('reason')}")
    if int.from_bytes(payload[0x02:0x06], "little") != DEFAULT_FILE_VERSION:
        raise ShadowProbeError("candidate inner fileVersion mismatch")
    if int.from_bytes(payload[0x12:0x14], "little") != DEFAULT_MANUFACTURER:
        raise ShadowProbeError("candidate inner manufacturer mismatch")
    if int.from_bytes(payload[0x14:0x16], "little") != DEFAULT_IMAGE_TYPE:
        raise ShadowProbeError("candidate inner imageType mismatch")
    if not (STOCK_FILE_VERSION < SHADOW_FILE_VERSION < DEFAULT_FILE_VERSION):
        raise AssertionError("shadow version ordering invariant violated")
    return header, sub, payload


def build_shadow_probe(candidate: bytes, *, shadow_version: int = SHADOW_FILE_VERSION) -> tuple[bytes, dict]:
    header, sub, payload = _validate_candidate(candidate)
    if not (STOCK_FILE_VERSION < shadow_version < DEFAULT_FILE_VERSION):
        raise ShadowProbeError("shadow fileVersion must be above stock and below the real stager version")

    out = bytearray(candidate)
    struct.pack_into("<I", out, OUTER_FILE_VERSION_OFFSET, shadow_version)
    struct.pack_into("<I", out, sub.data_offset + INNER_FILE_VERSION_OFFSET, shadow_version)

    declared = int.from_bytes(out[sub.data_offset + 0x18:sub.data_offset + 0x1C], "little")
    if declared != len(payload):
        raise ShadowProbeError("candidate declared inner size does not equal upgrade-image length")
    crc_offset = sub.data_offset + declared - 4
    good_crc = forensics.telink_xcrc32(bytes(out[sub.data_offset:crc_offset]))
    bad_crc = good_crc ^ 0x00000001
    struct.pack_into("<I", out, crc_offset, bad_crc)
    shadow = bytes(out)

    shadow_header = forensics.parse_ota_header(shadow)
    shadow_subs = forensics.parse_subelements(shadow, shadow_header.header_length)
    shadow_sub = shadow_subs[0]
    shadow_payload = shadow[shadow_sub.data_offset:shadow_sub.data_end]
    app = forensics.validate_telink_application(shadow_payload)

    if shadow_header.file_version != shadow_version:
        raise ShadowProbeError("shadow outer fileVersion patch failed")
    if int.from_bytes(shadow_payload[0x02:0x06], "little") != shadow_version:
        raise ShadowProbeError("shadow inner fileVersion patch failed")
    if shadow_header.minimum_hardware_version != TARGET_HW_VERSION or shadow_header.maximum_hardware_version != TARGET_HW_VERSION:
        raise ShadowProbeError("shadow hardware-version envelope drifted")
    if not app.get("valid_pattern_5d02") or not app.get("marker_valid"):
        raise ShadowProbeError("shadow lost valid Telink preamble/startup marker")
    if app.get("valid") or app.get("reason") != "telink_crc_mismatch":
        raise ShadowProbeError(f"shadow fatal condition is not exact CRC mismatch: {app}")
    if int.from_bytes(shadow_payload[-4:], "little") != bad_crc:
        raise ShadowProbeError("shadow stored CRC does not match intended one-bit corruption")

    allowed = set(range(OUTER_FILE_VERSION_OFFSET, OUTER_FILE_VERSION_OFFSET + 4))
    allowed.update(range(sub.data_offset + INNER_FILE_VERSION_OFFSET, sub.data_offset + INNER_FILE_VERSION_OFFSET + 4))
    allowed.update(range(crc_offset, crc_offset + 4))
    diffs = [i for i, (a, b) in enumerate(zip(candidate, shadow)) if a != b]
    if len(candidate) != len(shadow):
        raise ShadowProbeError("shadow size drifted")
    unexpected = [i for i in diffs if i not in allowed]
    if unexpected:
        raise ShadowProbeError(f"shadow changed bytes outside allowed fields: {unexpected[:16]}")
    required_regions = [
        range(OUTER_FILE_VERSION_OFFSET, OUTER_FILE_VERSION_OFFSET + 4),
        range(sub.data_offset + INNER_FILE_VERSION_OFFSET, sub.data_offset + INNER_FILE_VERSION_OFFSET + 4),
        range(crc_offset, crc_offset + 4),
    ]
    if not all(any(i in diffs for i in region) for region in required_regions):
        raise ShadowProbeError("shadow did not change every required region")

    meta = {
        "LIVE_USE_REQUIRES_EXPLICIT_AUTHORIZATION": True,
        "INTENTIONALLY_NON_BOOTABLE": True,
        "failure_mode": "telink_xcrc32_mismatch",
        "forensics_reason": app.get("reason"),
        "manufacturerCode": shadow_header.manufacturer_code,
        "imageType": shadow_header.image_type,
        "fileVersion": shadow_version,
        "hardwareVersionMin": shadow_header.minimum_hardware_version,
        "hardwareVersionMax": shadow_header.maximum_hardware_version,
        "innerBytes": len(shadow_payload),
        "bytes": len(shadow),
        "expected_telink_xcrc32": f"0x{good_crc:08X}",
        "stored_bad_xcrc32": f"0x{bad_crc:08X}",
        "candidate_sha256": hashlib.sha256(candidate).hexdigest(),
        "candidate_sha512": hashlib.sha512(candidate).hexdigest(),
        "shadow_sha256": hashlib.sha256(shadow).hexdigest(),
        "shadow_sha512": hashlib.sha512(shadow).hexdigest(),
        "byte_diff_count": len(diffs),
        "byte_diff_offsets": diffs,
        "allowed_diff_ranges": [
            [OUTER_FILE_VERSION_OFFSET, OUTER_FILE_VERSION_OFFSET + 4],
            [sub.data_offset + INNER_FILE_VERSION_OFFSET, sub.data_offset + INNER_FILE_VERSION_OFFSET + 4],
            [crc_offset, crc_offset + 4],
        ],
    }
    return shadow, meta


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path, help="valid quarantined real stager OTA")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--offline-build-shadow-probe",
        action="store_true",
        help="required acknowledgment: create an offline non-bootable shadow only",
    )
    ns = parser.parse_args(argv)
    if not ns.offline_build_shadow_probe:
        parser.error("refusing to build shadow probe without --offline-build-shadow-probe")

    candidate = ns.candidate.read_bytes()
    shadow, meta = build_shadow_probe(candidate)
    ns.out.write_bytes(shadow)
    ns.out.with_suffix(ns.out.suffix + ".json").write_text(
        json.dumps(meta, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(meta, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
