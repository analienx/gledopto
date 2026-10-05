#!/usr/bin/env python3
"""Build and attest the quarantined GL-SD-301P End Device OTA container.

This is an offline packaging tool only. It does not contact Zigbee2MQTT, serve
files, or authorize a live update. The resulting sidecar remains explicitly
quarantined and binds the OTA bytes to the real pinned-TC32 target manifest.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from telink_app_finalize import (
    DEFAULT_FILE_VERSION,
    DEFAULT_IMAGE_TYPE,
    DEFAULT_MANUFACTURER,
    validate_finalized_image,
)

OTA_MAGIC = 0x0BEEF11E
OTA_HEADER_VERSION = 0x0100
OTA_FIELD_HW_RANGE = 0x0004
OTA_HEADER_LEN = 60
OTA_UPGRADE_TAG = 0x0000
ZIGBEE_STACK_VERSION = 0x0002
TARGET_HW_VERSION = 2
TARGET_IEEE = "0xa4c13850cfcdb3a4"
TARGET_MODEL = "GL-SD-301P"
TARGET_VENDOR = "GLEDOPTO"
HEADER_STRING = b"GLSD301P ED QUARANTINED"
BANK_A_BASE = 0x00000
BANK_B_BASE = 0x40000
BANK_A_SLOT_END = 0x34000
BANK_B_SLOT_END = 0x74000
EXPECTED_SDK_COMMIT = "d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e"


class EndDeviceOtaError(ValueError):
    pass


def _parse_int(value: str, field: str) -> int:
    try:
        return int(value, 0)
    except (TypeError, ValueError) as exc:
        raise EndDeviceOtaError(f"invalid {field}: {value!r}") from exc


def parse_build_manifest(path: Path) -> tuple[dict[str, str], dict[str, str]]:
    text = path.read_text(encoding="utf-8")
    fields: dict[str, str] = {}
    hashes: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        digest = re.fullmatch(r"([0-9a-fA-F]{64})\s+(.+)", line)
        if digest:
            hashes[Path(digest.group(2)).name] = digest.group(1).lower()
        elif "=" in line:
            key, value = line.split("=", 1)
            fields[key.strip()] = value.strip()
    return fields, hashes


def validate_target_manifest(manifest_path: Path, inner_path: Path) -> dict:
    fields, hashes = parse_build_manifest(manifest_path)
    expected = {
        "QUARANTINED_BUILD": "YES",
        "DEPLOYABLE": "NO",
        "FIRST_FLASHABLE_CANARY_ALLOWED": "NO",
        "SDK_EXPECTED_COMMIT": EXPECTED_SDK_COMMIT,
        "STACK_ARCHIVE": "libzb_ed.a",
        "ROUTER_ARCHIVE_LINKED": "NO",
        "ZB_ED_ROLE": "1",
        "ZB_ROUTER_ROLE": "0",
        "ZB_MAC_RX_ON_WHEN_IDLE": "1",
        "PM_ENABLE": "0",
        "ENDPOINT": "11",
        "POWER_SOURCE": "MAINS",
        "BOOT_FIRST_POWER_STAGE_FRAME": "A55A010004AA",
        "FAMILY_0x02_CORE_RUNTIME": "ABSENT",
        "ADC_FLASH_SAFETY_PIN": "GPIO_PB3_VENDOR_FIRMWARE_CONFIRMED",
        "UART": "9600_8N1_PB1_TX_PA0_RX",
        "REACHABLE_TARGET_RUNTIME_CHAIN": "PASS",
    }
    missing = sorted(k for k in expected if k not in fields)
    if missing:
        raise EndDeviceOtaError(f"build manifest missing fields: {', '.join(missing)}")
    wrong = [f"{k}={fields[k]!r}" for k, v in expected.items() if fields[k] != v]
    if wrong:
        raise EndDeviceOtaError("build manifest violates target contract: " + ", ".join(wrong))

    inner = inner_path.read_bytes()
    validate_finalized_image(
        inner,
        manufacturer_code=DEFAULT_MANUFACTURER,
        image_type=DEFAULT_IMAGE_TYPE,
        file_version=DEFAULT_FILE_VERSION,
        max_final_size=BANK_A_SLOT_END,
    )
    declared_size = _parse_int(fields.get("FINAL_BINARY_SIZE", ""), "FINAL_BINARY_SIZE")
    if declared_size != len(inner):
        raise EndDeviceOtaError(
            f"FINAL_BINARY_SIZE {declared_size} does not match inner bytes {len(inner)}"
        )
    text_vma = _parse_int(fields.get("TEXT_VMA", ""), "TEXT_VMA")
    if not (0 <= text_vma < BANK_A_SLOT_END):
        raise EndDeviceOtaError("target is not a logical-address-0 image")
    physical_b_end = _parse_int(
        fields.get("PHYSICAL_BANK_B_END_EXCLUSIVE", ""), "PHYSICAL_BANK_B_END_EXCLUSIVE"
    )
    if physical_b_end != BANK_B_BASE + len(inner):
        raise EndDeviceOtaError("physical bank-B end does not match the neutral inner length")
    if len(inner) >= BANK_A_SLOT_END or physical_b_end >= BANK_B_SLOT_END:
        raise EndDeviceOtaError("target reaches a reserved application-slot boundary")

    inner_sha256 = hashlib.sha256(inner).hexdigest()
    if hashes.get(inner_path.name) != inner_sha256:
        raise EndDeviceOtaError("inner SHA-256 is not bound by the build manifest")

    return {
        "bankNeutral": True,
        "logicalLinkBase": BANK_A_BASE,
        "physicalBootTargets": [BANK_A_BASE, BANK_B_BASE],
        "bankASlotEnd": BANK_A_SLOT_END,
        "bankBSlotEnd": BANK_B_SLOT_END,
        "physicalAEndExclusive": len(inner),
        "physicalBEndExclusive": physical_b_end,
        "textVma": text_vma,
        "innerBytes": len(inner),
        "innerSha256": inner_sha256,
        "innerSha512": hashlib.sha512(inner).hexdigest(),
        "buildManifestSha256": hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        "sdkCommit": EXPECTED_SDK_COMMIT,
        "architecture": {
            "endDeviceRole": True,
            "routerRole": False,
            "rxOnWhenIdle": True,
            "powerManagementEnabled": False,
            "endpoint": 11,
            "powerSource": "MAINS",
            "stackArchive": "libzb_ed.a",
            "routerArchiveLinked": False,
        },
        "hardwareContract": {
            "adcFlashSafetyPin": "PB3",
            "adcFlashSafetyEvidence": "same-model-vendor-firmware-confirmed",
            "uartTx": "PB1",
            "uartRx": "PA0",
            "pushInput": "PC2",
            "auxInput": "PB4",
        },
    }


def build_ota(inner: bytes) -> bytes:
    validate_finalized_image(
        inner,
        manufacturer_code=DEFAULT_MANUFACTURER,
        image_type=DEFAULT_IMAGE_TYPE,
        file_version=DEFAULT_FILE_VERSION,
        max_final_size=BANK_A_SLOT_END,
    )
    name = HEADER_STRING[:32].ljust(32, b"\x00")
    subelement = struct.pack("<HI", OTA_UPGRADE_TAG, len(inner)) + inner
    total = OTA_HEADER_LEN + len(subelement)
    base = struct.pack(
        "<IHHHHHIH32sI",
        OTA_MAGIC,
        OTA_HEADER_VERSION,
        OTA_HEADER_LEN,
        OTA_FIELD_HW_RANGE,
        DEFAULT_MANUFACTURER,
        DEFAULT_IMAGE_TYPE,
        DEFAULT_FILE_VERSION,
        ZIGBEE_STACK_VERSION,
        name,
        total,
    )
    result = base + struct.pack("<HH", TARGET_HW_VERSION, TARGET_HW_VERSION) + subelement
    if len(result) != total:
        raise AssertionError("internal Zigbee OTA size mismatch")
    return result


def validate_ota(data: bytes) -> dict:
    if len(data) < OTA_HEADER_LEN + 6:
        raise EndDeviceOtaError("OTA is too short")
    magic, header_ver, header_len, field_control, mfr, image, version, stack, name, total = struct.unpack_from(
        "<IHHHHHIH32sI", data, 0
    )
    if magic != OTA_MAGIC or header_ver != OTA_HEADER_VERSION or header_len != OTA_HEADER_LEN:
        raise EndDeviceOtaError("unexpected Zigbee OTA header")
    if field_control != OTA_FIELD_HW_RANGE:
        raise EndDeviceOtaError("hardware-version range is not mandatory")
    if (mfr, image, version, stack) != (
        DEFAULT_MANUFACTURER,
        DEFAULT_IMAGE_TYPE,
        DEFAULT_FILE_VERSION,
        ZIGBEE_STACK_VERSION,
    ):
        raise EndDeviceOtaError("outer Zigbee OTA identity mismatch")
    if total != len(data):
        raise EndDeviceOtaError("outer Zigbee OTA total-size mismatch")
    hw_min, hw_max = struct.unpack_from("<HH", data, 56)
    if (hw_min, hw_max) != (TARGET_HW_VERSION, TARGET_HW_VERSION):
        raise EndDeviceOtaError("OTA is not locked to hwVersion 2")
    tag, length = struct.unpack_from("<HI", data, OTA_HEADER_LEN)
    if tag != OTA_UPGRADE_TAG:
        raise EndDeviceOtaError("unexpected OTA subelement tag")
    start = OTA_HEADER_LEN + 6
    end = start + length
    if end != len(data):
        raise EndDeviceOtaError("OTA must contain exactly one upgrade-image subelement")
    inner = data[start:end]
    preamble = validate_finalized_image(
        inner,
        manufacturer_code=DEFAULT_MANUFACTURER,
        image_type=DEFAULT_IMAGE_TYPE,
        file_version=DEFAULT_FILE_VERSION,
        max_final_size=BANK_A_SLOT_END,
    )
    return {
        "manufacturerCode": mfr,
        "imageType": image,
        "fileVersion": version,
        "hardwareVersionMin": hw_min,
        "hardwareVersionMax": hw_max,
        "innerBytes": len(inner),
        "innerDeclaredBytes": preamble.declared_size,
        "headerString": name.rstrip(b"\x00").decode("ascii"),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inner", type=Path)
    parser.add_argument("--build-manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--source-head", required=True, help="exact repository head used for the build")
    parser.add_argument(
        "--offline-build-quarantined",
        action="store_true",
        help="required acknowledgement that this creates a non-authorized offline artifact",
    )
    args = parser.parse_args(argv)
    if not args.offline_build_quarantined:
        parser.error("refusing to build candidate without --offline-build-quarantined")
    if not re.fullmatch(r"[0-9a-fA-F]{40}", args.source_head):
        parser.error("--source-head must be a full 40-hex commit SHA")

    placement = validate_target_manifest(args.build_manifest, args.inner)
    ota = build_ota(args.inner.read_bytes())
    validation = validate_ota(ota)
    args.out.write_bytes(ota)
    metadata = {
        "schema": 1,
        "artifactClass": "glsd301p-end-device-canary",
        "QUARANTINED_BUILD": True,
        "DEPLOYABLE": False,
        "DO_NOT_SERVE_WITHOUT_EXPLICIT_AUTHORIZATION": True,
        "authorizationGranted": False,
        "targetIeee": TARGET_IEEE,
        "targetModel": TARGET_MODEL,
        "targetVendor": TARGET_VENDOR,
        "sourceHead": args.source_head.lower(),
        "file": args.out.name,
        "bytes": len(ota),
        "sha256": hashlib.sha256(ota).hexdigest(),
        "sha512": hashlib.sha512(ota).hexdigest(),
        **validation,
        **placement,
    }
    sidecar = args.out.with_suffix(args.out.suffix + ".quarantine.json")
    sidecar.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(metadata, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
