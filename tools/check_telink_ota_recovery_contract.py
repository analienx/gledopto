#!/usr/bin/env python3
"""Fail-closed source gate for post-boot GL-SD-301P vendor OTA recovery.

This proves only the *post-boot* recovery contract of the pinned Telink OTA
client used by the custom End Device. It cannot make a first-valid-custom-boot
failure recoverable over Zigbee if the custom image never becomes reachable.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess

EXPECTED_SDK_COMMIT = "d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e"
CUSTOM_FILE_VERSION = 0x7F020001
VENDOR_RECOVERY_FILE_VERSION = 0x28013001


class RecoveryContractError(ValueError):
    pass


def _define_value(text: str, name: str) -> str | None:
    match = re.search(rf"^\s*#define\s+{re.escape(name)}\s+([^\s/]+)", text, re.MULTILINE)
    return match.group(1) if match else None


def analyze_contract(
    ota_source: str,
    app_cfg: str,
    target_source: str,
    zcl_config: str | None = None,
) -> dict:
    errors: list[str] = []
    if _define_value(app_cfg, "ZCL_OTA_SUPPORT") != "1":
        errors.append("ZCL_OTA_SUPPORT must remain 1")
    if _define_value(app_cfg, "ZCL_WWAH_SUPPORT") != "0":
        errors.append("ZCL_WWAH_SUPPORT must remain 0 for the tested downgrade contract")
    if "ZCL_CLUSTER_OTA" not in target_source:
        errors.append("OTA client cluster is missing from the endpoint descriptor")
    if "ota_init(" not in target_source or "g_ota_info" not in target_source:
        errors.append("target does not initialize the standard Telink OTA client with its preamble")

    fn_start = ota_source.find("ota_queryNextImageRspHandler")
    if fn_start < 0:
        errors.append("pinned ota_queryNextImageRspHandler not found")
        handler = ""
    else:
        handler = ota_source[fn_start:fn_start + 9000]

    downgrade_expr = "pQueryNextImageRsp->fileVer < g_otaCtx.pOtaPreamble->fileVer"
    equal_expr = "pQueryNextImageRsp->fileVer == g_otaCtx.pOtaPreamble->fileVer"
    ww_attr = "ZCL_ATTRID_WWAH_DISABLE_OTA_DOWNGRADES"
    if downgrade_expr not in handler:
        errors.append("expected candidate-lower-than-current comparison not found")
    if equal_expr not in handler:
        errors.append("equal-version early-return contract not found")
    if ww_attr not in handler:
        errors.append("WWAH downgrade-disable attribute gate not found")

    # V3.7.2.0 compiles the lower-version rejection only under ZCL_WWAH.
    # zcl_config.h, in turn, defines ZCL_WWAH only when ZCL_WWAH_SUPPORT is
    # nonzero. The target pins ZCL_WWAH_SUPPORT to 0, so a lower same-identity
    # vendor image is not unconditionally rejected by file-version ordering.
    wwah_block_confined = False
    if downgrade_expr in handler:
        pos = handler.find(downgrade_expr)
        before = handler[max(0, pos - 1800):pos]
        after = handler[pos:pos + 1800]
        wwah_block_confined = "#ifdef ZCL_WWAH" in before and "#endif" in after
        if not wwah_block_confined:
            errors.append("lower-version rejection is not demonstrably confined to #ifdef ZCL_WWAH")

    zcl_macro_mapping = None
    if zcl_config is not None:
        compact = re.sub(r"\s+", " ", zcl_config)
        zcl_macro_mapping = bool(
            re.search(
                r"#if\s+ZCL_WWAH_SUPPORT\s+#define\s+ZCL_WWAH\b",
                compact,
            )
        )
        if not zcl_macro_mapping:
            errors.append("zcl_config.h no longer maps ZCL_WWAH_SUPPORT to ZCL_WWAH as expected")

    if VENDOR_RECOVERY_FILE_VERSION >= CUSTOM_FILE_VERSION:
        errors.append("test constants no longer exercise an actual downgrade")

    return {
        "schema": 1,
        "pass": not errors,
        "errors": errors,
        "customFileVersion": CUSTOM_FILE_VERSION,
        "vendorRecoveryFileVersion": VENDOR_RECOVERY_FILE_VERSION,
        "vendorRecoveryIsDowngrade": VENDOR_RECOVERY_FILE_VERSION < CUSTOM_FILE_VERSION,
        "zclOtaSupport": _define_value(app_cfg, "ZCL_OTA_SUPPORT"),
        "zclWwahSupport": _define_value(app_cfg, "ZCL_WWAH_SUPPORT"),
        "zclWwahMacroMappingProven": zcl_macro_mapping,
        "otaClientClusterPresent": "ZCL_CLUSTER_OTA" in target_source,
        "otaClientInitialized": "ota_init(" in target_source and "g_ota_info" in target_source,
        "downgradeRejectionWwahConditional": wwah_block_confined,
        "authorizationGranted": False,
        "scope": "POST_BOOT_VENDOR_RECOVERY_ONLY",
        "firstValidCustomBootRecoveryProven": False,
        "residualRisk": (
            "If the first valid custom image fails before Zigbee/OTA becomes reachable, "
            "wireless vendor-firmware recovery is unavailable."
        ),
    }


def git_head(path: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=path, text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        raise RecoveryContractError("cannot establish pinned Telink SDK git commit") from exc


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--app-cfg", type=Path, required=True)
    parser.add_argument("--target-source", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    actual_commit = git_head(args.sdk_root)
    if actual_commit != EXPECTED_SDK_COMMIT:
        raise SystemExit(
            f"ERROR: Telink SDK commit {actual_commit} != pinned {EXPECTED_SDK_COMMIT}"
        )
    ota_path = args.sdk_root / "zigbee/ota/ota.c"
    zcl_config_path = args.sdk_root / "zigbee/zcl/zcl_config.h"
    report = analyze_contract(
        ota_path.read_text(encoding="utf-8", errors="replace"),
        args.app_cfg.read_text(encoding="utf-8"),
        args.target_source.read_text(encoding="utf-8"),
        zcl_config_path.read_text(encoding="utf-8", errors="replace"),
    )
    report["sdkCommit"] = actual_commit
    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    if not report["pass"]:
        return 2
    print("GLSD301P_POST_BOOT_VENDOR_RECOVERY_SOURCE_GATE=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
