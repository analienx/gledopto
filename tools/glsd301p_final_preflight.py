#!/usr/bin/env python3
"""Offline final preflight for the GL-SD-301P production-only/no-spare track.

The tool can establish that the software/evidence package is ready for a human
canary decision. It can never authorize, serve, schedule, or execute an OTA.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from telink_app_finalize import validate_finalized_image
from make_glsd301p_ed_ota import (
    DEFAULT_FILE_VERSION,
    DEFAULT_IMAGE_TYPE,
    DEFAULT_MANUFACTURER,
    TARGET_HW_VERSION,
    TARGET_IEEE,
    TARGET_MODEL,
    TARGET_VENDOR,
    validate_ota,
)

STOCK_SW_BUILD = "20651203"
STOCK_DATE_CODE = "20240704"
STOCK_FILE_VERSION = 0x26013001
CUSTOM_FILE_VERSION = DEFAULT_FILE_VERSION
VENDOR_RECOVERY_FILE_VERSION = 0x28013001
VENDOR_RECOVERY_SHA256 = "16595a38ab9783d3afc4eb58ab4ec32625249bd569c1fa6dbaf468bddc76dd72"
VENDOR_RECOVERY_SHA512 = "3e8d9101157ee5363ab91352d3f09d3955a5a0403030d8aa7f48ae429161423902203991324c5156403399725e4d3ddedb659523dc1c7988fdcdb5b054aa3cb0"
VENDOR_RECOVERY_BYTES = 208946


class FinalPreflightError(ValueError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sha512(path: Path) -> str:
    return hashlib.sha512(path.read_bytes()).hexdigest()


def _parse_vendor_ota(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 62:
        raise FinalPreflightError("vendor recovery OTA too short")
    magic, header_ver, header_len, field_control, mfr, image, version, stack, name, total = struct.unpack_from(
        "<IHHHHHIH32sI", data, 0
    )
    if magic != 0x0BEEF11E or header_ver != 0x0100:
        raise FinalPreflightError("vendor recovery Zigbee OTA header invalid")
    if total != len(data) or not (56 <= header_len <= len(data) - 6):
        raise FinalPreflightError("vendor recovery total/header length invalid")
    tag, length = struct.unpack_from("<HI", data, header_len)
    start = header_len + 6
    end = start + length
    if tag != 0x0000 or end != len(data):
        raise FinalPreflightError("vendor recovery must contain one standard upgrade-image subelement")
    inner = data[start:end]
    preamble = validate_finalized_image(
        inner,
        manufacturer_code=DEFAULT_MANUFACTURER,
        image_type=DEFAULT_IMAGE_TYPE,
        file_version=VENDOR_RECOVERY_FILE_VERSION,
        max_final_size=0x34000,
    )
    return {
        "manufacturerCode": mfr,
        "imageType": image,
        "fileVersion": version,
        "stackVersion": stack,
        "fieldControl": field_control,
        "headerString": name.rstrip(b"\x00").decode("ascii", errors="replace"),
        "innerBytes": len(inner),
        "innerDeclaredBytes": preamble.declared_size,
    }


def validate_vendor_recovery(vendor_ota: Path, vendor_manifest: Path) -> dict:
    manifest = json.loads(vendor_manifest.read_text(encoding="utf-8"))
    artifacts = manifest.get("artifacts", [])
    expected_name = vendor_ota.name
    matches = [entry for entry in artifacts if entry.get("filename") == expected_name]
    if len(matches) != 1:
        raise FinalPreflightError("vendor manifest does not uniquely bind the recovery OTA")
    entry = matches[0]
    if manifest.get("device") != TARGET_MODEL or manifest.get("vendor") != TARGET_VENDOR:
        raise FinalPreflightError("vendor manifest is not same-model/same-vendor")
    if entry.get("byte_identical_to_obtained_original") is not True:
        raise FinalPreflightError("vendor recovery is not attested byte-identical to obtained original")
    if entry.get("project_license_applies") is not False:
        raise FinalPreflightError("vendor recovery licensing boundary is not explicit")
    if entry.get("size_bytes") != VENDOR_RECOVERY_BYTES or len(vendor_ota.read_bytes()) != VENDOR_RECOVERY_BYTES:
        raise FinalPreflightError("vendor recovery byte count mismatch")
    if entry.get("sha256") != VENDOR_RECOVERY_SHA256 or sha256(vendor_ota) != VENDOR_RECOVERY_SHA256:
        raise FinalPreflightError("vendor recovery SHA-256 mismatch")
    if entry.get("sha512") != VENDOR_RECOVERY_SHA512 or sha512(vendor_ota) != VENDOR_RECOVERY_SHA512:
        raise FinalPreflightError("vendor recovery SHA-512 mismatch")
    z = entry.get("zigbee_ota", {})
    if (z.get("manufacturer_code"), z.get("image_type"), z.get("file_version")) != (
        "0x124F", "0x1416", "0x28013001"
    ):
        raise FinalPreflightError("vendor manifest OTA identity mismatch")
    parsed = _parse_vendor_ota(vendor_ota)
    if (parsed["manufacturerCode"], parsed["imageType"], parsed["fileVersion"]) != (
        DEFAULT_MANUFACTURER, DEFAULT_IMAGE_TYPE, VENDOR_RECOVERY_FILE_VERSION
    ):
        raise FinalPreflightError("vendor recovery OTA bytes have wrong identity")
    return {
        "sameModelVendorRecoveryAvailable": True,
        "fileVersion": VENDOR_RECOVERY_FILE_VERSION,
        "bytes": VENDOR_RECOVERY_BYTES,
        "sha256": VENDOR_RECOVERY_SHA256,
        "sha512": VENDOR_RECOVERY_SHA512,
        "telinkInnerValid": True,
        **parsed,
    }


def validate_live_tuple(live: dict) -> list[str]:
    expected = {
        "ieee": TARGET_IEEE,
        "model": TARGET_MODEL,
        "manufacturer": TARGET_VENDOR,
        "swBuildId": STOCK_SW_BUILD,
        "dateCode": STOCK_DATE_CODE,
        "hwVersion": 2,
        "appVersion": 1,
        "stackVersion": 2,
        "manufacturerCode": DEFAULT_MANUFACTURER,
        "imageType": DEFAULT_IMAGE_TYPE,
        "fileVersion": STOCK_FILE_VERSION,
    }
    blockers = []
    for key, value in expected.items():
        if live.get(key) != value:
            blockers.append(f"LIVE_{key.upper()}_MISMATCH")
    return blockers


def evaluate(
    candidate_ota: Path,
    candidate_sidecar: Path,
    vendor_ota: Path,
    vendor_manifest: Path,
    recovery_contract: Path,
    live_tuple: Path,
    *,
    expected_source_head: str | None,
    full_size_shadow_passed: bool,
    post_rejection_power_cycle_passed: bool,
    accept_first_valid_custom_boot_risk: bool,
) -> dict:
    blockers: list[str] = []
    sidecar = json.loads(candidate_sidecar.read_text(encoding="utf-8"))
    candidate = candidate_ota.read_bytes()
    try:
        parsed_candidate = validate_ota(candidate)
    except Exception as exc:
        parsed_candidate = {}
        blockers.append(f"CANDIDATE_OTA_INVALID:{exc}")

    checks = {
        "QUARANTINED_BUILD": sidecar.get("QUARANTINED_BUILD") is True,
        "DEPLOYABLE_FALSE": sidecar.get("DEPLOYABLE") is False,
        "AUTHORIZATION_FALSE": sidecar.get("authorizationGranted") is False,
        "EXACT_TARGET_IEEE": sidecar.get("targetIeee") == TARGET_IEEE,
        "EXACT_MODEL": sidecar.get("targetModel") == TARGET_MODEL,
        "EXACT_VENDOR": sidecar.get("targetVendor") == TARGET_VENDOR,
        "HW2_LOCK": sidecar.get("hardwareVersionMin") == 2 and sidecar.get("hardwareVersionMax") == 2,
        "BANK_NEUTRAL": sidecar.get("bankNeutral") is True and sidecar.get("logicalLinkBase") == 0,
        "PHYSICAL_TARGETS": sidecar.get("physicalBootTargets") == [0, 0x40000],
        "PIN_PB3_PROVEN": sidecar.get("hardwareContract", {}).get("adcFlashSafetyPin") == "PB3"
            and sidecar.get("hardwareContract", {}).get("adcFlashSafetyEvidence") == "same-model-vendor-firmware-confirmed",
        "END_DEVICE_ROLE": sidecar.get("architecture", {}).get("endDeviceRole") is True,
        "NO_ROUTER_ROLE": sidecar.get("architecture", {}).get("routerRole") is False,
        "RX_ON_WHEN_IDLE": sidecar.get("architecture", {}).get("rxOnWhenIdle") is True,
        "PM_DISABLED": sidecar.get("architecture", {}).get("powerManagementEnabled") is False,
        "ENDPOINT_11": sidecar.get("architecture", {}).get("endpoint") == 11,
        "MAINS_POWER": sidecar.get("architecture", {}).get("powerSource") == "MAINS",
        "CANDIDATE_SHA256_BOUND": sidecar.get("sha256") == hashlib.sha256(candidate).hexdigest(),
        "CANDIDATE_SHA512_BOUND": sidecar.get("sha512") == hashlib.sha512(candidate).hexdigest(),
        "CANDIDATE_SIZE_BOUND": sidecar.get("bytes") == len(candidate),
    }
    if expected_source_head is not None:
        checks["SOURCE_HEAD_BOUND"] = sidecar.get("sourceHead") == expected_source_head.lower()
    for name, passed in checks.items():
        if not passed:
            blockers.append(name)

    if parsed_candidate and parsed_candidate.get("fileVersion") != CUSTOM_FILE_VERSION:
        blockers.append("CUSTOM_FILE_VERSION_MISMATCH")

    try:
        recovery = validate_vendor_recovery(vendor_ota, vendor_manifest)
    except Exception as exc:
        recovery = {"sameModelVendorRecoveryAvailable": False, "error": str(exc)}
        blockers.append("VENDOR_RECOVERY_IMAGE_INVALID")

    recovery_source = json.loads(recovery_contract.read_text(encoding="utf-8"))
    if recovery_source.get("pass") is not True:
        blockers.append("POST_BOOT_VENDOR_RECOVERY_SOURCE_UNPROVEN")
    if recovery_source.get("firstValidCustomBootRecoveryProven") is not False:
        blockers.append("RECOVERY_SOURCE_OVERCLAIMS_FIRST_BOOT")
    if recovery_source.get("authorizationGranted") is not False:
        blockers.append("RECOVERY_SOURCE_AUTHORIZATION_NOT_FALSE")

    live = json.loads(live_tuple.read_text(encoding="utf-8"))
    blockers.extend(validate_live_tuple(live))
    if not full_size_shadow_passed:
        blockers.append("FULL_SIZE_SHADOW_EMPIRICAL_GATE_NOT_PASSED")
    if not post_rejection_power_cycle_passed:
        blockers.append("POST_REJECTION_POWER_CYCLE_NOT_PASSED")
    if not accept_first_valid_custom_boot_risk:
        blockers.append("FIRST_VALID_CUSTOM_BOOT_RISK_NOT_ACCEPTED")

    software_blockers = [
        b for b in blockers if b != "FIRST_VALID_CUSTOM_BOOT_RISK_NOT_ACCEPTED"
    ]
    software_pass = not software_blockers
    canary_ready = not blockers
    return {
        "schema": 1,
        "track": "PRODUCTION_ONLY_NO_SPARE",
        "targetIeee": TARGET_IEEE,
        "SOFTWARE_PREFLIGHT_PASS": software_pass,
        "CANARY_DECISION_READY": canary_ready,
        "AUTHORIZATION_GRANTED": False,
        "authorizationGranted": False,
        "EXECUTION_PERMITTED": False,
        "blockers": blockers,
        "checks": checks,
        "fullSizeShadowEmpiricalGatePassed": full_size_shadow_passed,
        "postRejectionPowerCyclePassed": post_rejection_power_cycle_passed,
        "firstValidCustomBootRiskAccepted": accept_first_valid_custom_boot_risk,
        "hardwareEvidenceSource": "same-model-vendor-firmware+production-shadow-probes",
        "directProductionGeometryProven": False,
        "productionGeometryEmpiricallyBounded": full_size_shadow_passed and post_rejection_power_cycle_passed,
        "candidate": {
            "file": candidate_ota.name,
            "bytes": len(candidate),
            "sha256": hashlib.sha256(candidate).hexdigest(),
            "sha512": hashlib.sha512(candidate).hexdigest(),
            "sourceHead": sidecar.get("sourceHead"),
            "fileVersion": CUSTOM_FILE_VERSION,
        },
        "liveStockTuple": live,
        "vendorRecovery": recovery,
        "postBootVendorRecoverySourceGate": recovery_source,
        "residualRisk": (
            "Irreducible no-spare first-valid-custom-boot risk remains: if the new image is activated "
            "but fails before Zigbee/OTA is reachable, the vendor recovery OTA cannot be delivered "
            "wirelessly; wired/open-device recovery or replacement is the contingency."
        ),
    }


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("candidate_ota", type=Path)
    p.add_argument("candidate_sidecar", type=Path)
    p.add_argument("--vendor-ota", type=Path, required=True)
    p.add_argument("--vendor-manifest", type=Path, required=True)
    p.add_argument("--recovery-contract", type=Path, required=True)
    p.add_argument("--live-tuple", type=Path, required=True)
    p.add_argument("--expected-source-head")
    p.add_argument("--full-size-shadow-passed", action="store_true")
    p.add_argument("--post-rejection-power-cycle-passed", action="store_true")
    p.add_argument("--accept-first-valid-custom-boot-risk", action="store_true")
    p.add_argument("--out", type=Path)
    a = p.parse_args(argv)
    report = evaluate(
        a.candidate_ota,
        a.candidate_sidecar,
        a.vendor_ota,
        a.vendor_manifest,
        a.recovery_contract,
        a.live_tuple,
        expected_source_head=a.expected_source_head,
        full_size_shadow_passed=a.full_size_shadow_passed,
        post_rejection_power_cycle_passed=a.post_rejection_power_cycle_passed,
        accept_first_valid_custom_boot_risk=a.accept_first_valid_custom_boot_risk,
    )
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if a.out:
        a.out.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0 if report["CANARY_DECISION_READY"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
