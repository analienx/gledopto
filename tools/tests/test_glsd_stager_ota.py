#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from make_glsd_stager_ota import (  # noqa: E402
    DEFAULT_FILE_VERSION,
    DEFAULT_IMAGE_TYPE,
    DEFAULT_MANUFACTURER,
    OTA_FIELD_HW_RANGE,
    TARGET_HW_VERSION,
    StagerOtaError,
    build_stager_ota,
    validate_bank_manifest,
    validate_stager_ota,
)
from telink_app_finalize import (  # noqa: E402
    TELINK_RAW_MAGIC,
    TELINK_STARTUP_FLAG_BYTES,
    finalize_link_binary,
)
import telink_ota_forensics as forensics  # noqa: E402


def make_finalized_inner(size: int = 256) -> bytes:
    raw = bytearray([0xA5] * size)
    struct.pack_into("<I", raw, 0x02, DEFAULT_FILE_VERSION)
    raw[0x06:0x08] = TELINK_RAW_MAGIC
    raw[0x08:0x0C] = TELINK_STARTUP_FLAG_BYTES
    struct.pack_into("<H", raw, 0x12, DEFAULT_MANUFACTURER)
    struct.pack_into("<H", raw, 0x14, DEFAULT_IMAGE_TYPE)
    struct.pack_into("<I", raw, 0x18, size)
    return finalize_link_binary(bytes(raw))


def make_bank_manifest(path: pathlib.Path, inner_path: pathlib.Path, bank: str) -> pathlib.Path:
    base = 0 if bank == "bank_a" else 0x40000
    slot_end = base + 0x34000
    data = inner_path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    text = "\n".join(
        [
            "MECHANICS_ONLY=YES",
            "DEPLOYABLE=NO",
            f"BANK={bank}",
            f"GLSD_STAGER_LINK_BASE=0x{base:05x}",
            f"FINAL_INNER_BINARY_SIZE={len(data)}",
            f"PHYSICAL_FLASH_START=0x{base:05x}",
            f"PHYSICAL_FLASH_END_EXCLUSIVE=0x{base + len(data):05x}",
            f"APP_SLOT_END=0x{slot_end:05x}",
            f"{digest}  {inner_path}",
            "",
        ]
    )
    path.write_text(text, encoding="utf-8")
    return path


class StagerOtaTests(unittest.TestCase):
    def test_wrapper_is_exactly_target_locked(self) -> None:
        ota = build_stager_ota(make_finalized_inner())
        with tempfile.TemporaryDirectory() as td:
            path = pathlib.Path(td) / "stager.ota"
            path.write_bytes(ota)
            report = validate_stager_ota(path)
        h = report["header"]
        self.assertEqual(h["manufacturer_code"], DEFAULT_MANUFACTURER)
        self.assertEqual(h["image_type"], DEFAULT_IMAGE_TYPE)
        self.assertEqual(h["file_version"], DEFAULT_FILE_VERSION)
        self.assertEqual(h["field_control"], OTA_FIELD_HW_RANGE)
        self.assertEqual(h["minimum_hardware_version"], TARGET_HW_VERSION)
        self.assertEqual(h["maximum_hardware_version"], TARGET_HW_VERSION)
        self.assertTrue(report["upgrade_image"]["application_validation"]["valid"])
        self.assertTrue(report["upgrade_image"]["outer_identity_matches_inner"])
        self.assertEqual(len(report["subelements"]), 1)
        self.assertEqual(report["subelements"][0]["tag_id"], 0)
        self.assertEqual(report["subelements"][0]["data_end"], len(ota))

    def test_bank_manifest_attests_exact_inner_and_bank(self) -> None:
        inner = make_finalized_inner()
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            inner_path = root / "glsd-stager-bank_b.final.bin"
            inner_path.write_bytes(inner)
            manifest = make_bank_manifest(root / "manifest.txt", inner_path, "bank_b")
            attestation = validate_bank_manifest(manifest, inner_path, "bank_b")
            self.assertEqual(attestation["targetBank"], "bank_b")
            self.assertEqual(attestation["targetLinkBase"], 0x40000)
            self.assertEqual(attestation["innerSha256"], hashlib.sha256(inner).hexdigest())

    def test_bank_manifest_rejects_cross_bank_label(self) -> None:
        inner = make_finalized_inner()
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            inner_path = root / "glsd-stager-bank_a.final.bin"
            inner_path.write_bytes(inner)
            manifest = make_bank_manifest(root / "manifest.txt", inner_path, "bank_a")
            with self.assertRaises(StagerOtaError):
                validate_bank_manifest(manifest, inner_path, "bank_b")

    def test_bank_manifest_rejects_tampered_inner(self) -> None:
        inner = make_finalized_inner()
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            inner_path = root / "glsd-stager-bank_a.final.bin"
            inner_path.write_bytes(inner)
            manifest = make_bank_manifest(root / "manifest.txt", inner_path, "bank_a")
            inner_path.write_bytes(inner[:-1] + bytes([inner[-1] ^ 0x01]))
            with self.assertRaises(StagerOtaError):
                validate_bank_manifest(manifest, inner_path, "bank_a")

    def test_reject_non_hw2_wrapper(self) -> None:
        with self.assertRaises(StagerOtaError):
            build_stager_ota(make_finalized_inner(), hardware_version=3)

    def test_reject_wrong_inner_identity(self) -> None:
        inner = bytearray(make_finalized_inner())
        struct.pack_into("<H", inner, 0x14, 0x9999)
        with self.assertRaises(ValueError):
            build_stager_ota(bytes(inner))

    def test_parser_exposes_hw_range(self) -> None:
        ota = build_stager_ota(make_finalized_inner())
        header = forensics.parse_ota_header(ota)
        self.assertEqual(header.header_length, 60)
        self.assertEqual(header.minimum_hardware_version, 2)
        self.assertEqual(header.maximum_hardware_version, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
