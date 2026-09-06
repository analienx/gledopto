#!/usr/bin/env python3
"""Offline promotion preflight for a quarantined GL-SD-301P stager OTA.

This tool performs no device/network I/O and cannot flash or serve firmware. It
turns the remaining production unknowns into explicit fail-closed release gates.
A PASS means only that the stated preconditions are internally consistent; it
is not authorization to mutate a device.
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
EXPECTED_BANKS = {"bank_a", "bank_b"}
OPPOSITE_BANK = {"bank_a": "bank_b", "bank_b": "bank_a"}


class FlashPreflightError(ValueError):
    pass


def evaluate_preconditions(
    metadata: dict,
    *,
    active_bank: str,
    production_flash_size: int | None,
    production_hw_version: int,
    current_file_version: int,
    production_mcu: str,
    production_revision_proven: bool,
    return_to_stock_spare_passed: bool,
) -> dict:
    blockers: list[str] = []

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

    target_bank = metadata.get("targetBank")
    if target_bank not in EXPECTED_BANKS:
        blockers.append("TARGET_BANK_UNATTESTED")
    if active_bank not in EXPECTED_BANKS:
        blockers.append("ACTIVE_BANK_UNKNOWN")
    elif target_bank in EXPECTED_BANKS and target_bank != OPPOSITE_BANK[active_bank]:
        blockers.append("TARGET_BANK_IS_NOT_INACTIVE_BANK")

    if production_mcu != EXPECTED_MCU:
        blockers.append("PRODUCTION_MCU_UNPROVEN")
    if production_flash_size != EXPECTED_FLASH_SIZE:
        blockers.append("PRODUCTION_FLASH_GEOMETRY_UNPROVEN")
    if not production_revision_proven:
        blockers.append("PRODUCTION_REVISION_UNPROVEN")
    if production_hw_version != EXPECTED_HW_VERSION:
        blockers.append("LIVE_HW_VERSION_MISMATCH")
    if current_file_version != EXPECTED_STOCK_FILE_VERSION:
        blockers.append("LIVE_STOCK_VERSION_UNEXPECTED")

    candidate_file_version = metadata.get("fileVersion")
    if not isinstance(candidate_file_version, int) or candidate_file_version <= current_file_version:
        blockers.append("CANDIDATE_VERSION_NOT_NEWER")

    target_link_base = metadata.get("targetLinkBase")
    expected_link_base = 0x00000 if target_bank == "bank_a" else 0x40000 if target_bank == "bank_b" else None
    if expected_link_base is None or target_link_base != expected_link_base:
        blockers.append("TARGET_LINK_BASE_MISMATCH")

    start = metadata.get("physicalFlashStart")
    end = metadata.get("physicalFlashEndExclusive")
    slot_end = metadata.get("appSlotEnd")
    if not all(isinstance(v, int) for v in (start, end, slot_end)):
        blockers.append("CANDIDATE_GEOMETRY_MISSING")
    elif not (start == expected_link_base and start < end < slot_end <= EXPECTED_FLASH_SIZE):
        blockers.append("CANDIDATE_GEOMETRY_INVALID")

    if not return_to_stock_spare_passed:
        blockers.append("RETURN_TO_STOCK_SPARE_NOT_PASSED")

    blockers = sorted(set(blockers))
    return {
        "FLASH_WRITE_PRECONDITIONS_PASS": not blockers,
        "AUTHORIZATION_GRANTED": False,
        "candidateTargetBank": target_bank,
        "activeBank": active_bank,
        "expectedInactiveBank": OPPOSITE_BANK.get(active_bank),
        "blockers": blockers,
    }


def _int_auto(value: str) -> int:
    return int(value, 0)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path, help="*.quarantine.json sidecar from make_glsd_stager_ota.py")
    parser.add_argument("--active-bank", choices=("bank_a", "bank_b", "unknown"), default="unknown")
    parser.add_argument("--production-flash-size", type=_int_auto)
    parser.add_argument("--production-hw-version", type=_int_auto, default=EXPECTED_HW_VERSION)
    parser.add_argument("--current-file-version", type=_int_auto, default=EXPECTED_STOCK_FILE_VERSION)
    parser.add_argument("--production-mcu", default="unknown")
    parser.add_argument("--production-revision-proven", action="store_true")
    parser.add_argument("--return-to-stock-spare-passed", action="store_true")
    ns = parser.parse_args(argv)

    metadata = json.loads(ns.metadata.read_text(encoding="utf-8"))
    result = evaluate_preconditions(
        metadata,
        active_bank=ns.active_bank,
        production_flash_size=ns.production_flash_size,
        production_hw_version=ns.production_hw_version,
        current_file_version=ns.current_file_version,
        production_mcu=ns.production_mcu,
        production_revision_proven=ns.production_revision_proven,
        return_to_stock_spare_passed=ns.return_to_stock_spare_passed,
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["FLASH_WRITE_PRECONDITIONS_PASS"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
