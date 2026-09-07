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

import telink_ota_forensics as forensics  # noqa: E402
from make_glsd_stager_ota import (  # noqa: E402
    DEFAULT_FILE_VERSION,
    DEFAULT_IMAGE_TYPE,
    DEFAULT_MANUFACTURER,
    TARGET_HW_VERSION,
    build_stager_ota,
)
from make_glsd_stager_shadow_probe import (  # noqa: E402
    SHADOW_FILE_VERSION,
    STOCK_FILE_VERSION,
    ShadowProbeError,
    build_shadow_probe,
)
from telink_app_finalize import (  # noqa: E402
    TELINK_RAW_MAGIC,
    TELINK_STARTUP_FLAG_BYTES,
    finalize_link_binary,
)


def make_finalized_inner(size: int = 512) -> bytes:
    raw = bytearray([0xA5] * size)
    struct.pack_into("<I", raw, 0x02, DEFAULT_FILE_VERSION)
    raw[0x06:0x08] = TELINK_RAW_MAGIC
    raw[0x08:0x0C] = TELINK_STARTUP_FLAG_BYTES
    struct.pack_into("<H", raw, 0x12, DEFAULT_MANUFACTURER)
    struct.pack_into("<H", raw, 0x14, DEFAULT_IMAGE_TYPE)
    struct.pack_into("<I", raw, 0x18, size)
    return finalize_link_binary(bytes(raw))


class ShadowProbeTests(unittest.TestCase):
    def test_shadow_is_same_size_and_fails_only_telink_crc(self) -> None:
        candidate = build_stager_ota(make_finalized_inner())
        shadow, meta = build_shadow_probe(candidate)

        self.assertEqual(len(shadow), len(candidate))
        self.assertGreater(meta["byte_diff_count"], 0)
        self.assertEqual(meta["forensics_reason"], "telink_crc_mismatch")
        self.assertEqual(meta["fileVersion"], SHADOW_FILE_VERSION)
        self.assertEqual(meta["hardwareVersionMin"], TARGET_HW_VERSION)
        self.assertEqual(meta["hardwareVersionMax"], TARGET_HW_VERSION)
        self.assertEqual(meta["innerBytes"], len(make_finalized_inner()))

        header = forensics.parse_ota_header(shadow)
        subs = forensics.parse_subelements(shadow, header.header_length)
        payload = shadow[subs[0].data_offset:subs[0].data_end]
        app = forensics.validate_telink_application(payload)
        self.assertFalse(app["valid"])
        self.assertEqual(app["reason"], "telink_crc_mismatch")
        self.assertTrue(app["valid_pattern_5d02"])
        self.assertTrue(app["marker_valid"])
        self.assertEqual(header.file_version, SHADOW_FILE_VERSION)
        self.assertEqual(int.from_bytes(payload[0x02:0x06], "little"), SHADOW_FILE_VERSION)

    def test_candidate_remains_valid_and_untouched(self) -> None:
        candidate = build_stager_ota(make_finalized_inner())
        before = bytes(candidate)
        shadow, _ = build_shadow_probe(candidate)
        self.assertEqual(candidate, before)
        self.assertNotEqual(shadow, candidate)

        header = forensics.parse_ota_header(candidate)
        sub = forensics.parse_subelements(candidate, header.header_length)[0]
        app = forensics.validate_telink_application(candidate[sub.data_offset:sub.data_end])
        self.assertTrue(app["valid"])
        self.assertEqual(header.file_version, DEFAULT_FILE_VERSION)

    def test_shadow_version_is_between_stock_and_real_stager(self) -> None:
        self.assertLess(STOCK_FILE_VERSION, SHADOW_FILE_VERSION)
        self.assertLess(SHADOW_FILE_VERSION, DEFAULT_FILE_VERSION)
        candidate = build_stager_ota(make_finalized_inner())
        with self.assertRaises(ShadowProbeError):
            build_shadow_probe(candidate, shadow_version=STOCK_FILE_VERSION)
        with self.assertRaises(ShadowProbeError):
            build_shadow_probe(candidate, shadow_version=DEFAULT_FILE_VERSION)

    def test_rejects_tampered_candidate(self) -> None:
        candidate = bytearray(build_stager_ota(make_finalized_inner()))
        header = forensics.parse_ota_header(candidate)
        sub = forensics.parse_subelements(candidate, header.header_length)[0]
        candidate[sub.data_offset + 0x40] ^= 0x01
        with self.assertRaises(ShadowProbeError):
            build_shadow_probe(bytes(candidate))


if __name__ == "__main__":
    unittest.main(verbosity=2)
