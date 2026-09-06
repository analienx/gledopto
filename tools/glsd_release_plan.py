#!/usr/bin/env python3
"""Build a non-executing, exact-target GL-SD OTA release plan.

The plan uses Zigbee2MQTT 2.14+'s per-device `{id, url}` OTA API. It deliberately
avoids a global override index so the candidate is not advertised to other
matching devices. The tool never publishes MQTT, serves a file, or talks to a
device.

A read-only/check request may be shown while production blockers remain. The
mutating update request is emitted only when the machine-readable flash
preflight passes. Even then `authorizationGranted` remains false: the final
operator authorization is intentionally outside this tool.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from urllib.parse import urlparse

from glsd_flash_preflight import evaluate_preconditions

TARGET_IEEE = "0xa4c13850cfcdb3a4"
BASE_TOPIC = "zigbee2mqtt"
CHECK_TOPIC = f"{BASE_TOPIC}/bridge/request/device/ota_update/check"
UPDATE_TOPIC = f"{BASE_TOPIC}/bridge/request/device/ota_update/update"
ABORT_TOPIC = f"{BASE_TOPIC}/bridge/request/device/ota_update/update/abort"


class ReleasePlanError(ValueError):
    pass


def _validate_url(url: str) -> None:
    parsed = urlparse(url)
    # Z2M accepts local paths and URLs, but for the eventual live transaction we
    # require an explicit HTTP(S) source so the operator is not relying on a
    # process-local path that may resolve differently inside the add-on.
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise ReleasePlanError("live candidate URL must be an explicit http(s) URL")
    if parsed.username or parsed.password:
        raise ReleasePlanError("do not embed credentials in the candidate URL")


def build_plan(
    metadata: dict,
    *,
    url: str,
    production_flash_size: int | None,
    production_hw_version: int,
    current_file_version: int,
    production_mcu: str,
    production_revision_proven: bool,
    return_to_stock_spare_passed: bool,
) -> dict:
    _validate_url(url)
    preflight = evaluate_preconditions(
        metadata,
        production_flash_size=production_flash_size,
        production_hw_version=production_hw_version,
        current_file_version=current_file_version,
        production_mcu=production_mcu,
        production_revision_proven=production_revision_proven,
        return_to_stock_spare_passed=return_to_stock_spare_passed,
    )

    check_request = {
        "topic": CHECK_TOPIC,
        "payload": {"id": TARGET_IEEE, "url": url},
        "mutatesFirmware": False,
    }
    abort_request = {
        "topic": ABORT_TOPIC,
        "payload": {"id": TARGET_IEEE},
        "mutatesFirmware": False,
    }
    update_request = None
    if preflight["FLASH_WRITE_PRECONDITIONS_PASS"]:
        update_request = {
            "topic": UPDATE_TOPIC,
            "payload": {"id": TARGET_IEEE, "url": url},
            "mutatesFirmware": True,
        }

    return {
        "schemaVersion": 1,
        "targetIeee": TARGET_IEEE,
        "usesGlobalOverrideIndex": False,
        "automaticUpdateChecksMustRemainDisabled": True,
        "scheduledOtaMustBeEmpty": True,
        "authorizationGranted": False,
        "preflight": preflight,
        "checkRequest": check_request,
        "updateRequest": update_request,
        "abortRequest": abort_request,
        "candidate": {
            "url": url,
            "sha256": metadata.get("sha256"),
            "sha512": metadata.get("sha512"),
            "fileVersion": metadata.get("fileVersion"),
            "manufacturerCode": metadata.get("manufacturerCode"),
            "imageType": metadata.get("imageType"),
            "hardwareVersionMin": metadata.get("hardwareVersionMin"),
            "hardwareVersionMax": metadata.get("hardwareVersionMax"),
            "bankNeutral": metadata.get("bankNeutral"),
            "innerSha256": metadata.get("innerSha256"),
        },
    }


def _int_auto(value: str) -> int:
    return int(value, 0)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("metadata", type=Path, help="quarantine sidecar from make_glsd_stager_ota.py")
    ap.add_argument("--url", required=True)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--production-flash-size", type=_int_auto)
    ap.add_argument("--production-hw-version", type=_int_auto, default=2)
    ap.add_argument("--current-file-version", type=_int_auto, default=0x26013001)
    ap.add_argument("--production-mcu", default="unknown")
    ap.add_argument("--production-revision-proven", action="store_true")
    ap.add_argument("--return-to-stock-spare-passed", action="store_true")
    ns = ap.parse_args(argv)

    metadata = json.loads(ns.metadata.read_text(encoding="utf-8"))
    plan = build_plan(
        metadata,
        url=ns.url,
        production_flash_size=ns.production_flash_size,
        production_hw_version=ns.production_hw_version,
        current_file_version=ns.current_file_version,
        production_mcu=ns.production_mcu,
        production_revision_proven=ns.production_revision_proven,
        return_to_stock_spare_passed=ns.return_to_stock_spare_passed,
    )
    encoded = json.dumps(plan, indent=2, sort_keys=True) + "\n"
    if ns.out:
        ns.out.write_text(encoded, encoding="utf-8")
    else:
        print(encoded, end="")
    return 0 if plan["preflight"]["FLASH_WRITE_PRECONDITIONS_PASS"] else 3


if __name__ == "__main__":
    raise SystemExit(main())
