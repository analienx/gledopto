#!/usr/bin/env python3
"""Offline Zigbee/Telink OTA forensics for the GL-SD recovery stream.

No device I/O is performed here.  The Telink application CRC convention is the
reflected 0xEDB88320 CRC with initial 0xFFFFFFFF and no final XOR.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import json
from dataclasses import asdict, dataclass
from pathlib import Path
import struct
from typing import Any

OTA_MAGIC = 0x0BEEF11E
OTA_BASE_HEADER_LEN = 56
OTA_TAG_UPGRADE_IMAGE = 0x0000

TELINK_VALID_PATTERN = b"\x5D\x02"
# Telink valid startup marker on this B85/TLSR8258 lineage is bytes
# 4B 4E 4C 54 at +0x08, i.e. little-endian 0x544C4E4B.
TELINK_STARTUP_FLAG = 0x544C4E4B
TELINK_FILE_VERSION_OFFSET = 0x02
TELINK_MARKER_OFFSET = 0x08
TELINK_DECLARED_SIZE_OFFSET = 0x18
TELINK_APP_LIMIT = 0x34000


class ParseError(ValueError):
    pass


@dataclass
class OtaHeader:
    file_identifier: int
    header_version: int
    header_length: int
    field_control: int
    manufacturer_code: int
    image_type: int
    file_version: int
    zigbee_stack_version: int
    header_string: str
    total_image_size: int
    security_credential_version: int | None = None
    upgrade_file_destination: str | None = None
    minimum_hardware_version: int | None = None
    maximum_hardware_version: int | None = None


@dataclass
class SubElement:
    tag_id: int
    length: int
    file_offset: int
    data_offset: int
    data_end: int


def _u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def _u16(data: bytes, off: int) -> int:
    return struct.unpack_from("<H", data, off)[0]


def telink_xcrc32(data: bytes, initial: int = 0xFFFFFFFF) -> int:
    crc = initial & 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return crc & 0xFFFFFFFF


def parse_ota_header(data: bytes) -> OtaHeader:
    if len(data) < OTA_BASE_HEADER_LEN:
        raise ParseError("file shorter than Zigbee OTA base header")
    vals = struct.unpack_from("<IHHHHHIH32sI", data, 0)
    magic, hv, hl, fc, mfg, img, fv, stack, raw_name, total = vals
    if magic != OTA_MAGIC:
        raise ParseError(f"bad Zigbee OTA magic 0x{magic:08X}")
    if hl < OTA_BASE_HEADER_LEN or hl > len(data):
        raise ParseError(f"invalid OTA header length {hl}")

    off = OTA_BASE_HEADER_LEN
    sec = None
    dest = None
    min_hw = None
    max_hw = None
    if fc & 0x0001:
        if off + 1 > hl:
            raise ParseError("truncated security credential field")
        sec = data[off]
        off += 1
    if fc & 0x0002:
        if off + 8 > hl:
            raise ParseError("truncated upgrade destination field")
        dest = data[off : off + 8].hex()
        off += 8
    if fc & 0x0004:
        if off + 4 > hl:
            raise ParseError("truncated hardware version range")
        min_hw, max_hw = struct.unpack_from("<HH", data, off)
        off += 4
    if off != hl:
        raise ParseError(
            f"fieldControl/headerLength mismatch: parsed {off}, declared {hl}"
        )

    return OtaHeader(
        file_identifier=magic,
        header_version=hv,
        header_length=hl,
        field_control=fc,
        manufacturer_code=mfg,
        image_type=img,
        file_version=fv,
        zigbee_stack_version=stack,
        header_string=raw_name.split(b"\x00", 1)[0].decode("ascii", errors="replace"),
        total_image_size=total,
        security_credential_version=sec,
        upgrade_file_destination=dest,
        minimum_hardware_version=min_hw,
        maximum_hardware_version=max_hw,
    )


def parse_subelements(data: bytes, start: int) -> list[SubElement]:
    out: list[SubElement] = []
    off = start
    while off < len(data):
        if off + 6 > len(data):
            raise ParseError(f"truncated OTA sub-element header at 0x{off:X}")
        tag, length = struct.unpack_from("<HI", data, off)
        data_off = off + 6
        end = data_off + length
        if end > len(data):
            raise ParseError(
                f"sub-element 0x{tag:04X} overruns file: end=0x{end:X}"
            )
        out.append(SubElement(tag, length, off, data_off, end))
        off = end
    return out


def crc_candidates(body: bytes) -> dict[str, int]:
    z = binascii.crc32(body) & 0xFFFFFFFF
    return {
        "telink_xcrc32": telink_xcrc32(body),
        "crc32_iso_hdlc": z,
        "crc32_iso_hdlc_complement": z ^ 0xFFFFFFFF,
    }


def validate_telink_application(
    fw: bytes, *, allow_invalidated_marker: bool = False
) -> dict[str, Any]:
    result: dict[str, Any] = {"available_length": len(fw)}
    if len(fw) < 0x1C:
        result.update(valid=False, reason="too_short")
        return result

    declared = _u32(fw, TELINK_DECLARED_SIZE_OFFSET)
    marker = _u32(fw, TELINK_MARKER_OFFSET)
    expected = TELINK_STARTUP_FLAG.to_bytes(4, "little")
    valid_pattern = fw[6:8] == TELINK_VALID_PATTERN
    size_valid = 0x1C <= declared < TELINK_APP_LIMIT and declared <= len(fw)
    marker_valid = marker == TELINK_STARTUP_FLAG
    marker_invalidated = bool(
        allow_invalidated_marker
        and fw[TELINK_MARKER_OFFSET] == 0
        and fw[TELINK_MARKER_OFFSET + 1 : TELINK_MARKER_OFFSET + 4] == expected[1:]
    )

    result.update(
        declared_app_size=declared,
        size_valid=size_valid,
        valid_pattern_5d02=valid_pattern,
        marker32=f"0x{marker:08X}",
        marker_valid=marker_valid,
        marker_invalidated=marker_invalidated,
    )
    if not size_valid:
        result.update(valid=False, reason="invalid_declared_size")
        return result

    stored_crc = _u32(fw, declared - 4)
    exact_crc = telink_xcrc32(fw[: declared - 4])
    crc_valid = stored_crc == exact_crc
    result.update(
        stored_crc32=f"0x{stored_crc:08X}",
        telink_xcrc32=f"0x{exact_crc:08X}",
        telink_crc_valid=crc_valid,
    )
    result["valid"] = bool(
        valid_pattern and (marker_valid or marker_invalidated) and crc_valid
    )
    if not valid_pattern:
        result["reason"] = "missing_5d02_pattern"
    elif not (marker_valid or marker_invalidated):
        result["reason"] = "bad_startup_marker"
    elif not crc_valid:
        result["reason"] = "telink_crc_mismatch"
    else:
        result["reason"] = "ok"
    return result


def reconstruct_invalidated_telink_app(raw: bytes) -> tuple[bytes, dict[str, Any]]:
    """Restore only the boot-marker byte changed by a standard bank switch.

    Accepted marker shapes at +0x08 are exactly:
      valid stock:       4B 4E 4C 54
      invalidated stock: 00 4E 4C 54
    """
    if len(raw) < 12:
        raise ParseError("raw application too short for Telink startup marker")
    expected = TELINK_STARTUP_FLAG.to_bytes(4, "little")
    seen = raw[TELINK_MARKER_OFFSET : TELINK_MARKER_OFFSET + 4]
    if seen == expected:
        patched = bytes(raw)
        diffs: list[dict[str, int]] = []
    elif seen == b"\x00" + expected[1:]:
        out = bytearray(raw)
        before = out[TELINK_MARKER_OFFSET]
        out[TELINK_MARKER_OFFSET] = expected[0]
        patched = bytes(out)
        diffs = [
            {
                "offset": TELINK_MARKER_OFFSET,
                "before": before,
                "after": expected[0],
            }
        ]
    else:
        raise ParseError(
            "unexpected old-bank marker shape; standard Telink reconstruction "
            f"not applicable: {seen.hex()}"
        )

    validation = validate_telink_application(patched)
    return patched, {
        "diffs": diffs,
        "raw_sha256": hashlib.sha256(raw).hexdigest(),
        "reconstructed_sha256": hashlib.sha256(patched).hexdigest(),
        "validation": validation,
    }


def parse_telink_payload(payload: bytes) -> dict[str, Any]:
    result: dict[str, Any] = {"length": len(payload)}
    if len(payload) < 0x1C:
        result["recognition"] = "too_short"
        return result

    declared = _u32(payload, TELINK_DECLARED_SIZE_OFFSET)
    result.update(
        file_version_at_0x02=_u32(payload, TELINK_FILE_VERSION_OFFSET),
        boot_marker_at_0x08_hex=f"0x{_u32(payload, 0x08):08X}",
        manufacturer_code_at_0x12=_u16(payload, 0x12),
        image_type_at_0x14=_u16(payload, 0x14),
        declared_app_size_at_0x18=declared,
    )
    result["application_validation"] = validate_telink_application(payload)
    if 4 <= declared <= len(payload):
        trailer = _u32(payload, declared - 4)
        cands = crc_candidates(payload[: declared - 4])
        result["declared_tail_crc32"] = f"0x{trailer:08X}"
        result["crc32_candidates"] = {k: f"0x{v:08X}" for k, v in cands.items()}
        matches = [name for name, value in cands.items() if value == trailer]
        result["crc_match_candidates"] = matches
        result["crc_convention_proven"] = "telink_xcrc32" in matches
    else:
        result["crc_match_candidates"] = []
        result["crc_convention_proven"] = False
    return result


def analyze(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    header = parse_ota_header(data)
    subs = parse_subelements(data, header.header_length)
    result: dict[str, Any] = {
        "path": str(path),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "sha512": hashlib.sha512(data).hexdigest(),
        "header": asdict(header),
        "total_size_matches_header": header.total_image_size == len(data),
        "subelements": [asdict(x) for x in subs],
    }
    upgrade = next((x for x in subs if x.tag_id == OTA_TAG_UPGRADE_IMAGE), None)
    if upgrade is None:
        result["upgrade_image"] = None
    else:
        payload = data[upgrade.data_offset : upgrade.data_end]
        telink = parse_telink_payload(payload)
        telink["outer_identity_matches_inner"] = bool(
            telink.get("manufacturer_code_at_0x12") == header.manufacturer_code
            and telink.get("image_type_at_0x14") == header.image_type
            and telink.get("file_version_at_0x02") == header.file_version
        )
        result["upgrade_image"] = telink
        result["upgrade_image_sha256"] = hashlib.sha256(payload).hexdigest()

    upgrade_info = result.get("upgrade_image") or {}
    result["offline_gate"] = {
        "container_structurally_valid": header.total_image_size == len(data),
        "has_upgrade_image": upgrade is not None,
        "identity_consistent": bool(upgrade_info.get("outer_identity_matches_inner")),
        "telink_crc_convention_identified": bool(
            upgrade_info.get("crc_convention_proven")
        ),
        "crc_convention_identified": bool(upgrade_info.get("crc_convention_proven")),
    }
    return result


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("image", type=Path)
    ap.add_argument("--json", action="store_true")
    ns = ap.parse_args(argv)
    try:
        r = analyze(ns.image)
    except (OSError, ParseError, struct.error) as exc:
        print(f"ERROR: {exc}")
        return 2
    if ns.json:
        print(json.dumps(r, indent=2, sort_keys=True))
    else:
        h = r["header"]
        print(f"{ns.image}: {r['size']} bytes SHA256={r['sha256']}")
        print(
            f"OTA mfg=0x{h['manufacturer_code']:04X} "
            f"image=0x{h['image_type']:04X} "
            f"version=0x{h['file_version']:08X} "
            f"name={h['header_string']!r}"
        )
        print("Gate:", json.dumps(r["offline_gate"], sort_keys=True))
        if r["upgrade_image"]:
            print(json.dumps(r["upgrade_image"], indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
