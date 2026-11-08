#!/usr/bin/env python3
"""Stock SWire recovery tests; zero serial access, zero device mutations."""
from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import glsd301p_stock_swire_recovery as recovery
from telink_app_finalize import finalize_link_binary


def dummy_inner():
    raw = bytearray(b"\xff" * 0x2000)
    raw[6:8] = b"\0\0"
    raw[8:12] = b"KNLT"
    struct.pack_into("<I", raw, 2, recovery.EXPECTED_VENDOR_VERSION)
    struct.pack_into("<H", raw, 0x12, recovery.MANUFACTURER)
    struct.pack_into("<H", raw, 0x14, recovery.IMAGE_TYPE)
    struct.pack_into("<I", raw, 0x18, len(raw))
    return finalize_link_binary(
        bytes(raw),
        manufacturer_code=recovery.MANUFACTURER,
        image_type=recovery.IMAGE_TYPE,
        file_version=recovery.EXPECTED_VENDOR_VERSION,
    )


def dummy_ota(inner=None, field_control=0, tag=0, stack_version=2):
    app = dummy_inner() if inner is None else inner
    hw = struct.pack("<HH", 2, 2) if field_control == 4 else b""
    header_len = 56 + len(hw)
    head = struct.pack(
        "<IHHHHHIH32sI",
        recovery.OTA_MAGIC, recovery.OTA_HEADER_VERSION,
        header_len, field_control, recovery.MANUFACTURER,
        recovery.IMAGE_TYPE, recovery.EXPECTED_VENDOR_VERSION,
        stack_version, b"TEST STOCK IMAGE".ljust(32, b"\0"),
        header_len + 6 + len(app),
    )
    return head + hw + struct.pack("<HI", tag, len(app)) + app


class StockRecoveryTests(unittest.TestCase):
    def test_base_header_extracts_exact_binary_no_rewrite(self):
        inner = dummy_inner()
        self.assertEqual(recovery.extract_stock_app(dummy_ota(inner)), inner)
        self.assertEqual(inner[8:12], b"KNLT")

    def test_hw_version_header(self):
        self.assertEqual(
            recovery.extract_stock_app(dummy_ota(field_control=4)),
            dummy_inner(),
        )

    def test_reject_wrong_ota_identity(self):
        bad = bytearray(dummy_ota())
        bad[10] ^= 1
        with self.assertRaises(recovery.RecoveryError):
            recovery.extract_stock_app(bytes(bad))

    def test_reject_invalid_app_crc(self):
        bad = bytearray(dummy_ota())
        bad[-8] ^= 1
        with self.assertRaises(recovery.RecoveryError):
            recovery.extract_stock_app(bytes(bad))

    def test_reject_wrong_subelement(self):
        with self.assertRaises(recovery.RecoveryError):
            recovery.extract_stock_app(dummy_ota(tag=0xF000))

    def test_reject_multiple_or_trailing_subelements(self):
        with self.assertRaises(recovery.RecoveryError):
            recovery.extract_stock_app(dummy_ota() + b"\0" * 6)

    def test_reject_unknown_header_fields_and_stack(self):
        for file in (dummy_ota(field_control=8), dummy_ota(stack_version=0)):
            with self.subTest(case=file[8:12]), self.assertRaises(recovery.RecoveryError):
                recovery.extract_stock_app(file)

    def test_block_invalid_vendor_source(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / recovery.VENDOR_OTA.name
            path.write_bytes(b"malicious replacement")
            with self.assertRaises(recovery.RecoveryError):
                recovery.checked_vendor_source(path, recovery.VENDOR_MANIFEST)

    def test_no_firmware_writes_without_exact_confirmation(self):
        with patch.object(recovery, "prepare", return_value={"safe": True}), \
             patch.object(recovery.sys.stdin, "isatty", return_value=False), \
             patch.object(recovery.subprocess, "run") as process:
            with self.assertRaises(recovery.RecoveryError):
                recovery.flash(
                    port="COM3", output=Path("/tmp/unused.bin"),
                    programmer=None, activate_ms=0, confirmation=None
                )
            process.assert_not_called()

    def test_two_phase_activation_rejected_after_failed_stage(self):
        with patch.object(recovery, "prepare", return_value={
            "stagedOutput": "safe-stage.bin",
            "activationOutput": "marker.bin",
        }), patch.object(recovery, "get_programmer", return_value=Path("flasher.py")), \
             patch.dict(sys.modules, {"serial": object()}), \
             patch.object(recovery.subprocess, "run",
                          return_value=type("R", (), {"returncode": 1})()) as run:
            with self.assertRaises(recovery.RecoveryError):
                recovery.flash(port="COM3", output=Path("stock.bin"),
                               programmer=None, activate_ms=0,
                               confirmation="WRITE_STOCK_BANK_A")
            run.assert_called_once()
            self.assertEqual(run.call_args.args[0][-3:],
                             ["wf", "0x00000", "safe-stage.bin"])

    def test_two_phase_activation_uses_no_erase_for_marker(self):
        with patch.object(recovery, "prepare", return_value={
            "stagedOutput": "safe-stage.bin",
            "activationOutput": "marker.bin",
        }), patch.object(recovery, "get_programmer", return_value=Path("flasher.py")), \
             patch.dict(sys.modules, {"serial": object()}), \
             patch.object(recovery.subprocess, "run",
                          return_value=type("R", (), {"returncode": 0})()) as run:
            recovery.flash(port="COM3", output=Path("stock.bin"),
                           programmer=None, activate_ms=0,
                           confirmation="WRITE_STOCK_BANK_A")
            self.assertEqual(run.call_count, 2)
            self.assertEqual(run.call_args_list[0].args[0][-3:],
                             ["wf", "0x00000", "safe-stage.bin"])
            self.assertEqual(run.call_args_list[1].args[0][-3:],
                             ["we", "0x00008", "marker.bin"])

    def test_no_raw_ota_passed_to_programmer(self):
        self.assertEqual(recovery.BANK_A_START, 0)
        self.assertEqual(recovery.NV_A_START, recovery.BANK_A_END)
        self.assertEqual(recovery.BANK_B_START, 0x40000)
        self.assertEqual(recovery.MAC_START, 0x76000)
        self.assertNotEqual(recovery.VENDOR_OTA.suffix, ".bin")
        with self.assertRaises(recovery.RecoveryError):
            recovery.extract_stock_app(b"OTA?")

    def test_upstream_git_blob_integrity(self):
        source = b"test flasher"
        expected = hashlib.sha1(b"blob 12\0" + source).hexdigest()
        self.assertEqual(recovery.upstream_blob_sha1(source), expected)
        with tempfile.TemporaryDirectory() as td:
            fake = Path(td) / "TLSR825xComFlasher.py"
            fake.write_bytes(source)
            with self.assertRaises(recovery.RecoveryError):
                recovery.get_programmer(fake, Path(td))

    def test_real_vendor_artifact_is_preparable(self):
        """Run against checked-in ORIGINAL, not a mocked image; mandatory in CI."""
        self.assertTrue(recovery.VENDOR_OTA.is_file(), "vendor original missing")
        original_before = recovery.VENDOR_OTA.read_bytes()
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "stock-bank-a.bin"
            report = recovery.prepare(output=out)
            self.assertEqual(out.read_bytes(), recovery.extract_stock_app(original_before))
            self.assertEqual(report["applicationSha256"], recovery.sha256(out.read_bytes()))
            self.assertLessEqual(
                int(report["eraseEndExclusive"], 16), recovery.NV_A_START
            )
            self.assertEqual(out.read_bytes()[8:12], b"KNLT")
            self.assertTrue(out.with_suffix(".json").exists())
            stage = out.with_name(out.stem + "-stage.bin").read_bytes()
            marker = out.with_name(out.stem + "-activate.bin").read_bytes()
            self.assertEqual(stage[8], 0xff)
            self.assertEqual(stage[9:12], b"NLT")
            self.assertEqual(stage[:8] + stage[9:], out.read_bytes()[:8] + out.read_bytes()[9:])
            self.assertEqual(marker, b"K")
            self.assertEqual(report["activationAddress"], "0x00008")
            self.assertLessEqual(int(report["eraseEndExclusive"], 16), 0x34000)
        self.assertEqual(recovery.VENDOR_OTA.read_bytes(), original_before)


if __name__ == "__main__":
    unittest.main()
