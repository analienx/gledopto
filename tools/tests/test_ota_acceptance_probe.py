#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GEN = ROOT / 'tools' / 'make_ota_acceptance_probe.py'
FORENSICS = ROOT / 'tools' / 'telink_ota_forensics.py'


class AcceptanceProbeTests(unittest.TestCase):
    def test_probe_is_structurally_valid_but_telink_crc_invalid(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            ota = Path(td) / 'acceptance-probe.ota'
            subprocess.run(
                [
                    sys.executable,
                    str(GEN),
                    '--out',
                    str(ota),
                    '--version',
                    '0x26013002',
                    '--unsafe-create-probe',
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run(
                [sys.executable, str(FORENSICS), str(ota), '--json'],
                check=True,
                capture_output=True,
                text=True,
            )
            analysis = json.loads(result.stdout)
            app = analysis['upgrade_image']
            validation = app['application_validation']

            self.assertTrue(analysis['total_size_matches_header'])
            self.assertTrue(app['outer_identity_matches_inner'])
            self.assertTrue(validation['valid_pattern_5d02'])
            self.assertTrue(validation['marker_valid'])
            self.assertTrue(validation['size_valid'])
            self.assertFalse(validation['telink_crc_valid'])
            self.assertFalse(validation['valid'])
            self.assertEqual(validation['reason'], 'telink_crc_mismatch')

            meta = json.loads(ota.with_suffix('.ota.json').read_text())
            self.assertTrue(meta['INTENTIONALLY_NON_BOOTABLE'])
            self.assertEqual(meta['failure_mode'], 'telink_xcrc32_mismatch')
            self.assertTrue(meta['startup_marker_valid'])
            self.assertTrue(meta['preamble_5d02_valid'])
            self.assertEqual(meta['version'], 0x26013002)
            self.assertEqual(meta['size'], ota.stat().st_size)
            self.assertEqual(len(meta['sha256']), 64)
            self.assertEqual(len(meta['sha512']), 128)
            self.assertNotEqual(
                meta['expected_telink_xcrc32'], meta['stored_bad_xcrc32']
            )


if __name__ == '__main__':
    unittest.main()
