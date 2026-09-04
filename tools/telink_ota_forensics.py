#!/usr/bin/env python3
"""Offline Zigbee/Telink OTA forensics for the GLEDOPTO GL-SD research stream.

This tool never talks to Zigbee hardware. It parses an OTA container, iterates
sub-elements, recognises the Telink header fields observed in the recovered
GLEDOPTO lineage, and evaluates common CRC32 conventions against the last four
bytes of the Telink application image.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import json
from dataclasses import dataclass, asdict
from pathlib import Path
import struct
from typing import Any

OTA_MAGIC = 0x0BEEF11E
OTA_BASE_HEADER_LEN = 56
OTA_TAG_UPGRADE_IMAGE = 0x0000


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
    return struct.unpack_from('<I', data, off)[0]


def _u16(data: bytes, off: int) -> int:
    return struct.unpack_from('<H', data, off)[0]


def parse_ota_header(data: bytes) -> OtaHeader:
    if len(data) < OTA_BASE_HEADER_LEN:
        raise ParseError('file shorter than Zigbee OTA base header')
    vals = struct.unpack_from('<IHHHHHIH32sI', data, 0)
    magic, hv, hl, fc, mfg, img, fv, stack, raw_name, total = vals
    if magic != OTA_MAGIC:
        raise ParseError(f'bad Zigbee OTA magic 0x{magic:08X}')
    if hl < OTA_BASE_HEADER_LEN or hl > len(data):
        raise ParseError(f'invalid OTA header length {hl}')
    off = OTA_BASE_HEADER_LEN
    sec = None
    dest = None
    min_hw = None
    max_hw = None
    if fc & 0x0001:
        if off + 1 > hl:
            raise ParseError('truncated security credential field')
        sec = data[off]
        off += 1
    if fc & 0x0002:
        if off + 8 > hl:
            raise ParseError('truncated upgrade destination field')
        dest = data[off:off + 8].hex()
        off += 8
    if fc & 0x0004:
        if off + 4 > hl:
            raise ParseError('truncated hardware version range')
        min_hw, max_hw = struct.unpack_from('<HH', data, off)
        off += 4
    if off != hl:
        raise ParseError(f'fieldControl/headerLength mismatch: parsed {off}, declared {hl}')
    return OtaHeader(
        file_identifier=magic,
        header_version=hv,
        header_length=hl,
        field_control=fc,
        manufacturer_code=mfg,
        image_type=img,
        file_version=fv,
        zigbee_stack_version=stack,
        header_string=raw_name.split(b'\x00', 1)[0].decode('ascii', errors='replace'),
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
            raise ParseError(f'truncated OTA sub-element header at 0x{off:X}')
        tag, length = struct.unpack_from('<HI', data, off)
        data_off = off + 6
        end = data_off + length
        if end > len(data):
            raise ParseError(f'sub-element 0x{tag:04X} overruns file: end=0x{end:X}')
        out.append(SubElement(tag, length, off, data_off, end))
        off = end
    return out


def crc_candidates(body: bytes) -> dict[str, int]:
    """Return common CRC32 representations for explicit forensic comparison."""
    z = binascii.crc32(body) & 0xFFFFFFFF
    comp = z ^ 0xFFFFFFFF
    swap = int.from_bytes(z.to_bytes(4, 'little'), 'big')
    swap_comp = int.from_bytes(comp.to_bytes(4, 'little'), 'big')
    return {
        'crc32_iso_hdlc': z,
        'crc32_iso_hdlc_complement': comp,
        'crc32_iso_hdlc_byteswapped': swap,
        'crc32_iso_hdlc_complement_byteswapped': swap_comp,
    }


def parse_telink_payload(payload: bytes) -> dict[str, Any]:
    result: dict[str, Any] = {'length': len(payload)}
    if len(payload) < 0x1C:
        result['recognition'] = 'too_short'
        return result
    result.update({
        'file_version_at_0x04': _u32(payload, 0x04),
        'boot_marker_at_0x08_hex': f'0x{_u32(payload, 0x08):08X}',
        'manufacturer_code_at_0x12': _u16(payload, 0x12),
        'image_type_at_0x14': _u16(payload, 0x14),
        'declared_app_size_at_0x18': _u32(payload, 0x18),
    })
    if len(payload) >= 4:
        trailer_le = int.from_bytes(payload[-4:], 'little')
        trailer_be = int.from_bytes(payload[-4:], 'big')
        cands = crc_candidates(payload[:-4])
        matches = [name for name, value in cands.items() if value in (trailer_le, trailer_be)]
        result['trailer_last4_hex'] = payload[-4:].hex()
        result['trailer_u32_le'] = trailer_le
        result['trailer_u32_be'] = trailer_be
        result['crc32_candidates'] = {k: f'0x{v:08X}' for k, v in cands.items()}
        result['crc_match_candidates'] = matches
        result['crc_convention_proven'] = bool(matches)
    return result


def analyze(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    header = parse_ota_header(data)
    subs = parse_subelements(data, header.header_length)
    result: dict[str, Any] = {
        'path': str(path),
        'size': len(data),
        'sha256': hashlib.sha256(data).hexdigest(),
        'sha512': hashlib.sha512(data).hexdigest(),
        'header': asdict(header),
        'total_size_matches_header': header.total_image_size == len(data),
        'subelements': [asdict(x) for x in subs],
    }
    upgrade = next((x for x in subs if x.tag_id == OTA_TAG_UPGRADE_IMAGE), None)
    if upgrade:
        payload = data[upgrade.data_offset:upgrade.data_end]
        telink = parse_telink_payload(payload)
        telink['outer_identity_matches_inner'] = (
            telink.get('manufacturer_code_at_0x12') == header.manufacturer_code
            and telink.get('image_type_at_0x14') == header.image_type
            and telink.get('file_version_at_0x04') == header.file_version
        )
        result['upgrade_image'] = telink
        result['upgrade_image_sha256'] = hashlib.sha256(payload).hexdigest()
    else:
        result['upgrade_image'] = None
    result['offline_gate'] = {
        'container_structurally_valid': header.total_image_size == len(data),
        'has_upgrade_image': upgrade is not None,
        'identity_consistent': bool(result.get('upgrade_image', {}).get('outer_identity_matches_inner')) if upgrade else False,
        'crc_convention_identified': bool(result.get('upgrade_image', {}).get('crc_convention_proven')) if upgrade else False,
    }
    return result


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('image', type=Path)
    ap.add_argument('--json', action='store_true')
    ns = ap.parse_args(argv)
    try:
        r = analyze(ns.image)
    except (OSError, ParseError, struct.error) as e:
        print(f'ERROR: {e}')
        return 2
    if ns.json:
        print(json.dumps(r, indent=2, sort_keys=True))
    else:
        h = r['header']
        print(f"{ns.image}: {r['size']} bytes SHA256={r['sha256']}")
        print(f"OTA mfg=0x{h['manufacturer_code']:04X} image=0x{h['image_type']:04X} version=0x{h['file_version']:08X} name={h['header_string']!r}")
        print('Gate:', json.dumps(r['offline_gate'], sort_keys=True))
        if r['upgrade_image']:
            print(json.dumps(r['upgrade_image'], indent=2, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
