#!/usr/bin/env python3
"""Create a one-image Zigbee2MQTT override index for a GL-SD stager OTA.

Offline only: this tool never serves an image or talks to Zigbee. It refuses an
image unless the outer OTA identity is exact, the file version is above the
observed stock version, the Telink application validates, and the hardware lock
is GL-SD-301P hwVersion 2.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import telink_ota_forensics as forensics

TARGET_MODEL = "GL-SD-301P"
TARGET_MANUFACTURER = "GLEDOPTO"
TARGET_HW = 2
TARGET_MFG_CODE = 0x124F
TARGET_IMAGE_TYPE = 0x1416
STOCK_FILE_VERSION = 0x26013001


def build_entry(image: Path, *, url: str) -> dict:
    if not url or not isinstance(url, str):
        raise ValueError("an explicit image URL/path reference is required")
    data = image.read_bytes()
    report = forensics.analyze(image)
    header = report["header"]
    if header["manufacturer_code"] != TARGET_MFG_CODE:
        raise ValueError("OTA manufacturer code is not the target 0x124F")
    if header["image_type"] != TARGET_IMAGE_TYPE:
        raise ValueError("OTA image type is not the target 0x1416")
    if header["file_version"] <= STOCK_FILE_VERSION:
        raise ValueError("OTA file version must be strictly higher than stock")
    if not report["total_size_matches_header"]:
        raise ValueError("OTA container total size does not match header")
    upgrade = report.get("upgrade_image")
    if not upgrade:
        raise ValueError("OTA has no upgrade-image subelement")
    validation = upgrade.get("application_validation") or {}
    if not validation.get("valid"):
        raise ValueError("inner Telink application is not boot-valid")
    if upgrade.get("manufacturer_code_at_0x12") != TARGET_MFG_CODE:
        raise ValueError("inner Telink manufacturer code is not target identity")
    if upgrade.get("image_type_at_0x14") != TARGET_IMAGE_TYPE:
        raise ValueError("inner Telink image type is not target identity")
    if upgrade.get("file_version_at_0x02") != header["file_version"]:
        raise ValueError("inner and outer file versions differ")

    return {
        "fileName": image.name,
        "fileVersion": header["file_version"],
        "fileSize": len(data),
        "url": url,
        "imageType": TARGET_IMAGE_TYPE,
        "manufacturerCode": TARGET_MFG_CODE,
        "sha512": hashlib.sha512(data).hexdigest(),
        "otaHeaderString": header["header_string"],
        "modelId": TARGET_MODEL,
        "manufacturerName": TARGET_MANUFACTURER,
        "hardwareVersionMin": TARGET_HW,
        "hardwareVersionMax": TARGET_HW,
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Build offline target-locked GL-SD OTA index")
    ap.add_argument("image", type=Path)
    ap.add_argument("--url", required=True, help="exact URL/path Z2M should use later; nothing is served by this tool")
    ap.add_argument("--out", type=Path, required=True)
    ns = ap.parse_args(argv)
    entry = build_entry(ns.image, url=ns.url)
    ns.out.write_text(json.dumps([entry], indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"out": str(ns.out), "sha512": entry["sha512"], "fileVersion": entry["fileVersion"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
