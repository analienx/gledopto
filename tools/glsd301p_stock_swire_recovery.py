#!/usr/bin/env python3
"""Offline, stock-only GL-SD-301P recovery preparation and guarded SWire writer.

Usage:
    python tools/glsd301p_stock_swire_recovery.py prepare
    python tools/glsd301p_stock_swire_recovery.py flash --port COM3

Never accepts experimental firmware or a full-chip erase operation. In flash
mode it uses the independently maintained pvvx TLSR825xComFlasher wf command
at address ZERO; there is no live Zigbee/HA dependency or flash-read requirement.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import urllib.request

from telink_app_finalize import TelinkImageError, validate_finalized_image

REPO = Path(__file__).resolve().parents[1]
VENDOR_DIR = REPO / "devices/gl-sd-301p/vendor-firmware"
VENDOR_OTA = VENDOR_DIR / "originals/GL-SD-301P_V20851203_20251230.ota"
VENDOR_MANIFEST = VENDOR_DIR / "MANIFEST.json"
DEFAULT_OUTPUT = REPO / "local-recovery/glsd301p-stock-bank-a.bin"
EXPECTED_ORIGINAL_SHA256 = "16595a38ab9783d3afc4eb58ab4ec32625249bd569c1fa6dbaf468bddc76dd72"
EXPECTED_ORIGINAL_SIZE = 208946
EXPECTED_VENDOR_VERSION = 0x28013001
MANUFACTURER = 0x124F
IMAGE_TYPE = 0x1416
OTA_MAGIC = 0x0BEEF11E
OTA_HEADER_VERSION = 0x0100
OTA_HEADER_BASE_SIZE = 56
OTA_HW_RANGE = 4
OTA_SUBELEMENT = 0
BANK_A_START = 0x00000
BANK_A_END = 0x34000
BANK_B_START = 0x40000
NV_A_START = 0x34000
MAC_START = 0x76000
FLASH_SECTOR_SIZE = 4096

# Commit and Git blob SHA both pin the upstream third-party programming tool.
# The source is NOT copied into the Gledopto repository.
PVVX_COMMIT = "042613b1b47a1598856b0ff3e8a133f2918798a5"
PVVX_BLOB = "44b13b908c9ea2b7619edf3a78ba0843ebfda214"
PVVX_URL = (
    "https://raw.githubusercontent.com/pvvx/TlsrComSwireWriter/"
    + PVVX_COMMIT + "/TLSR825xComFlasher.py"
)


class RecoveryError(ValueError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def checked_vendor_source(ota_path: Path, manifest_path: Path) -> bytes:
    """Only the exact original supplied by GLEDOPTO is ever accepted."""
    ota = ota_path.read_bytes()
    if len(ota) != EXPECTED_ORIGINAL_SIZE or sha256(ota) != EXPECTED_ORIGINAL_SHA256:
        raise RecoveryError("vendor OTA bytes differ from the pinned original: STOP")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    records = [
        a for a in manifest.get("artifacts", [])
        if a.get("filename") == ota_path.name
    ]
    if len(records) != 1:
        raise RecoveryError("canonical original not uniquely recorded in MANIFEST.json")
    record = records[0]
    if (record.get("sha256") != EXPECTED_ORIGINAL_SHA256
            or record.get("size_bytes") != EXPECTED_ORIGINAL_SIZE
            or record.get("zigbee_ota", {}).get("manufacturer_code") != "0x124F"
            or record.get("zigbee_ota", {}).get("image_type") != "0x1416"
            or record.get("zigbee_ota", {}).get("file_version") != "0x28013001"):
        raise RecoveryError("vendor source manifest conflicts with pinned stock identity")
    return ota


def extract_stock_app(ota: bytes) -> bytes:
    """Validate Zigbee OTA envelope and Telink application; do NOT alter app bytes."""
    if len(ota) < OTA_HEADER_BASE_SIZE + 6:
        raise RecoveryError("truncated Zigbee OTA")
    try:
        (magic, header_ver, header_len, fields, maker, image_type, version,
         stack_version, _label, declared_total) = struct.unpack_from(
            "<IHHHHHIH32sI", ota, 0
        )
    except struct.error as exc:
        raise RecoveryError("malformed Zigbee OTA header") from exc
    if (magic, header_ver, maker, image_type, version) != (
        OTA_MAGIC, OTA_HEADER_VERSION, MANUFACTURER, IMAGE_TYPE,
        EXPECTED_VENDOR_VERSION
    ):
        raise RecoveryError("not the expected GL-SD-301P vendor OTA identity")
    if stack_version != 2 or declared_total != len(ota):
        raise RecoveryError("unexpected stack version or OTA length")
    # Only no optional fields, or just the standard 4-byte HW-version range.
    if (fields, header_len) not in ((0, 56), (OTA_HW_RANGE, 60)):
        raise RecoveryError("unknown OTA field-control/header combination")
    if fields == OTA_HW_RANGE:
        hw_min, hw_max = struct.unpack_from("<HH", ota, 56)
        if not (hw_min <= 2 <= hw_max):
            raise RecoveryError("vendor OTA excludes GL-SD-301P HW version 2")
    tag, declared_app_size = struct.unpack_from("<HI", ota, header_len)
    app = ota[header_len + 6:]
    if tag != OTA_SUBELEMENT or len(app) != declared_app_size:
        raise RecoveryError("expected exactly one uncompressed upgrade-image element")
    if len(app) >= BANK_A_END:
        raise RecoveryError("stock app reaches the 0x34000 network-NV boundary")
    erase_end = (len(app) + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1)
    if erase_end > BANK_A_END:
        raise RecoveryError("sector-rounded erase would enter NV")
    try:
        preamble = validate_finalized_image(
            app,
            manufacturer_code=MANUFACTURER,
            image_type=IMAGE_TYPE,
            file_version=EXPECTED_VENDOR_VERSION,
            max_final_size=BANK_A_END,
        )
    except TelinkImageError as exc:
        raise RecoveryError(f"vendor inner Telink application is not valid: {exc}") from exc
    if preamble.declared_size != len(app):
        raise RecoveryError("inner declared firmware size differs from OTA subelement")
    return app


def prepare(output: Path = DEFAULT_OUTPUT,
            ota_path: Path = VENDOR_OTA,
            manifest_path: Path = VENDOR_MANIFEST) -> dict[str, object]:
    app = extract_stock_app(checked_vendor_source(ota_path, manifest_path))
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() and output.read_bytes() != app:
        raise RecoveryError(f"refusing to overwrite different local file: {output}")
    if not output.exists():
        output.write_bytes(app)
    # Stage the complete application with invalid bank-A boot marker (0xFF).
    # Only after the complete stage has been programmed do we clear bits in
    # that one byte to 0x4B, enabling the valid TLNK boot marker atomically.
    if app[8:12] != b"KNLT":
        raise RecoveryError("unexpected stock startup marker")
    staged = bytearray(app)
    staged[8] = 0xFF
    stage_path = output.with_name(output.stem + "-stage.bin")
    activate_path = output.with_name(output.stem + "-activate.bin")
    for file_path, payload in ((stage_path, bytes(staged)), (activate_path, b"K")):
        if file_path.exists() and file_path.read_bytes() != payload:
            raise RecoveryError(f"refusing to overwrite different local file: {file_path}")
        if not file_path.exists():
            file_path.write_bytes(payload)
    erase_end = (len(app) + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1)
    report = {
        "device": "GL-SD-301P",
        "source": VENDOR_OTA.name,
        "sourceSha256": EXPECTED_ORIGINAL_SHA256,
        "fileVersion": f"0x{EXPECTED_VENDOR_VERSION:08x}",
        "applicationBytes": len(app),
        "applicationSha256": sha256(app),
        "writeStart": "0x00000",
        "eraseEndExclusive": f"0x{erase_end:05x}",
        "bankAEndExclusive": "0x34000",
        "firstNvStart": "0x34000",
        "bankBStart": "0x40000",
        "factoryMacStart": "0x76000",
        "readbackVerified": False,
        "stockZigbeeInterviewVerified": False,
        "output": str(output),
        "stagedOutput": str(stage_path),
        "stagedSha256": sha256(bytes(staged)),
        "stagedBootMarker": "ff4e4c54",
        "activationOutput": str(activate_path),
        "activationAddress": "0x00008",
        "activationByte": "4b",
        "writeAlgorithm": "two-phase: wf bank A with inactive marker; we 0x00008 one byte",
    }
    output.with_suffix(".json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return report


def upstream_blob_sha1(source: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(source)).encode() + b"\0" + source).hexdigest()


def get_programmer(explicit_path: Path | None, cache_dir: Path) -> Path:
    if explicit_path is not None:
        program = explicit_path
        if not program.is_file():
            raise RecoveryError("programmer script not found: " + str(program))
        data = program.read_bytes()
    else:
        program = cache_dir / f"TLSR825xComFlasher-{PVVX_COMMIT}.py"
        if program.exists():
            data = program.read_bytes()
        else:
            # This download occurs only on an explicit flash attempt.
            try:
                with urllib.request.urlopen(PVVX_URL, timeout=25) as response:
                    data = response.read(100_000)
            except Exception as exc:
                raise RecoveryError(
                    "cannot download pinned SWire flasher; supply --programmer PATH"
                ) from exc
            if len(data) >= 100_000:
                raise RecoveryError("downloaded flasher unexpectedly large")
    if upstream_blob_sha1(data) != PVVX_BLOB:
        raise RecoveryError("pvvx flasher does not match the pinned Git blob SHA")
    if explicit_path is None and not program.exists():
        cache_dir.mkdir(parents=True, exist_ok=True)
        program.write_bytes(data)
    return program


def flash(*, port: str, output: Path, programmer: Path | None,
          activate_ms: int, confirmation: str | None) -> None:
    if not port or "\x00" in port or not re.fullmatch(r"[A-Za-z0-9_./:\\-]+", port):
        raise RecoveryError("invalid COM/serial port")
    if not 0 <= activate_ms <= 1000:
        raise RecoveryError("--activate-ms must be 0..1000")
    report = prepare(output=output)
    print(json.dumps(report, indent=2, sort_keys=True))
    print(
        "\nWRITE-ONLY SWire RECOVERY — MAINS DISCONNECTED.\n"
        "Verify power isolation, 3.3V logic/power, common GND, exact SWS pad,\n"
        "and safe programmer reset/RTS wiring BEFORE connecting USB.\n"
        "DO NOT connect the USB programmer while the dimmer is on 230 V.\n"
        "No full-chip erase. Only stock app bank A is sector-written.\n"
        "PHASE 1 writes bank A with INVALID marker 0xFF; PHASE 2 changes\n"
        "ONLY the startup marker at 0x00008 from FF to 4B (no erase).\n"
        "A failed PHASE 1 must NEVER be followed by PHASE 2.\n"
        "This cannot verify the write by readback. It requires live Zigbee\n"
        "interview after fully disconnecting programmer and safe reassembly.\n"
    )
    if confirmation != "WRITE_STOCK_BANK_A":
        if not sys.stdin.isatty():
            raise RecoveryError(
                "noninteractive flash requires --confirm WRITE_STOCK_BANK_A"
            )
        reply = input("Type WRITE_STOCK_BANK_A to permit the write: ").strip()
        if reply != "WRITE_STOCK_BANK_A":
            raise RecoveryError("write cancelled")
    try:
        import serial  # noqa: F401
    except ImportError as exc:
        raise RecoveryError("pyserial missing: python -m pip install pyserial") from exc
    program = get_programmer(programmer, REPO / "local-recovery/tools")
    # Two-phase activation: flash *invalid* bank A first, then program only
    # the 1->0 transition FF -> 4B at offset 8, WITHOUT sector erase.
    # Even if the initial write is interrupted, SDK boot selection keeps
    # preferring the existing valid bank B rather than a truncated bank A.
    base_command = [
        sys.executable, str(program), "-p", port, "-t", str(activate_ms),
    ]
    stages = (
        ("STAGE", ["wf", "0x00000", report["stagedOutput"]]),
        ("ACTIVATE", ["we", "0x00008", report["activationOutput"]]),
    )
    for step, tail in stages:
        command = base_command + tail
        print(f"PHASE {step}:", subprocess.list2cmdline(command), flush=True)
        completed = subprocess.run(command, check=False)
        if completed.returncode:
            raise RecoveryError(
                f"PHASE {step} failed (exit {completed.returncode}); "
                "do NOT declare recovery or attempt manual marker changes"
            )
    print(
        "TWO_PHASE_PROGRAMMER_REPORTED_SUCCESS; READBACK_UNVERIFIED.\n"
        "Disconnect USB/3.3V, reassemble safely, then validate stock Zigbee\n"
        "join + complete interview + OnOff/Level before declaring recovery."
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="action", required=True)
    prep = subs.add_parser("prepare", help="validate original and emit bank-A binary; NO hardware")
    prep.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    fl = subs.add_parser("flash", help="explicitly write stock bank A, never network NV")
    fl.add_argument("--port", required=True, help="COM3, /dev/ttyUSB0, etc")
    fl.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    fl.add_argument("--programmer", type=Path, help="optional pinned TLSR825xComFlasher.py")
    fl.add_argument("--activate-ms", type=int, default=0, help="RTS activation only if wiring qualified")
    fl.add_argument("--confirm", help="for attended executor: WRITE_STOCK_BANK_A")
    a = parser.parse_args(argv)
    try:
        if a.action == "prepare":
            print(json.dumps(prepare(output=a.output), indent=2, sort_keys=True))
        else:
            flash(
                port=a.port,
                output=a.output,
                programmer=a.programmer,
                activate_ms=a.activate_ms,
                confirmation=a.confirm,
            )
    except (RecoveryError, OSError, json.JSONDecodeError, struct.error) as exc:
        print(f"RECOVERY_BLOCKED: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
