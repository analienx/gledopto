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
    BANK_A_BASE,
    BANK_A_SLOT_END,
    BANK_B_BASE,
    BANK_B_SLOT_END,
    DEFAULT_FILE_VERSION,
    DEFAULT_IMAGE_TYPE,
    DEFAULT_MANUFACTURER,
    OTA_FIELD_HW_RANGE,
    TARGET_HW_VERSION,
    StagerOtaError,
    build_stager_ota,
    validate_neutral_manifest,
    validate_stager_ota,
)
from make_ota_acceptance_probe import (  # noqa: E402
    BASE_VERSION as ACCEPTANCE_BASE_VERSION,
    build_ota as build_acceptance_probe_ota,
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


def make_neutral_manifest(path: pathlib.Path, inner_path: pathlib.Path) -> pathlib.Path:
    data = inner_path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    text = "\n".join(
        [
            "MECHANICS_ONLY=YES",
            "DEPLOYABLE=NO",
            "BANK_NEUTRAL=YES",
            "LOGICAL_LINK_BASE=0x00000",
            "RUNTIME_BOOT_BANK_DETECTION=mcuBootAddrGet",
            f"PHYSICAL_BOOT_TARGET_A=0x{BANK_A_BASE:05x}",
            f"PHYSICAL_BOOT_TARGET_B=0x{BANK_B_BASE:05x}",
            f"FINAL_INNER_BINARY_SIZE={len(data)}",
            f"PHYSICAL_A_END_EXCLUSIVE=0x{BANK_A_BASE + len(data):05x}",
            f"PHYSICAL_B_END_EXCLUSIVE=0x{BANK_B_BASE + len(data):05x}",
            f"BANK_A_SLOT_END=0x{BANK_A_SLOT_END:05x}",
            f"BANK_B_SLOT_END=0x{BANK_B_SLOT_END:05x}",
            "TELINK_MULTI_ADDRESS_MODEL=PASS",
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

    def test_acceptance_probe_reaches_only_exact_telink_crc_failure(self) -> None:
        ota, crc_meta = build_acceptance_probe_ota(ACCEPTANCE_BASE_VERSION + 1, 512)
        with tempfile.TemporaryDirectory() as td:
            path = pathlib.Path(td) / "acceptance-probe.ota"
            path.write_bytes(ota)
            report = forensics.analyze(path)

        app = report["upgrade_image"]
        validation = app["application_validation"]
        self.assertTrue(report["total_size_matches_header"])
        self.assertTrue(app["outer_identity_matches_inner"])
        self.assertTrue(validation["valid_pattern_5d02"])
        self.assertTrue(validation["marker_valid"])
        self.assertTrue(validation["size_valid"])
        self.assertFalse(validation["telink_crc_valid"])
        self.assertFalse(validation["valid"])
        self.assertEqual(validation["reason"], "telink_crc_mismatch")
        self.assertNotEqual(
            crc_meta["expected_telink_xcrc32"], crc_meta["stored_bad_xcrc32"]
        )

    def test_neutral_manifest_attests_exact_inner_and_both_physical_slots(self) -> None:
        inner = make_finalized_inner()
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            inner_path = root / "glsd-stager-neutral.final.bin"
            inner_path.write_bytes(inner)
            manifest = make_neutral_manifest(root / "manifest.txt", inner_path)
            attestation = validate_neutral_manifest(manifest, inner_path)
            self.assertTrue(attestation["bankNeutral"])
            self.assertEqual(attestation["logicalLinkBase"], 0)
            self.assertEqual(attestation["physicalBootTargets"], [0, 0x40000])
            self.assertEqual(attestation["runtimeBootBankDetection"], "mcuBootAddrGet")
            self.assertEqual(attestation["innerSha256"], hashlib.sha256(inner).hexdigest())

    def test_neutral_manifest_rejects_physical_relink(self) -> None:
        inner = make_finalized_inner()
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            inner_path = root / "glsd-stager-neutral.final.bin"
            inner_path.write_bytes(inner)
            manifest = make_neutral_manifest(root / "manifest.txt", inner_path)
            text = manifest.read_text().replace("LOGICAL_LINK_BASE=0x00000", "LOGICAL_LINK_BASE=0x40000")
            manifest.write_text(text)
            with self.assertRaises(StagerOtaError):
                validate_neutral_manifest(manifest, inner_path)

    def test_neutral_manifest_rejects_tampered_inner(self) -> None:
        inner = make_finalized_inner()
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            inner_path = root / "glsd-stager-neutral.final.bin"
            inner_path.write_bytes(inner)
            manifest = make_neutral_manifest(root / "manifest.txt", inner_path)
            inner_path.write_bytes(inner[:-1] + bytes([inner[-1] ^ 0x01]))
            with self.assertRaises(StagerOtaError):
                validate_neutral_manifest(manifest, inner_path)

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
