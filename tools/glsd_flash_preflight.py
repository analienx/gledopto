#!/usr/bin/env python3
"""Offline promotion preflight for a quarantined GL-SD-301P stager OTA.

This tool performs no device/network I/O and cannot flash or serve firmware. It
turns the remaining production unknowns into explicit fail-closed release gates.
A PASS means only that the stated preconditions are internally consistent; it
is not authorization to mutate a device.

TLSR8258 standard Zigbee OTA is bank-neutral: one logical-0 application can be
written to either physical boot slot by the SDK's ping-pong OTA engine. Therefore
active-bank knowledge is deliberately NOT a precondition for the first OTA.

Production hardware evidence is intentionally explicit. The preferred strongest
mode is ``installed-direct`` (the installed target itself was physically
identified). A second mode, ``exact-revision-spare``, permits high-confidence
inference from a sacrificial unit only when that spare matches the complete live
revision tuple, has the expected MCU/512-KiB geometry, has passed the full
return-to-stock proof, and the operator explicitly accepts the residual risk that
a vendor could have shipped a different BOM under identical identifiers.
Neither mode grants final authorization.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

EXPECTED_MANUFACTURER = 0x124F
EXPECTED_IMAGE_TYPE = 0x1416
EXPECTED_HW_VERSION = 2
EXPECTED_FLASH_SIZE = 0x80000
EXPECTED_STOCK_FILE_VERSION = 0x26013001
EXPECTED_MCU = "TLSR8258"
BANK_A_BASE = 0x00000
BANK_B_BASE = 0x40000
BANK_A_SLOT_END = 0x34000
BANK_B_SLOT_END = 0x74000

HARDWARE_EVIDENCE_UNPROVEN = "unproven"
HARDWARE_EVIDENCE_INSTALLED_DIRECT = "installed-direct"
HARDWARE_EVIDENCE_EXACT_SPARE = "exact-revision-spare"
HARDWARE_EVIDENCE_CHOICES = {
    HARDWARE_EVIDENCE_UNPROVEN,
    HARDWARE_EVIDENCE_INSTALLED_DIRECT,
    HARDWARE_EVIDENCE_EXACT_SPARE,
}


class FlashPreflightError(ValueError):
    pass


def evaluate_preconditions(
    metadata: dict,
    *,
    production_flash_size: int | None,
    production_hw_version: int,
    current_file_version: int,
    production_mcu: str,
    production_revision_proven: bool,
    return_to_stock_spare_passed: bool,
    hardware_evidence_source: str = HARDWARE_EVIDENCE_UNPROVEN,
    exact_revision_spare_match_passed: bool = False,
    accept_spare_inference_for_production: bool = False,
) -> dict:
    blockers: list[str] = []

    if hardware_evidence_source not in HARDWARE_EVIDENCE_CHOICES:
        raise FlashPreflightError(
            f"invalid hardware evidence source: {hardware_evidence_source!r}"
        )

    # Backward-compatible interpretation for older callers that supplied only
    # --production-revision-proven. New callers should set the evidence source
    # explicitly so the resulting plan records where the geometry claim came from.
    effective_hardware_evidence = hardware_evidence_source
    if (
        effective_hardware_evidence == HARDWARE_EVIDENCE_UNPROVEN
        and production_revision_proven
    ):
        effective_hardware_evidence = HARDWARE_EVIDENCE_INSTALLED_DIRECT

    if metadata.get("DEPLOYABLE") is not False:
        blockers.append("CANDIDATE_NOT_QUARANTINED")
    if metadata.get("DO_NOT_SERVE_TO_PRODUCTION") is not True:
        blockers.append("QUARANTINE_MARKER_MISSING")
    if metadata.get("manufacturerCode") != EXPECTED_MANUFACTURER:
        blockers.append("MANUFACTURER_MISMATCH")
    if metadata.get("imageType") != EXPECTED_IMAGE_TYPE:
        blockers.append("IMAGE_TYPE_MISMATCH")
    if metadata.get("hardwareVersionMin") != EXPECTED_HW_VERSION or metadata.get(
        "hardwareVersionMax"
    ) != EXPECTED_HW_VERSION:
        blockers.append("CANDIDATE_HW_RANGE_MISMATCH")
    if not metadata.get("innerValid"):
        blockers.append("INNER_IMAGE_NOT_VALIDATED")

    # A standard Telink OTA payload must be one logical-0 multi-address image.
    if metadata.get("bankNeutral") is not True:
        blockers.append("BANK_NEUTRAL_ATTESTATION_MISSING")
    if metadata.get("logicalLinkBase") != 0:
        blockers.append("LOGICAL_LINK_BASE_NOT_ZERO")
    if metadata.get("runtimeBootBankDetection") != "mcuBootAddrGet":
        blockers.append("RUNTIME_BOOT_BANK_DETECTION_MISSING")
    if metadata.get("physicalBootTargets") != [BANK_A_BASE, BANK_B_BASE]:
        blockers.append("MULTI_ADDRESS_BOOT_TARGETS_MISMATCH")

    end_a = metadata.get("physicalAEndExclusive")
    end_b = metadata.get("physicalBEndExclusive")
    slot_a = metadata.get("bankASlotEnd")
    slot_b = metadata.get("bankBSlotEnd")
    if not all(isinstance(v, int) for v in (end_a, end_b, slot_a, slot_b)):
        blockers.append("CANDIDATE_GEOMETRY_MISSING")
    elif not (
        BANK_A_BASE < end_a < slot_a == BANK_A_SLOT_END
        and BANK_B_BASE < end_b < slot_b == BANK_B_SLOT_END
        and end_b <= EXPECTED_FLASH_SIZE
    ):
        blockers.append("CANDIDATE_GEOMETRY_INVALID")

    # Hardware facts must match the target profile regardless of how they were
    # obtained. In exact-revision-spare mode these values are corroborated on the
    # spare and then explicitly treated as an inference for the installed unit.
    if production_mcu != EXPECTED_MCU:
        blockers.append("PRODUCTION_MCU_UNPROVEN")
    if production_flash_size != EXPECTED_FLASH_SIZE:
        blockers.append("PRODUCTION_FLASH_GEOMETRY_UNPROVEN")

    direct_production_geometry_proven = False
    production_geometry_inferred_from_spare = False

    if effective_hardware_evidence == HARDWARE_EVIDENCE_INSTALLED_DIRECT:
        if not production_revision_proven:
            blockers.append("PRODUCTION_REVISION_UNPROVEN")
        else:
            direct_production_geometry_proven = (
                production_mcu == EXPECTED_MCU
                and production_flash_size == EXPECTED_FLASH_SIZE
            )

    elif effective_hardware_evidence == HARDWARE_EVIDENCE_EXACT_SPARE:
        if production_revision_proven:
            blockers.append("CONFLICTING_HARDWARE_EVIDENCE_MODE")
        if not exact_revision_spare_match_passed:
            blockers.append("EXACT_REVISION_SPARE_MATCH_NOT_PASSED")
        if not return_to_stock_spare_passed:
            blockers.append("RETURN_TO_STOCK_SPARE_NOT_PASSED")
        if not accept_spare_inference_for_production:
            blockers.append("SPARE_GEOMETRY_INFERENCE_NOT_ACCEPTED")
        if (
            exact_revision_spare_match_passed
            and return_to_stock_spare_passed
            and accept_spare_inference_for_production
            and production_mcu == EXPECTED_MCU
            and production_flash_size == EXPECTED_FLASH_SIZE
        ):
            production_geometry_inferred_from_spare = True

    else:
        if not production_revision_proven:
            blockers.append("PRODUCTION_REVISION_UNPROVEN")

    if production_hw_version != EXPECTED_HW_VERSION:
        blockers.append("LIVE_HW_VERSION_MISMATCH")
    if current_file_version != EXPECTED_STOCK_FILE_VERSION:
        blockers.append("LIVE_STOCK_VERSION_UNEXPECTED")

    candidate_file_version = metadata.get("fileVersion")
    if not isinstance(candidate_file_version, int) or candidate_file_version <= current_file_version:
        blockers.append("CANDIDATE_VERSION_NOT_NEWER")

    # The return-to-stock canary is always required before production, regardless
    # of whether installed hardware was inspected directly.
    if not return_to_stock_spare_passed:
        blockers.append("RETURN_TO_STOCK_SPARE_NOT_PASSED")

    blockers = sorted(set(blockers))
    return {
        "FLASH_WRITE_PRECONDITIONS_PASS": not blockers,
        "AUTHORIZATION_GRANTED": False,
        "bankNeutral": metadata.get("bankNeutral") is True,
        "activeBankRequiredForFirstOta": False,
        "hardwareEvidenceSource": effective_hardware_evidence,
        "directProductionGeometryProven": direct_production_geometry_proven,
        "productionGeometryInferredFromExactRevisionSpare": (
            production_geometry_inferred_from_spare
        ),
        "spareInferenceAccepted": accept_spare_inference_for_production,
        "blockers": blockers,
    }


def _int_auto(value: str) -> int:
    return int(value, 0)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path, help="*.quarantine.json sidecar from make_glsd_stager_ota.py")
    parser.add_argument("--production-flash-size", type=_int_auto)
    parser.add_argument("--production-hw-version", type=_int_auto, default=EXPECTED_HW_VERSION)
    parser.add_argument("--current-file-version", type=_int_auto, default=EXPECTED_STOCK_FILE_VERSION)
    parser.add_argument("--production-mcu", default="unknown")
    parser.add_argument("--production-revision-proven", action="store_true")
    parser.add_argument("--return-to-stock-spare-passed", action="store_true")
    parser.add_argument(
        "--hardware-evidence-source",
        choices=sorted(HARDWARE_EVIDENCE_CHOICES),
        default=HARDWARE_EVIDENCE_UNPROVEN,
        help="where the MCU/flash/revision evidence came from",
    )
    parser.add_argument("--exact-revision-spare-match-passed", action="store_true")
    parser.add_argument(
        "--accept-spare-inference-for-production",
        action="store_true",
        help=(
            "explicitly accept residual same-identifiers/different-BOM risk when "
            "using an exact-revision spare instead of opening the installed unit"
        ),
    )
    ns = parser.parse_args(argv)

    metadata = json.loads(ns.metadata.read_text(encoding="utf-8"))
    result = evaluate_preconditions(
        metadata,
        production_flash_size=ns.production_flash_size,
        production_hw_version=ns.production_hw_version,
        current_file_version=ns.current_file_version,
        production_mcu=ns.production_mcu,
        production_revision_proven=ns.production_revision_proven,
        return_to_stock_spare_passed=ns.return_to_stock_spare_passed,
        hardware_evidence_source=ns.hardware_evidence_source,
        exact_revision_spare_match_passed=ns.exact_revision_spare_match_passed,
        accept_spare_inference_for_production=ns.accept_spare_inference_for_production,
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["FLASH_WRITE_PRECONDITIONS_PASS"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
