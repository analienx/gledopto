#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from glsd_release_plan import TARGET_IEEE, ReleasePlanError, build_plan  # noqa: E402


def candidate() -> dict:
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
        "sha256": "11" * 32,
        "sha512": "22" * 64,
        "innerSha256": "33" * 32,
    }


class ReleasePlanTests(unittest.TestCase):
    def test_current_blockers_omit_mutating_request(self) -> None:
        plan = build_plan(
            candidate(),
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

    def test_full_evidence_emits_exact_ieee_request_but_not_authorization(self) -> None:
        url = "https://fw.example.invalid/one-use/glsd-stager.ota"
        plan = build_plan(
            candidate(),
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

    def test_rejects_local_or_credential_url(self) -> None:
        for url in (
            "file:///tmp/stager.ota",
            "/config/stager.ota",
            "https://user:pass@example.invalid/stager.ota",
        ):
            with self.subTest(url=url):
                with self.assertRaises(ReleasePlanError):
                    build_plan(
                        candidate(),
                        url=url,
                        production_flash_size=None,
                        production_hw_version=2,
                        current_file_version=0x26013001,
                        production_mcu="unknown",
                        production_revision_proven=False,
                        return_to_stock_spare_passed=False,
                    )


if __name__ == "__main__":
    unittest.main(verbosity=2)
