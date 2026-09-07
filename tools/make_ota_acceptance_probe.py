#!/usr/bin/env python3
"""Build a deliberately non-bootable GL-SD OTA acceptance probe.

The probe is structurally Telink-shaped and intentionally carries exactly one
fatal validation defect: its embedded Telink xcrc32 trailer is wrong by one bit.
The startup marker, 0x5D02 preamble, inner identity and declared size are valid.

This allows a live acceptance experiment to exercise the stock client's final
Telink image validator rather than relying on an obviously malformed startup
marker. Live use still requires separate explicit supervisor/operator approval.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

OTA_MAGIC = 0x0BEEF11E
MFG = 0x124F
IMAGE = 0x1416
BASE_VERSION = 0x26013001
HEADER_STRING = b'GLSD CRC-REJECT PROBE - NO BOOT'
TELINK_STARTUP_FLAG = 0x544C4E4B
TELINK_VALID_PATTERN = b'\x5D\x02'


def telink_xcrc32(data: bytes, initial: int = 0xFFFFFFFF) -> int:
    """Telink reflected CRC-32: init FFFFFFFF, polynomial EDB88320, no final xor."""
    crc = initial & 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return crc & 0xFFFFFFFF


def build_payload(version: int, size: int) -> bytes:
    if size < 64:
        raise ValueError('payload must be >=64 bytes')

    b = bytearray([0xA5] * size)
    struct.pack_into('<H', b, 0x00, 0x0000)
    struct.pack_into('<I', b, 0x02, version)
    b[0x06:0x08] = TELINK_VALID_PATTERN
    struct.pack_into('<I', b, 0x08, TELINK_STARTUP_FLAG)
    struct.pack_into('<H', b, 0x12, MFG)
    struct.pack_into('<H', b, 0x14, IMAGE)
    struct.pack_into('<I', b, 0x18, size)

    banner = b'NON-BOOTABLE: INTENTIONAL TELINK XCRC32 FAILURE'
    b[0x20:0x20 + len(banner)] = banner

    good_crc = telink_xcrc32(bytes(b[:-4]))
    bad_crc = good_crc ^ 0x00000001
    struct.pack_into('<I', b, size - 4, bad_crc)

    # Prove the intended single fatal condition locally.
    assert int.from_bytes(b[0x08:0x0C], 'little') == TELINK_STARTUP_FLAG
    assert b[0x06:0x08] == TELINK_VALID_PATTERN
    assert int.from_bytes(b[0x18:0x1C], 'little') == size
    assert telink_xcrc32(bytes(b[:-4])) == good_crc
    assert int.from_bytes(b[-4:], 'little') == bad_crc
    assert bad_crc != good_crc

    return bytes(b)


def build_ota(version: int, payload_size: int) -> bytes:
    # Keep the historical public builder API returning bytes; tests and offline
    # callers already depend on this shape.
    payload = build_payload(version, payload_size)
    header_len = 56
    sub = struct.pack('<HI', 0x0000, len(payload)) + payload
    total = header_len + len(sub)
    name = HEADER_STRING[:32].ljust(32, b'\x00')
    header = struct.pack(
        '<IHHHHHIH32sI', OTA_MAGIC, 0x0100, header_len, 0,
        MFG, IMAGE, version, 0x0002, name, total,
    )
    return header + sub


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--version', type=lambda x: int(x, 0), default=BASE_VERSION + 1)
    ap.add_argument('--payload-size', type=int, default=512)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--unsafe-create-probe', action='store_true')
    ns = ap.parse_args()

    if not ns.unsafe_create_probe:
        ap.error('refusing to create probe without --unsafe-create-probe acknowledgment')
    if ns.version <= BASE_VERSION:
        ap.error('probe version must be higher than stock 0x26013001')

    data = build_ota(ns.version, ns.payload_size)
    payload = data[56 + 6:]
    expected_crc = telink_xcrc32(payload[:-4])
    stored_bad_crc = int.from_bytes(payload[-4:], 'little')

    ns.out.write_bytes(data)
    meta = {
        'LIVE_USE_REQUIRES_EXPLICIT_AUTHORIZATION': True,
        'INTENTIONALLY_NON_BOOTABLE': True,
        'failure_mode': 'telink_xcrc32_mismatch',
        'startup_marker_valid': True,
        'preamble_5d02_valid': True,
        'inner_identity_matches_outer': True,
        'version': ns.version,
        'size': len(data),
        'sha256': hashlib.sha256(data).hexdigest(),
        'sha512': hashlib.sha512(data).hexdigest(),
        'expected_telink_xcrc32': f'0x{expected_crc:08X}',
        'stored_bad_xcrc32': f'0x{stored_bad_crc:08X}',
    }
    ns.out.with_suffix(ns.out.suffix + '.json').write_text(json.dumps(meta, indent=2) + '\n')
    print(json.dumps(meta, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
