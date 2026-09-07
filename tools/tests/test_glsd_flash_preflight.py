#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from glsd_flash_preflight import (  # noqa: E402
    HARDWARE_EVIDENCE_EXACT_SPARE,
    HARDWARE_EVIDENCE_INSTALLED_DIRECT,
    evaluate_preconditions,
)


def candidate() -> dict:
    size = 156708
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
    }


class FlashPreflightTests(unittest.TestCase):
    def test_current_unknowns_fail_closed_without_active_bank_blocker(self) -> None:
        result = evaluate_preconditions(
            candidate(),
            production_flash_size=None,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="unknown",
            production_revision_proven=False,
            return_to_stock_spare_passed=False,
        )
        self.assertFalse(result["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertFalse(result["AUTHORIZATION_GRANTED"])
        self.assertTrue(result["bankNeutral"])
        self.assertFalse(result["activeBankRequiredForFirstOta"])
        self.assertNotIn("ACTIVE_BANK_UNKNOWN", result["blockers"])
        self.assertIn("PRODUCTION_MCU_UNPROVEN", result["blockers"])
        self.assertIn("PRODUCTION_FLASH_GEOMETRY_UNPROVEN", result["blockers"])
        self.assertIn("PRODUCTION_REVISION_UNPROVEN", result["blockers"])
        self.assertIn("RETURN_TO_STOCK_SPARE_NOT_PASSED", result["blockers"])

    def test_direct_installed_evidence_allows_precondition_pass_only(self) -> None:
        result = evaluate_preconditions(
            candidate(),
            production_flash_size=0x80000,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="TLSR8258",
            production_revision_proven=True,
            return_to_stock_spare_passed=True,
            hardware_evidence_source=HARDWARE_EVIDENCE_INSTALLED_DIRECT,
        )
        self.assertTrue(result["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertFalse(result["AUTHORIZATION_GRANTED"])
        self.assertEqual(result["blockers"], [])
        self.assertEqual(result["hardwareEvidenceSource"], HARDWARE_EVIDENCE_INSTALLED_DIRECT)
        self.assertTrue(result["directProductionGeometryProven"])
        self.assertFalse(result["productionGeometryInferredFromExactRevisionSpare"])
        self.assertFalse(result["activeBankRequiredForFirstOta"])

    def test_exact_spare_inference_requires_explicit_risk_acceptance(self) -> None:
        result = evaluate_preconditions(
            candidate(),
            production_flash_size=0x80000,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="TLSR8258",
            production_revision_proven=False,
            return_to_stock_spare_passed=True,
            hardware_evidence_source=HARDWARE_EVIDENCE_EXACT_SPARE,
            exact_revision_spare_match_passed=True,
            accept_spare_inference_for_production=False,
        )
        self.assertFalse(result["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertIn("SPARE_GEOMETRY_INFERENCE_NOT_ACCEPTED", result["blockers"])
        self.assertFalse(result["productionGeometryInferredFromExactRevisionSpare"])
        self.assertFalse(result["AUTHORIZATION_GRANTED"])

    def test_exact_spare_inference_can_close_hardware_gate_without_direct_claim(self) -> None:
        result = evaluate_preconditions(
            candidate(),
            production_flash_size=0x80000,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="TLSR8258",
            production_revision_proven=False,
            return_to_stock_spare_passed=True,
            hardware_evidence_source=HARDWARE_EVIDENCE_EXACT_SPARE,
            exact_revision_spare_match_passed=True,
            accept_spare_inference_for_production=True,
        )
        self.assertTrue(result["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertEqual(result["blockers"], [])
        self.assertEqual(result["hardwareEvidenceSource"], HARDWARE_EVIDENCE_EXACT_SPARE)
        self.assertFalse(result["directProductionGeometryProven"])
        self.assertTrue(result["productionGeometryInferredFromExactRevisionSpare"])
        self.assertTrue(result["spareInferenceAccepted"])
        self.assertFalse(result["AUTHORIZATION_GRANTED"])

    def test_exact_spare_mode_requires_revision_match(self) -> None:
        result = evaluate_preconditions(
            candidate(),
            production_flash_size=0x80000,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="TLSR8258",
            production_revision_proven=False,
            return_to_stock_spare_passed=True,
            hardware_evidence_source=HARDWARE_EVIDENCE_EXACT_SPARE,
            exact_revision_spare_match_passed=False,
            accept_spare_inference_for_production=True,
        )
        self.assertFalse(result["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertIn("EXACT_REVISION_SPARE_MATCH_NOT_PASSED", result["blockers"])

    def test_physical_relink_model_is_rejected(self) -> None:
        meta = candidate()
        meta["bankNeutral"] = False
        meta["logicalLinkBase"] = 0x40000
        result = evaluate_preconditions(
            meta,
            production_flash_size=0x80000,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="TLSR8258",
            production_revision_proven=True,
            return_to_stock_spare_passed=True,
        )
        self.assertFalse(result["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertIn("BANK_NEUTRAL_ATTESTATION_MISSING", result["blockers"])
        self.assertIn("LOGICAL_LINK_BASE_NOT_ZERO", result["blockers"])

    def test_candidate_geometry_must_fit_both_physical_slots(self) -> None:
        meta = candidate()
        meta["physicalBEndExclusive"] = 0x76000
        result = evaluate_preconditions(
            meta,
            production_flash_size=0x80000,
            production_hw_version=2,
            current_file_version=0x26013001,
            production_mcu="TLSR8258",
            production_revision_proven=True,
            return_to_stock_spare_passed=True,
        )
        self.assertFalse(result["FLASH_WRITE_PRECONDITIONS_PASS"])
        self.assertIn("CANDIDATE_GEOMETRY_INVALID", result["blockers"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
