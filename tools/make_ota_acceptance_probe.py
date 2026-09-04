#!/usr/bin/env python3
"""Build an OFFLINE-ONLY GL-SD OTA acceptance probe.

The generated file intentionally has a wrong Telink payload trailer and is not
an executable firmware. It exists to exercise parsers/server tooling offline.
Do not serve it to hardware until the live-probe gate has been separately
approved after matching a real GLEDOPTO image's verification convention.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import json
from pathlib import Path
import struct

OTA_MAGIC = 0x0BEEF11E
MFG = 0x124F
IMAGE = 0x1416
BASE_VERSION = 0x26013001
HEADER_STRING = b'GLSD ACCEPTANCE PROBE - NO BOOT'


def build_payload(version: int, size: int) -> bytes:
    if size < 64:
        raise ValueError('payload must be >=64 bytes')
    b = bytearray([0xA5] * size)
    b[0:4] = b'PRBE'
    struct.pack_into('<I', b, 0x04, version)
    # Deliberately NOT a valid Telink boot marker.
    struct.pack_into('<I', b, 0x08, 0x214F4E44)
    struct.pack_into('<H', b, 0x12, MFG)
    struct.pack_into('<H', b, 0x14, IMAGE)
    struct.pack_into('<I', b, 0x18, size)
    banner = b'NON-EXECUTABLE OFFLINE OTA ACCEPTANCE PROBE'
    b[0x20:0x20 + len(banner)] = banner
    crc = binascii.crc32(b[:-4]) & 0xFFFFFFFF
    bad = crc ^ 0x01010101
    struct.pack_into('<I', b, size - 4, bad)
    assert (binascii.crc32(b[:-4]) & 0xFFFFFFFF) != int.from_bytes(b[-4:], 'little')
    return bytes(b)


def build_ota(version: int, payload_size: int) -> bytes:
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
    ap.add_argument(
        '--unsafe-create-probe', action='store_true',
        help='required acknowledgment; this only creates a file and never serves it',
    )
    ns = ap.parse_args()
    if not ns.unsafe_create_probe:
        ap.error('refusing to create probe without --unsafe-create-probe acknowledgment')
    if ns.version <= BASE_VERSION:
        ap.error('probe version must be higher than stock 0x26013001')
    data = build_ota(ns.version, ns.payload_size)
    ns.out.write_bytes(data)
    meta = {
        'DO_NOT_SERVE_TO_DEVICE': True,
        'reason': 'offline acceptance-probe artifact; deliberately non-bootable and CRC-invalid',
        'path': str(ns.out),
        'size': len(data),
        'sha256': hashlib.sha256(data).hexdigest(),
        'manufacturer_code': f'0x{MFG:04X}',
        'image_type': f'0x{IMAGE:04X}',
        'file_version': f'0x{ns.version:08X}',
    }
    ns.out.with_suffix(ns.out.suffix + '.json').write_text(json.dumps(meta, indent=2) + '\n')
    print(json.dumps(meta, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
