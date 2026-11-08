#!/usr/bin/env python3
"""Offline TX-only SWire protocol tests. No serial devices are opened."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import glsd301p_stock_txonly as tx
from glsd301p_stock_swire_recovery import RecoveryError


class FakeSerial:
    def __init__(self, short=False):
        self.writes = []
        self.flushes = 0
        self.short = short
    def write(self, value):
        self.writes.append(bytes(value))
        return len(value) - 1 if self.short else len(value)
    def flush(self):
        self.flushes += 1
    def __enter__(self):
        return self
    def __exit__(self, *_):
        return None
    def read(self, *_):
        raise AssertionError("TX-only sender must never read from MCU")


class FastClock:
    def __init__(self):
        self.t = 0.
    def __call__(self):
        self.t += 1.
        return self.t


def tiny_staged(length=550):
    image = bytearray([0xff]) * length
    image[8:12] = tx.STAGE_MARKER
    return bytes(image)


class TxOnlyProtocolTests(unittest.TestCase):
    def test_vectors_match_pvvx_v05_original_javascript(self):
        # Golden SHA-256 vectors computed independently with the exact
        # original upstream sws_wr_addr() JavaScript under Node.js.
        cases = [
            (0x00B3, b"\x80",
             "c9f62130d26b1e7d0bc21b1bcf93c62e12970706b2ff095e976bbebbbb1b8809"),
            (0x000C, b"\x06\x01",
             "8df7fb7003ed35312fac58b14b8c285897c91144d9085fdfcfab60da0afb313d"),
            (0x0008, b"\x4b",
             "d4cf4c583339908a0c0b6e864647c57b7078986a8b963342ea18a111267520a6"),
        ]
        for address, value, expected in cases:
            with self.subTest(address=hex(address)):
                data = tx.swire_write_frame(address, value)
                self.assertEqual(len(data), 10 * (len(value) + 6))
                self.assertEqual(hashlib.sha256(data).hexdigest(), expected)
                self.assertEqual(data[-10:], b"\x80"*9+b"\xfe")
                self.assertEqual(set(data), {0x80, 0xfe})

    def test_invalid_frame_rejected(self):
        for address, payload in ((-1, b"A"), (0x1000000, b"K"), (0, b"")):
            with self.subTest(address=address), self.assertRaises(RecoveryError):
                tx.swire_write_frame(address, payload)

    def test_short_transport_write_fails_closed(self):
        w = tx.TxOnlyWriter(FakeSerial(short=True))
        with self.assertRaisesRegex(RecoveryError, "short serial"):
            w.sws(0x0d, b"\x00")

    def test_no_chip_reads_and_inactive_stage(self):
        fake = FakeSerial()
        w = tx.TxOnlyWriter(fake, sleeper=lambda _: None, clock=FastClock())
        image = tiny_staged()
        w.stage(image)
        self.assertEqual(w.sectors_erased, [0])
        self.assertEqual(w.pages_programmed, [(0, 256), (256, 256), (512, 38)])
        self.assertEqual(w.activation_writes, [])
        self.assertGreater(w.bytes_sent, len(image))
        self.assertEqual(fake.flushes, len(fake.writes))
        # No flash read/erase all commands: the wire traffic only needs SWire
        # register writes. A "read" call on FakeSerial would fail immediately.

    def test_activation_is_only_one_byte_and_never_erases(self):
        fake = FakeSerial()
        w = tx.TxOnlyWriter(fake, sleeper=lambda _: None, clock=FastClock())
        w.activate_stock()
        self.assertEqual(w.sectors_erased, [])
        self.assertEqual(w.pages_programmed, [(8, 1)])
        self.assertEqual(w.activation_writes, [8])
        self.assertGreater(w.bytes_sent, 0)

    def test_out_of_bounds_and_wrong_marker_refused(self):
        fake = FakeSerial()
        w = tx.TxOnlyWriter(fake)
        for address in (-4096, 1, tx.BANK_A_END):
            with self.subTest(address=address), self.assertRaises(RecoveryError):
                w.erase_sector(address)
        with self.assertRaises(RecoveryError):
            w.page(8, b"X", activation=True)
        with self.assertRaises(RecoveryError):
            w.page(0x34000, b"X")
        with self.assertRaises(RecoveryError):
            w.page(255, b"\0\0")
        with self.assertRaises(RecoveryError):
            w.stage(b"\xff"*30)
        with self.assertRaises(RecoveryError):
            w.stage(tiny_staged(tx.BANK_A_END + 1))

    def test_activation_gate_requires_valid_stage_receipt_and_port(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td)/"stock.bin"
            report = {"device": "GL-SD-301P", "sourceSha256": "source"}
            staged = tiny_staged()
            activate = b"K"
            fake = FakeSerial()
            with patch.object(tx, "prepare_payload", return_value=(report, staged, activate)), \
                 patch.object(tx, "open_port", return_value=fake), \
                 patch.object(tx, "TxOnlyWriter") as writer:
                writer.return_value.bytes_sent = 1234
                writer.return_value.frame_count = 10
                writer.return_value.sectors_erased = [0]
                writer.return_value.pages_programmed = [(0, len(staged))]
                writer.return_value.activation_writes = []
                with self.assertRaisesRegex(RecoveryError, "no prior stage"):
                    tx.run_action("activate", "COM3", path, tx.ACTIVATE_CONFIRM)
                tx.run_action("stage", "COM3", path, tx.STAGE_CONFIRM)
                receipt = json.loads(tx.receipt_path(path).read_text())
                self.assertEqual(receipt["phase"], "STAGE_SENT_UNVERIFIED")
                self.assertFalse(receipt["readbackVerified"])
                with self.assertRaisesRegex(RecoveryError, "refusing automatic repeat"):
                    tx.run_action("stage", "COM3", path, tx.STAGE_CONFIRM)
                with self.assertRaisesRegex(RecoveryError, "receipt incompatible"):
                    tx.run_action("activate", "COM4", path, tx.ACTIVATE_CONFIRM)
                tx.run_action("activate", "COM3", path, tx.ACTIVATE_CONFIRM)
                receipt = json.loads(tx.receipt_path(path).read_text())
                self.assertEqual(receipt["phase"], "ACTIVATION_SENT_UNVERIFIED")
                with self.assertRaisesRegex(RecoveryError, "receipt incompatible"):
                    tx.run_action("activate", "COM3", path, tx.ACTIVATE_CONFIRM)
                self.assertEqual(writer.return_value.stage.call_count, 1)
                self.assertEqual(writer.return_value.activate_stock.call_count, 1)

    def test_offline_zip_bundle_without_checkout_and_corruption(self):
        with tempfile.TemporaryDirectory() as td:
            directory = Path(td)
            # These originate from the real pinned vendor original, and
            # have to match the hashes expected from the GH Actions ZIP.
            report, staged, activation = tx.prepare_payload(directory / tx.STOCK_FILE)
            (directory / tx.STAGE_FILE).write_bytes(staged)
            (directory / tx.ACTIVATION_FILE).write_bytes(activation)
            (directory / tx.REPORT_FILE).write_text(json.dumps(report))
            recovered, got_stage, got_activation = tx.prepare_artifact_bundle(directory)
            self.assertEqual(recovered["sourceSha256"], tx.ORIGINAL_SOURCE_SHA256)
            self.assertEqual(got_stage, staged)
            self.assertEqual(got_activation, b"K")
            broken = bytearray(staged)
            broken[4000] ^= 0x01
            (directory / tx.STAGE_FILE).write_bytes(broken)
            with self.assertRaisesRegex(RecoveryError, "fail pinned"):
                tx.prepare_artifact_bundle(directory)

    def test_offline_bundle_requires_manifest_and_valid_app_crc(self):
        with tempfile.TemporaryDirectory() as td:
            directory = Path(td)
            report, staged, activation = tx.prepare_payload(directory / tx.STOCK_FILE)
            (directory / tx.STAGE_FILE).write_bytes(staged)
            (directory / tx.ACTIVATION_FILE).write_bytes(activation)
            report["sourceSha256"] = "wrong"
            (directory / tx.REPORT_FILE).write_text(json.dumps(report))
            with self.assertRaisesRegex(RecoveryError, "manifest mismatches"):
                tx.prepare_artifact_bundle(directory)

    def test_prepare_actual_vendor_staged_image_on_host(self):
        with tempfile.TemporaryDirectory() as td:
            report, stage, act = tx.prepare_payload(Path(td)/"stock.bin")
            self.assertEqual(stage[8:12], tx.STAGE_MARKER)
            self.assertEqual(act, tx.ACTIVATE_BYTE)
            self.assertEqual(len(stage), report["applicationBytes"])
            self.assertLessEqual(int(report["eraseEndExclusive"], 16), tx.BANK_A_END)
            self.assertEqual(hashlib.sha256(stage).hexdigest(), report["stagedSha256"])
            self.assertEqual(report["sourceSha256"],
                             "16595a38ab9783d3afc4eb58ab4ec32625249bd569c1fa6dbaf468bddc76dd72")


if __name__ == "__main__":
    unittest.main()
