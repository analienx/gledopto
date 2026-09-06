#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from glsd_flash_preflight import evaluate_preconditions  # noqa: E402


def candidate(bank: str = "bank_b") -> dict:
    base = 0 if bank == "bank_a" else 0x40000
    return {
        "DEPLOYABLE": False,
        "DO_NOT_SERVE_TO_PRODUCTION": True,
        "manufacturerCode": 0x124F,
        "imageType": 0x1416,
        "fileVersion": 0x7F010001,
        "hardwareVersionMin": 2,
        "hardwareVersionMax": 2,
        "innerValid": True,
        "targetBank": bank,
        "targetLinkBase": base,
        "physicalFlashStart": base,
        "physicalFlashEndExclusive": base + 156708,
        "appSlotEnd": base + 0x34000,
    }


class FlashPreflightTests(unittest.TestCase):
    def test_current_unknowns_fail_closed(self) -> None:
        result = evaluate_preconditions(
            candidate(),
            active_bank="unknown",
            production_flash_size=None,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="unknown",
            production_revision_proven=False,
            return_to_stock_spare_passed=False,
        )
        self.assertFalse(result["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertFalse(result["AUTHORIZATION_GRANTED"])
        self.assertIn("ACTIVE_BANK_UNKNOWN", result["blockers"])
        self.assertIn("PRODUCTION_MCU_UNPROVEN", result["blockers"])
        self.assertIn("PRODUCTION_FLASH_GEOMETRY_UNPROVEN", result["blockers"])
        self.assertIn("PRODUCTION_REVISION_UNPROVEN", result["blockers"])
        self.assertIn("RETURN_TO_STOCK_SPARE_NOT_PASSED", result["blockers"])

    def test_complete_external_evidence_allows_precondition_pass_only(self) -> None:
        result = evaluate_preconditions(
            candidate("bank_b"),
            active_bank="bank_a",
            production_flash_size=0x80000,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="TLSR8258",
            production_revision_proven=True,
            return_to_stock_spare_passed=True,
        )
        self.assertTrue(result["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertFalse(result["AUTHORIZATION_GRANTED"])
        self.assertEqual(result["blockers"], [])
        self.assertEqual(result["expectedInactiveBank"], "bank_b")

    def test_wrong_bank_is_rejected_even_with_all_other_proofs(self) -> None:
        result = evaluate_preconditions(
            candidate("bank_a"),
            active_bank="bank_a",
            production_flash_size=0x80000,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="TLSR8258",
            production_revision_proven=True,
            return_to_stock_spare_passed=True,
        )
        self.assertFalse(result["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertIn("TARGET_BANK_IS_NOT_INACTIVE_BANK", result["blockers"])

    def test_candidate_geometry_must_match_attested_bank(self) -> None:
        meta = candidate("bank_b")
        meta["targetLinkBase"] = 0
        result = evaluate_preconditions(
            meta,
            active_bank="bank_a",
            production_flash_size=0x80000,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="TLSR8258",
            production_revision_proven=True,
            return_to_stock_spare_passed=True,
        )
        self.assertFalse(result["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertIn("TARGET_LINK_BASE_MISMATCH", result["blockers"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
