#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from glsd_release_plan import TARGET_IEEE, ReleasePlanError, build_plan  # noqa: E402


def candidate(payload: bytes, name: str = "glsd-stager.ota") -> dict:
    size = 156740
    return {
        "DEPLOYABLE": False,
        "DO_NOT_SERVE_TO_PRODUCTION": True,
        "manufacturerCode": 0x124F,
        "imageType": 0x1416,
        "fileVersion": 0x7F010001,
        "hardwareVersionMin": 2,
        "hardwareVersionMax": 2,
        "innerValid": True,
        "bankNeutral": True,
        "logicalLinkBase": 0,
        "runtimeBootBankDetection": "mcuBootAddrGet",
        "physicalBootTargets": [0, 0x40000],
        "physicalAEndExclusive": size,
        "physicalBEndExclusive": 0x40000 + size,
        "bankASlotEnd": 0x34000,
        "bankBSlotEnd": 0x74000,
        "file": name,
        "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "sha512": hashlib.sha512(payload).hexdigest(),
        "innerSha256": "33" * 32,
        "buildManifestSha256": "44" * 32,
    }


class ReleasePlanTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.payload = b"quarantined exact OTA bytes\x00\x01\x02"
        self.path = pathlib.Path(self.tmp.name) / "glsd-stager.ota"
        self.path.write_bytes(self.payload)
        self.meta = candidate(self.payload, self.path.name)

    def test_current_blockers_omit_mutating_request(self) -> None:
        plan = build_plan(
            self.meta,
            candidate_path=self.path,
            url="https://example.invalid/glsd-stager.ota",
            production_flash_size=None,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="unknown",
            production_revision_proven=False,
            return_to_stock_spare_passed=False,
        )
        self.assertFalse(plan["preflight"]["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertIsNone(plan["updateRequest"])
        self.assertFalse(plan["authorizationGranted"])
        self.assertFalse(plan["usesGlobalOverrideIndex"])
        self.assertEqual(plan["targetIeee"], TARGET_IEEE)
        self.assertEqual(plan["checkRequest"]["payload"]["id"], TARGET_IEEE)
        self.assertTrue(plan["candidateByteAttestation"]["matchesQuarantineSidecar"])
        self.assertEqual(plan["schemaVersion"], 2)

    def test_full_evidence_emits_exact_ieee_request_but_not_authorization(self) -> None:
        url = "https://fw.example.invalid/one-use/glsd-stager.ota"
        plan = build_plan(
            self.meta,
            candidate_path=self.path,
            url=url,
            production_flash_size=0x80000,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="TLSR8258",
            production_revision_proven=True,
            return_to_stock_spare_passed=True,
        )
        self.assertTrue(plan["preflight"]["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertFalse(plan["authorizationGranted"])
        self.assertEqual(
            plan["updateRequest"]["payload"],
            {"id": TARGET_IEEE, "url": url},
        )
        self.assertTrue(plan["updateRequest"]["mutatesFirmware"])
        self.assertEqual(plan["candidateByteAttestation"]["sha256"], self.meta["sha256"])
        self.assertEqual(plan["candidateByteAttestation"]["sha512"], self.meta["sha512"])

    def test_rejects_local_or_credential_url(self) -> None:
        for url in (
            "file:///tmp/stager.ota",
            "/config/stager.ota",
            "https://user:pass@example.invalid/stager.ota",
        ):
            with self.subTest(url=url):
                with self.assertRaises(ReleasePlanError):
                    build_plan(
                        self.meta,
                        candidate_path=self.path,
                        url=url,
                        production_flash_size=None,
                        production_hw_version=2,
                        current_file_version=0x26013001,
                        production_mcu="unknown",
                        production_revision_proven=False,
                        return_to_stock_spare_passed=False,
                    )

    def test_rejects_any_candidate_byte_mismatch(self) -> None:
        cases = {
            "content": lambda meta, path: path.write_bytes(self.payload + b"tamper"),
            "sha256": lambda meta, path: meta.__setitem__("sha256", "00" * 32),
            "sha512": lambda meta, path: meta.__setitem__("sha512", "00" * 64),
            "size": lambda meta, path: meta.__setitem__("bytes", len(self.payload) + 1),
            "filename": lambda meta, path: meta.__setitem__("file", "other.ota"),
        }
        for name, mutate in cases.items():
            with self.subTest(name=name):
                path = pathlib.Path(self.tmp.name) / f"case-{name}"
                path.mkdir()
                ota = path / "glsd-stager.ota"
                ota.write_bytes(self.payload)
                meta = candidate(self.payload, ota.name)
                mutate(meta, ota)
                with self.assertRaises(ReleasePlanError):
                    build_plan(
                        meta,
                        candidate_path=ota,
                        url="https://example.invalid/glsd-stager.ota",
                        production_flash_size=0x80000,
                        production_hw_version=2,
                        current_file_version=0x26013001,
                        production_mcu="TLSR8258",
                        production_revision_proven=True,
                        return_to_stock_spare_passed=True,
                    )


if __name__ == "__main__":
    unittest.main(verbosity=2)
