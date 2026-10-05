#!/usr/bin/env python3
"""Create a non-executing exact-byte/exact-IEEE GL-SD-301P canary plan.

The output is a review artifact, never an authorization or network action.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from urllib.parse import urlparse

from make_glsd301p_ed_ota import TARGET_IEEE


class ReleasePlanError(ValueError):
    pass


def _valid_url(url: str) -> bool:
    parsed = urlparse(url)
    return parsed.scheme in {"http", "https"} and bool(parsed.netloc)


def build_plan(candidate: Path, sidecar_path: Path, preflight_path: Path, url: str) -> dict:
    if not _valid_url(url):
        raise ReleasePlanError("candidate URL must be explicit http(s), not a file/global-index path")
    data = candidate.read_bytes()
    sidecar = json.loads(sidecar_path.read_text(encoding="utf-8"))
    preflight = json.loads(preflight_path.read_text(encoding="utf-8"))
    digest256 = hashlib.sha256(data).hexdigest()
    digest512 = hashlib.sha512(data).hexdigest()
    if sidecar.get("targetIeee") != TARGET_IEEE:
        raise ReleasePlanError("candidate sidecar is not locked to the production IEEE")
    if sidecar.get("bytes") != len(data):
        raise ReleasePlanError("candidate byte count differs from quarantine sidecar")
    if sidecar.get("sha256") != digest256 or sidecar.get("sha512") != digest512:
        raise ReleasePlanError("candidate bytes do not match quarantine hashes")
    if sidecar.get("authorizationGranted") is not False:
        raise ReleasePlanError("candidate sidecar authorization must be literal false")
    if preflight.get("targetIeee") != TARGET_IEEE:
        raise ReleasePlanError("preflight target IEEE mismatch")
    if preflight.get("AUTHORIZATION_GRANTED") is not False:
        raise ReleasePlanError("preflight authorization must be literal false")
    if preflight.get("candidate", {}).get("sha256") != digest256:
        raise ReleasePlanError("preflight is not bound to these candidate bytes")

    decision_ready = preflight.get("CANARY_DECISION_READY") is True
    request = None
    if decision_ready:
        request = {
            "topic": "zigbee2mqtt/bridge/request/device/ota_update/update",
            "payload": {"id": TARGET_IEEE, "url": url},
        }
    return {
        "schema": 1,
        "planType": "GLSD301P_EXACT_IEEE_CANARY_REVIEW",
        "targetIeee": TARGET_IEEE,
        "usesGlobalOverrideIndex": False,
        "candidateUrl": url,
        "candidate": {
            "file": candidate.name,
            "bytes": len(data),
            "sha256": digest256,
            "sha512": digest512,
            "sourceHead": sidecar.get("sourceHead"),
            "fileVersion": sidecar.get("fileVersion"),
        },
        "softwarePreflightPass": preflight.get("SOFTWARE_PREFLIGHT_PASS") is True,
        "canaryDecisionReady": decision_ready,
        "firstValidCustomBootRiskAccepted": preflight.get("firstValidCustomBootRiskAccepted") is True,
        "plannedUpdateRequest": request,
        "authorizationGranted": False,
        "AUTHORIZATION_GRANTED": False,
        "executionPermitted": False,
        "humanAuthorizationRequiredAfterPlan": True,
        "recovery": {
            "sameModelVendorImageAvailable": preflight.get("vendorRecovery", {}).get(
                "sameModelVendorRecoveryAvailable"
            ) is True,
            "postBootRecoverySourceGatePass": preflight.get(
                "postBootVendorRecoverySourceGate", {}
            ).get("pass") is True,
            "firstValidCustomBootRecoveryProven": False,
            "vendorRecoveryFileVersion": preflight.get("vendorRecovery", {}).get("fileVersion"),
            "vendorRecoverySha256": preflight.get("vendorRecovery", {}).get("sha256"),
        },
        "stopBoundary": (
            "This plan is inert. A separate explicit human authorization is required immediately "
            "before any live per-device OTA request; all live tuple/environment gates must be re-read then."
        ),
        "residualRisk": preflight.get("residualRisk"),
    }


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("candidate", type=Path)
    p.add_argument("sidecar", type=Path)
    p.add_argument("preflight", type=Path)
    p.add_argument("--url", required=True)
    p.add_argument("--out", type=Path, required=True)
    a = p.parse_args(argv)
    try:
        plan = build_plan(a.candidate, a.sidecar, a.preflight, a.url)
    except ReleasePlanError as exc:
        p.error(str(exc))
    a.out.write_text(json.dumps(plan, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(plan, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
