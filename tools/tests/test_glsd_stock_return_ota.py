#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import pathlib
import struct
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from make_glsd_stock_return_ota import (  # noqa: E402
    RETURN_OUTER_FILE_VERSION,
    STOCK_FILE_VERSION,
    STAGER_FILE_VERSION,
    TARGET_IEEE,
    StockReturnError,
    build_stock_return_ota,
)
from telink_app_finalize import (  # noqa: E402
    DEFAULT_IMAGE_TYPE,
    DEFAULT_MANUFACTURER,
    TELINK_RAW_MAGIC,
    TELINK_STARTUP_FLAG_BYTES,
    finalize_link_binary,
)
import telink_ota_forensics as forensics  # noqa: E402


def make_stock_inner(size: int = 256) -> bytes:
    raw = bytearray([0xA5] * size)
    struct.pack_into("<I", raw, 0x02, STOCK_FILE_VERSION)
    raw[0x06:0x08] = TELINK_RAW_MAGIC
    raw[0x08:0x0C] = TELINK_STARTUP_FLAG_BYTES
    struct.pack_into("<H", raw, 0x12, DEFAULT_MANUFACTURER)
    struct.pack_into("<H", raw, 0x14, DEFAULT_IMAGE_TYPE)
    struct.pack_into("<I", raw, 0x18, size)
    return finalize_link_binary(bytes(raw), file_version=STOCK_FILE_VERSION)


def make_validation(inner: bytes) -> dict:
    return {
        "pass": True,
        "target_ieee": TARGET_IEEE,
        "total_len": len(inner),
        "reconstructed_sha256": hashlib.sha256(inner).hexdigest(),
        "reconstruction_diffs": [{"offset": 8, "before": 0, "after": 0x4B}],
        "telink_application": {"valid": True},
    }


class StockReturnOtaTests(unittest.TestCase):
    def test_wrapper_keeps_stock_payload_byte_exact(self) -> None:
        inner = make_stock_inner()
        ota, meta = build_stock_return_ota(inner, make_validation(inner))
        header = forensics.parse_ota_header(ota)
        subs = forensics.parse_subelements(ota, header.header_length)
        self.assertEqual(len(subs), 1)
        payload = ota[subs[0].data_offset:subs[0].data_end]
        self.assertEqual(payload, inner)
        self.assertTrue(forensics.validate_telink_application(payload)["valid"])
        self.assertEqual(header.file_version, RETURN_OUTER_FILE_VERSION)
        self.assertEqual(int.from_bytes(payload[0x02:0x06], "little"), STOCK_FILE_VERSION)
        self.assertTrue(meta["payloadUnmodified"])
        self.assertTrue(meta["outerInnerVersionMismatchIntentional"])
        self.assertFalse(meta["AUTHORIZATION_GRANTED"])

    def test_outer_transport_version_must_be_newer_than_stager(self) -> None:
        inner = make_stock_inner()
        with self.assertRaises(StockReturnError):
            build_stock_return_ota(inner, make_validation(inner), outer_file_version=STAGER_FILE_VERSION)

    def test_reject_wrong_target_ieee(self) -> None:
        inner = make_stock_inner()
        validation = make_validation(inner)
        validation["target_ieee"] = "0x0000000000000001"
        with self.assertRaises(StockReturnError):
            build_stock_return_ota(inner, validation)

    def test_reject_tampered_reconstructed_bytes(self) -> None:
        inner = make_stock_inner()
        validation = make_validation(inner)
        mutated = inner[:-5] + bytes([inner[-5] ^ 0x01]) + inner[-4:]
        with self.assertRaises(StockReturnError):
            build_stock_return_ota(mutated, validation)

    def test_reject_wrong_reconstruction_provenance(self) -> None:
        inner = make_stock_inner()
        validation = make_validation(inner)
        validation["reconstruction_diffs"] = []
        with self.assertRaises(StockReturnError):
            build_stock_return_ota(inner, validation)

    def test_reject_non_stock_inner_version(self) -> None:
        inner = bytearray(make_stock_inner())
        struct.pack_into("<I", inner, 0x02, STOCK_FILE_VERSION + 1)
        validation = make_validation(bytes(inner))
        with self.assertRaises(StockReturnError):
            build_stock_return_ota(bytes(inner), validation)


if __name__ == "__main__":
    unittest.main(verbosity=2)
