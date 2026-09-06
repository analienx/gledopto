#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import struct
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from telink_app_finalize import (  # noqa: E402
    DEFAULT_FILE_VERSION,
    DEFAULT_IMAGE_TYPE,
    DEFAULT_MANUFACTURER,
    TELINK_8258_MAGIC,
    TELINK_STARTUP_FLAG_BYTES,
    TelinkImageError,
    finalize_link_binary,
    is_valid_finalized_image,
    parse_preamble,
    telink_xcrc32,
    validate_finalized_image,
    validate_link_binary,
)


def make_raw(size: int = 64) -> bytes:
    if size < 0x1C:
        raise ValueError
    raw = bytearray([0xA5] * size)
    struct.pack_into("<I", raw, 0x02, DEFAULT_FILE_VERSION)
    raw[0x06:0x08] = TELINK_8258_MAGIC
    raw[0x08:0x0C] = TELINK_STARTUP_FLAG_BYTES
    struct.pack_into("<H", raw, 0x12, DEFAULT_MANUFACTURER)
    struct.pack_into("<H", raw, 0x14, DEFAULT_IMAGE_TYPE)
    struct.pack_into("<I", raw, 0x18, size)
    return bytes(raw)


class TelinkFinalizeTests(unittest.TestCase):
    def test_finalize_aligned_link_binary(self) -> None:
        raw = make_raw(64)
        validate_link_binary(raw)
        final = finalize_link_binary(raw)
        self.assertEqual(len(final), 68)
        preamble = validate_finalized_image(final)
        self.assertEqual(preamble.declared_size, len(final))
        self.assertEqual(struct.unpack_from("<I", final, len(final) - 4)[0], telink_xcrc32(final[:-4]))
        self.assertTrue(is_valid_finalized_image(final))

    def test_finalize_pads_body_before_crc(self) -> None:
        raw = make_raw(65)
        final = finalize_link_binary(raw)
        self.assertEqual(len(final), 84)  # 65 -> 80-byte body + 4-byte CRC
        self.assertEqual(final[65:80], b"\xff" * 15)
        validate_finalized_image(final)

    def test_reject_wrong_marker(self) -> None:
        raw = bytearray(make_raw())
        raw[8] ^= 0x01
        with self.assertRaises(TelinkImageError):
            validate_link_binary(bytes(raw))

    def test_reject_wrong_identity(self) -> None:
        raw = bytearray(make_raw())
        struct.pack_into("<H", raw, 0x14, 0x9999)
        with self.assertRaises(TelinkImageError):
            validate_link_binary(bytes(raw))

    def test_reject_linker_size_mismatch(self) -> None:
        raw = bytearray(make_raw())
        struct.pack_into("<I", raw, 0x18, len(raw) + 1)
        with self.assertRaises(TelinkImageError):
            validate_link_binary(bytes(raw))

    def test_reject_corrupted_final_crc(self) -> None:
        final = bytearray(finalize_link_binary(make_raw()))
        final[32] ^= 0x80
        with self.assertRaises(TelinkImageError):
            validate_finalized_image(bytes(final))

    def test_refuse_double_finalize(self) -> None:
        final = finalize_link_binary(make_raw())
        with self.assertRaises(TelinkImageError):
            finalize_link_binary(final)

    def test_fail_before_app_limit(self) -> None:
        raw = make_raw(64)
        with self.assertRaises(TelinkImageError):
            finalize_link_binary(raw, max_final_size=68)

    def test_parse_preamble(self) -> None:
        p = parse_preamble(make_raw())
        self.assertEqual(p.file_version, DEFAULT_FILE_VERSION)
        self.assertEqual(p.manufacturer_code, DEFAULT_MANUFACTURER)
        self.assertEqual(p.image_type, DEFAULT_IMAGE_TYPE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
