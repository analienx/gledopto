#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import glsd301p_health_decode as decoder  # noqa: E402

FIXTURES = ROOT / "tools" / "tests" / "fixtures" / "glsd301p_health"


def load_fixture(name: str) -> dict:
    return json.loads((FIXTURES / name).read_text(encoding="utf-8"))


class HealthDecodeTests(unittest.TestCase):
    def test_fixtures_present_and_documented(self) -> None:
        names = sorted(p.name for p in FIXTURES.glob("*.json"))
        self.assertEqual(
            names,
            [
                "bad_length_long.json",
                "bad_length_short.json",
                "bad_version.json",
                "boot_defaults.json",
                "joined_active.json",
                "joining_retry.json",
                "saturated.json",
            ],
        )
        self.assertTrue((FIXTURES / "README.md").exists())

    def test_valid_fixtures_decode_exactly(self) -> None:
        for name in (
            "boot_defaults.json",
            "joined_active.json",
            "joining_retry.json",
            "saturated.json",
        ):
            with self.subTest(fixture=name):
                fixture = load_fixture(name)
                raw = bytes.fromhex(fixture["hex"])
                self.assertEqual(len(raw), decoder.HEALTH_SIZE)
                decoded = decoder.decode_health_snapshot(raw)
                self.assertEqual(decoded, fixture["expected"])

    def test_invalid_fixtures_rejected(self) -> None:
        for name in (
            "bad_version.json",
            "bad_length_short.json",
            "bad_length_long.json",
        ):
            with self.subTest(fixture=name):
                fixture = load_fixture(name)
                raw = bytes.fromhex(fixture["hex"])
                with self.assertRaises(decoder.HealthDecodeError) as ctx:
                    decoder.decode_health_snapshot(raw)
                self.assertIn(fixture["expect_error"], str(ctx.exception))

    def test_unknown_network_state_rejected(self) -> None:
        raw = bytearray(bytes.fromhex(load_fixture("boot_defaults.json")["hex"]))
        raw[1] = 0x03
        with self.assertRaises(decoder.HealthDecodeError) as ctx:
            decoder.decode_health_snapshot(bytes(raw))
        self.assertIn("unknown network state 3", str(ctx.exception))

    def test_layout_spot_checks(self) -> None:
        decoded = decoder.decode_health_snapshot(
            bytes.fromhex(load_fixture("joined_active.json")["hex"])
        )
        self.assertEqual(decoded["version"], 2)
        self.assertEqual(decoded["uptime_ms"], 0x12345678)
        self.assertEqual(decoded["uart_deadline_faults"], 0xDEADBEEF)
        self.assertEqual(decoded["exc_line"], 0xBEEF)
        self.assertTrue(decoded["flags"]["boot_off_complete"])
        self.assertFalse(decoded["flags"]["uart_busy"])

    def test_cli_round_trip(self) -> None:
        fixture = load_fixture("joining_retry.json")
        proc = subprocess.run(
            [sys.executable, str(TOOLS / "glsd301p_health_decode.py"),
             fixture["hex"]],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        self.assertEqual(proc.returncode, 0, proc.stderr.decode())
        self.assertEqual(json.loads(proc.stdout.decode()), fixture["expected"])

    def test_cli_rejects_bad_version(self) -> None:
        fixture = load_fixture("bad_version.json")
        proc = subprocess.run(
            [sys.executable, str(TOOLS / "glsd301p_health_decode.py"),
             fixture["hex"]],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
        )
        self.assertEqual(proc.returncode, 2)
        self.assertIn("unsupported health version", proc.stderr.decode())


if __name__ == "__main__":
    unittest.main()
