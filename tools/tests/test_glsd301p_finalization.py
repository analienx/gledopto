#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import check_telink_ota_recovery_contract as recovery_gate  # noqa: E402
import glsd301p_final_preflight as preflight  # noqa: E402
import glsd301p_release_plan as release_plan  # noqa: E402
import make_glsd301p_ed_ota as wrapper  # noqa: E402
from telink_app_finalize import finalize_link_binary  # noqa: E402


def finalized_inner(version: int, size: int = 256) -> bytes:
    raw = bytearray([0xA5] * size)
    struct.pack_into("<I", raw, 0x02, version)
    raw[0x06:0x08] = b"\x00\x00"
    raw[0x08:0x0C] = b"\x4b\x4e\x4c\x54"
    struct.pack_into("<H", raw, 0x12, wrapper.DEFAULT_MANUFACTURER)
    struct.pack_into("<H", raw, 0x14, wrapper.DEFAULT_IMAGE_TYPE)
    struct.pack_into("<I", raw, 0x18, len(raw))
    return finalize_link_binary(
        bytes(raw),
        file_version=version,
        manufacturer_code=wrapper.DEFAULT_MANUFACTURER,
        image_type=wrapper.DEFAULT_IMAGE_TYPE,
    )


def target_manifest(path: Path, inner_path: Path) -> None:
    inner = inner_path.read_bytes()
    fields = {
        "QUARANTINED_BUILD": "YES",
        "DEPLOYABLE": "NO",
        "FIRST_FLASHABLE_CANARY_ALLOWED": "NO",
        "SDK_EXPECTED_COMMIT": wrapper.EXPECTED_SDK_COMMIT,
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
        "FINAL_BINARY_SIZE": str(len(inner)),
        "TEXT_VMA": "0x00001560",
        "PHYSICAL_BANK_B_END_EXCLUSIVE": hex(0x40000 + len(inner)),
    }
    text = "\n".join(f"{k}={v}" for k, v in fields.items())
    text += f"\n{hashlib.sha256(inner).hexdigest()}  {inner_path}\n"
    path.write_text(text, encoding="utf-8")


def generic_ota(inner: bytes, version: int) -> bytes:
    header_len = 56
    sub = struct.pack("<HI", 0x0000, len(inner)) + inner
    total = header_len + len(sub)
    header = struct.pack(
        "<IHHHHHIH32sI",
        0x0BEEF11E,
        0x0100,
        header_len,
        0,
        wrapper.DEFAULT_MANUFACTURER,
        wrapper.DEFAULT_IMAGE_TYPE,
        version,
        2,
        b"Telink OTA Sample Usage".ljust(32, b"\x00"),
        total,
    )
    return header + sub


def stock_live_tuple() -> dict:
    return {
        "ieee": wrapper.TARGET_IEEE,
        "model": wrapper.TARGET_MODEL,
        "manufacturer": wrapper.TARGET_VENDOR,
        "swBuildId": preflight.STOCK_SW_BUILD,
        "dateCode": preflight.STOCK_DATE_CODE,
        "hwVersion": 2,
        "appVersion": 1,
        "stackVersion": 2,
        "manufacturerCode": wrapper.DEFAULT_MANUFACTURER,
        "imageType": wrapper.DEFAULT_IMAGE_TYPE,
        "fileVersion": preflight.STOCK_FILE_VERSION,
    }


class FinalizationTests(unittest.TestCase):
    def _candidate(self, root: Path) -> tuple[Path, Path]:
        inner_path = root / "glsd301p-ed.final.bin"
        inner_path.write_bytes(finalized_inner(wrapper.DEFAULT_FILE_VERSION))
        manifest = root / "manifest.txt"
        target_manifest(manifest, inner_path)
        placement = wrapper.validate_target_manifest(manifest, inner_path)
        self.assertTrue(placement["bankNeutral"])
        ota_path = root / "glsd301p-ed.quarantined.ota"
        ota_path.write_bytes(wrapper.build_ota(inner_path.read_bytes()))
        validated = wrapper.validate_ota(ota_path.read_bytes())
        meta = {
            "schema": 1,
            "artifactClass": "glsd301p-end-device-canary",
            "QUARANTINED_BUILD": True,
            "DEPLOYABLE": False,
            "authorizationGranted": False,
            "targetIeee": wrapper.TARGET_IEEE,
            "targetModel": wrapper.TARGET_MODEL,
            "targetVendor": wrapper.TARGET_VENDOR,
            "sourceHead": "0" * 40,
            "file": ota_path.name,
            "bytes": ota_path.stat().st_size,
            "sha256": hashlib.sha256(ota_path.read_bytes()).hexdigest(),
            "sha512": hashlib.sha512(ota_path.read_bytes()).hexdigest(),
            **validated,
            **placement,
        }
        sidecar = root / "candidate.json"
        sidecar.write_text(json.dumps(meta), encoding="utf-8")
        return ota_path, sidecar

    def _vendor(self, root: Path) -> tuple[Path, Path, dict[str, object]]:
        inner = finalized_inner(preflight.VENDOR_RECOVERY_FILE_VERSION)
        ota = generic_ota(inner, preflight.VENDOR_RECOVERY_FILE_VERSION)
        path = root / "vendor.ota"
        path.write_bytes(ota)
        values = {
            "bytes": len(ota),
            "sha256": hashlib.sha256(ota).hexdigest(),
            "sha512": hashlib.sha512(ota).hexdigest(),
        }
        manifest = {
            "schema": 1,
            "device": wrapper.TARGET_MODEL,
            "vendor": wrapper.TARGET_VENDOR,
            "artifacts": [{
                "filename": path.name,
                "byte_identical_to_obtained_original": True,
                "size_bytes": values["bytes"],
                "sha256": values["sha256"],
                "sha512": values["sha512"],
                "project_license_applies": False,
                "zigbee_ota": {
                    "manufacturer_code": "0x124F",
                    "image_type": "0x1416",
                    "file_version": "0x28013001",
                },
            }],
        }
        manifest_path = root / "vendor-manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return path, manifest_path, values

    def test_wrapper_exact_identity_geometry_and_quarantine(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            ota, sidecar = self._candidate(root)
            meta = json.loads(sidecar.read_text())
            parsed = wrapper.validate_ota(ota.read_bytes())
            self.assertEqual(parsed["manufacturerCode"], 0x124F)
            self.assertEqual(parsed["imageType"], 0x1416)
            self.assertEqual(parsed["fileVersion"], 0x7F020001)
            self.assertEqual((parsed["hardwareVersionMin"], parsed["hardwareVersionMax"]), (2, 2))
            self.assertTrue(meta["bankNeutral"])
            self.assertEqual(meta["physicalBootTargets"], [0, 0x40000])
            self.assertFalse(meta["DEPLOYABLE"])
            self.assertFalse(meta["authorizationGranted"])

    def test_wrapper_rejects_router_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            inner = root / "glsd301p-ed.final.bin"
            inner.write_bytes(finalized_inner(wrapper.DEFAULT_FILE_VERSION))
            manifest = root / "manifest.txt"
            target_manifest(manifest, inner)
            manifest.write_text(
                manifest.read_text().replace("ROUTER_ARCHIVE_LINKED=NO", "ROUTER_ARCHIVE_LINKED=YES")
            )
            with self.assertRaises(wrapper.EndDeviceOtaError):
                wrapper.validate_target_manifest(manifest, inner)

    def test_preflight_passes_only_with_explicit_irreducible_risk_acceptance(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            candidate, sidecar = self._candidate(root)
            vendor, vendor_manifest, vv = self._vendor(root)
            recovery = root / "recovery.json"
            recovery.write_text(json.dumps({
                "pass": True,
                "firstValidCustomBootRecoveryProven": False,
                "authorizationGranted": False,
            }))
            live = root / "live.json"
            live.write_text(json.dumps(stock_live_tuple()))
            patches = {
                "VENDOR_RECOVERY_BYTES": vv["bytes"],
                "VENDOR_RECOVERY_SHA256": vv["sha256"],
                "VENDOR_RECOVERY_SHA512": vv["sha512"],
            }
            with mock.patch.multiple(preflight, **patches):
                no_risk = preflight.evaluate(
                    candidate, sidecar, vendor, vendor_manifest, recovery, live,
                    expected_source_head="0" * 40,
                    full_size_shadow_passed=True,
                    post_rejection_power_cycle_passed=True,
                    accept_first_valid_custom_boot_risk=False,
                )
                self.assertTrue(no_risk["SOFTWARE_PREFLIGHT_PASS"])
                self.assertFalse(no_risk["CANARY_DECISION_READY"])
                self.assertIn("FIRST_VALID_CUSTOM_BOOT_RISK_NOT_ACCEPTED", no_risk["blockers"])
                yes_risk = preflight.evaluate(
                    candidate, sidecar, vendor, vendor_manifest, recovery, live,
                    expected_source_head="0" * 40,
                    full_size_shadow_passed=True,
                    post_rejection_power_cycle_passed=True,
                    accept_first_valid_custom_boot_risk=True,
                )
            self.assertTrue(yes_risk["CANARY_DECISION_READY"])
            self.assertFalse(yes_risk["AUTHORIZATION_GRANTED"])
            self.assertFalse(yes_risk["directProductionGeometryProven"])

    def test_preflight_rejects_live_tuple_drift(self) -> None:
        live = stock_live_tuple()
        live["hwVersion"] = 3
        self.assertIn("LIVE_HWVERSION_MISMATCH", preflight.validate_live_tuple(live))

    def test_release_plan_is_exact_ieee_inert_and_byte_bound(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            candidate, sidecar = self._candidate(root)
            meta = json.loads(sidecar.read_text())
            pf = root / "pf.json"
            pf.write_text(json.dumps({
                "targetIeee": wrapper.TARGET_IEEE,
                "AUTHORIZATION_GRANTED": False,
                "SOFTWARE_PREFLIGHT_PASS": True,
                "CANARY_DECISION_READY": True,
                "firstValidCustomBootRiskAccepted": True,
                "candidate": {"sha256": meta["sha256"]},
                "vendorRecovery": {
                    "sameModelVendorRecoveryAvailable": True,
                    "fileVersion": preflight.VENDOR_RECOVERY_FILE_VERSION,
                    "sha256": "abc",
                },
                "postBootVendorRecoverySourceGate": {"pass": True},
                "residualRisk": "test residual risk",
            }))
            plan = release_plan.build_plan(candidate, sidecar, pf, "https://example.invalid/candidate.ota")
            self.assertEqual(plan["targetIeee"], wrapper.TARGET_IEEE)
            self.assertFalse(plan["usesGlobalOverrideIndex"])
            self.assertFalse(plan["authorizationGranted"])
            self.assertFalse(plan["executionPermitted"])
            self.assertEqual(plan["plannedUpdateRequest"]["payload"]["id"], wrapper.TARGET_IEEE)
            mutated = bytearray(candidate.read_bytes())
            mutated[-1] ^= 1
            candidate.write_bytes(mutated)
            with self.assertRaises(release_plan.ReleasePlanError):
                release_plan.build_plan(candidate, sidecar, pf, "https://example.invalid/candidate.ota")

    def test_recovery_source_contract_requires_wwah_off_and_ota_client(self) -> None:
        ota_source = """
        u8 ota_queryNextImageRspHandler(void *x, ota_queryNextImageRsp_t *pQueryNextImageRsp) {
        #if ZCL_WWAH_SUPPORT
          if (foo == ZCL_ATTRID_WWAH_DISABLE_OTA_DOWNGRADES) {
            if (pQueryNextImageRsp->fileVer < g_otaCtx.pOtaPreamble->fileVer) { return 1; }
          }
        #endif
          if (pQueryNextImageRsp->fileVer == g_otaCtx.pOtaPreamble->fileVer) { return ZCL_STA_SUCCESS; }
          return 0;
        }
        """
        app_cfg = "#define ZCL_OTA_SUPPORT 1\n#define ZCL_WWAH_SUPPORT 0\n"
        target = "ZCL_CLUSTER_OTA; ota_init(OTA_TYPE_CLIENT, x, &g_ota_info, y);"
        report = recovery_gate.analyze_contract(ota_source, app_cfg, target)
        self.assertTrue(report["pass"])
        bad = recovery_gate.analyze_contract(
            ota_source, app_cfg.replace("ZCL_WWAH_SUPPORT 0", "ZCL_WWAH_SUPPORT 1"), target
        )
        self.assertFalse(bad["pass"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
