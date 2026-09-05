# Batch 5 target-build evidence — 2026-09-05

Tested head: `research/wireless-dump-stager` @ `e3549c84d02cbb7c2149d3bd0c410798855bb498` (== PR #2 head).
No OTA served, no production extension loaded, no private 0xFC00 traffic, no device mutation.

## A. Head verification + local test matrix (all exit 0)

- CI run 33984581451: SUCCESS (jobs "Python 3.14 + native GCC + Z2M contract", "Python 3.11 + native GCC + Z2M contract").
- Windows Python 3.14.4: test_wireless_dump 12 OK (2 GCC skips); test_wireless_dump_guard 11 OK;
  test_transport_adapter 1 OK (1 skip); test_z2m_dump_integration 3 OK; test_glsd_stager_index 3 OK.
- node integrations/zigbee2mqtt/test_glsd_wireless_dump_contract.mjs → `glsd_wireless_dump_contract: PASS` (node v24.18.0).
- `node --check integrations/zigbee2mqtt/glsd_wireless_dump_extension.mjs` → OK.
- WSL Ubuntu gcc 15.2.0, python3 3.14.4: all five suites OK with **no skips** (native core/dispatcher/transport tests executed): 12 OK / 11 OK / 1 OK / 3 OK / 3 OK.
- Native `-Wall -Wextra -Werror`: stager_core_test PASS, stager_dispatch_test PASS, e2e fixture INFO/DATA emitted.
- ASan+UBSan (gcc 15.2.0): all three binaries clean.

## B. TC32/TLSR8258 build environment — ACQUIRED with provenance

```text
TOOLCHAIN_URL    = https://shyboy.oss-cn-shenzhen.aliyuncs.com/readonly/tc32_gcc_v2.0.tar.bz2
TOOLCHAIN_SHA256 = 33b854be3e3db3dba4b4dacdda2cd4ea1c94dfd4d562864a095956de7991b430  (MATCHED; pinned by
                   romasku/tuya-zigbee-switch src/telink/tools.mk; origin = official Telink OSS bucket)
COMPILER         = tc32-elf-gcc (Telink TC32 version 2.0 build) 4.5.1.tc32-elf-1.5, Linux x86-64
SDK_URL          = https://github.com/telink-semi/telink_zigbee_sdk/archive/refs/tags/V3.7.2.0.zip
SDK_SHA256       = 77ca35173fc9d6c4fc8a6f6a97bd601d66c7a607a5c89e3012c138736ae0c049
SDK_VERSION_ID   = v3.7.2.0 (zigbee/common/includes/zb_version.h)
KEY SDK CONTENT  = platform/chip_8258 (flash.c + flash/flash_common.c + flash_mid1460c8.c etc.),
                   proj/tl_common.h, zigbee/zbapi/zb_api.h, zigbee/zcl/zcl_include.h,
                   zigbee/lib/tc32/{libzb_router,libzb_ed,libzb_coordinator}.a,
                   platform/lib/libdrivers_8258.a, platform/boot/8258/{boot_8258.link,cstartup_8258.S}
NOTE             = master @ 09fa2c34 has NO 8258 build target/chip sources; tag V3.7.2.0 does.
                   flash_mid1460c8.c == JEDEC 0x1460C8, matching our stager fixture/probe JEDEC.
```

## B1. Upstream sampleLight build (closest supported target) — PARTIAL

- 79+ objects compile with TC32 4.5.1: full SDK source set (platform/chip_8258, proj, zigbee stack incl. ota/gp/wwah/ss) + `apps/sampleLight` app sources. Required flags: `-DMCU_CORE_8258=1 -DROUTER=1 -DMCU_STARTUP_8258=1 -fshort-enums -fpack-struct(SDK objects) -D_SIZE_T -D_SIZE_T_ -D__SIZE_T -D__SIZE_T__` (size_t-conflict guard).
- Link with `platform/boot/8258/boot_8258.link` requires build-config defsyms: `__BOOT_LOADER_IMAGE` (0/1 bank-A/B origin selector), `__FW_OFFSET` (0x00000 / 0x40000), `__FW_RAMCODE_SIZE_MAX`. After providing defsyms, link reached real symbol resolution.
- Remaining unresolved at stop: `user_init` (present in apps/sampleLight/sampleLight.c but not exported by our compiled object — guarded definition; the SDK's own 8258 Eclipse project config supplies the correct build config), `ll_get_encrypted_network_key` (ZLL/stack closure; requires the SDK's own project file list/config).
- FIRST ACTIONABLE FAILURE (B1): the V3.7.2.0 tag contains no 8258 Eclipse project (`build/tlsr_tc32` has no 8258 sampleLight config); the exact project config (defines + defsyms + file list) ships in the legacy 8258 Telink IDE SDK package. Removing `TC32_TARGET_BUILD=BLOCKED` needs that project file or its defines.

## C. Supervisor target-facing sources — object compile with -DGLSD_TELINK_SDK

```text
glsd_stager_core.o        3184 B  sha256 f79f9c5ccdd7cbcf944f7596321387f6a7ba59c02a296a35077f83ba7daf4706  PASS
glsd_stager_dispatch.o    2232 B  sha256 08067469c4307abc51a6fb5b7814a7b4ab5fa0ed1e9b94e34520e03bd8c72a29  PASS
glsd_transport_adapter.o  1148 B  sha256 3964e00eb2eddb9df221bae1422018aa6763728f2369cd0e99fa3cdeb55ba8b1  PASS
glsd_telink_sdk_adapter.o FAIL    — first error: proj/tl_common.h:28 requires app-provided app_cfg.h;
                                  then proj/common/types.h:161 size_t conflicts with compiler stddef.h
```

Exact fix path (supervisor-owned; executor changed no code): in the target TU include `tl_common.h` before the `glsd_*` headers and compile with `-D_SIZE_T -D_SIZE_T_ -D__SIZE_T -D__SIZE_T__` (romasku's documented workaround, `telink_size_t_hack.h`), plus supply an app_cfg.h (application layer). The apparent `glsd_telink_send_response` vs `glsd_transport_send_fn` mismatch appears only under the broken size_t state — verify it disappears on recompile after the fix.

Command recorded: `tc32-elf-gcc -O2 -ffunction-sections -fdata-sections -Wall -std=gnu99 -funsigned-char -fshort-wchar -fms-extensions -DMCU_CORE_8258=1 -DMCU_STARTUP_8258=1 -DGLSD_TELINK_SDK -I<stager> -I$SDK/platform -I$SDK/platform/chip_8258 -I$SDK/proj -I$SDK/proj/common -I$SDK/zigbee -I$SDK/zigbee/af -I$SDK/zigbee/include -I$SDK/zigbee/bdb/includes -I$SDK/zigbee/common/includes -I$SDK/zigbee/ota -I$SDK/zigbee/zbapi -I$SDK/zigbee/zcl -I$SDK/zigbee/zdo -c <file>`

## D. Target-project ingredient pins (SDK V3.7.2.0)

```text
D1 build identity : -DMCU_CORE_8258=1 -DMCU_STARTUP_8258=1 (+ -DROUTER=1 / -DEND_DEVICE=1 -DPM_ENABLE);
                    linker = platform/boot/8258/boot_8258.link + cstartup_8258.S;
                    bank-A/B build = defsyms __BOOT_LOADER_IMAGE(0/1), __FW_OFFSET(0/0x40000),
                    __FW_RAMCODE_SIZE_MAX; image header fields via ota.h
                    (TL_START_UP_FLAG_WHOLE 0x544C4E4B, OTA_IMAGE_MAX_DATA_SIZE 48);
                    JEDEC/flash-size APIs: platform/chip_8258/flash.c + flash_mid1460c8.c (0x1460C8)
D2 role/NV/WDT    : RX-on-when-idle: MAC_CAP_RX_ON_WHEN_IDLE (zb_af.h:68) +
                    af_nodeDescRxOnWhenIdleUpdate(bool) (zb_af.h:322);
                    NV restore: drv_nv (nv_flashReadNew/WriteNew) + NV_MODULE_OTA persistence
                    (ota.c ota_saveUpdateInfo2NV / ota_clientInfoRecover);
                    watchdog: not in public ref for 8258 (toolchain lib / drv_pm) — OPEN
D3 endpoint/desc  : af_endpointRegister(ep, af_simple_descriptor_t*, rx_cb, cnf_cb) (zb_af.h:475);
                    registration inventory reference: apps/sampleLight/sampleLightEpCfg.c;
                    ZCL_CLUSTER_NUM_MAX consumption via zcl_register slots (zigbee/zcl/zcl.h)
D4 OTA client     = recovery channel: zigbee/zcl/ota_upgrading/zcl_ota.c(+attr) + zigbee/ota/ota.c
                    call graph: ota_clientInit(ota_regClientCb, otaPreamble) -> queryNextImageReq
                    (ota_fileIdCmp identity gate) -> imageBlockReq (48 B blocks, retries 10, wait 5s)
                    -> ota_imageDataProcess (magic check, NV-resume) -> UpgradeEnd -> ota_mcuReboot
                    (validate opposite bank; activate only if valid).
                    Simple-descriptor requirement: outClusterList must contain cluster 25 (OTA client)
                    — live GL-SD EP11 already has out [25].

## E. Return-path research

```text
E1 stock hunt   = GitHub code search: 10 hits for GL-SD-301P — all docs/fixtures
                  (Koenkk/zigbee2mqtt.io, zigpy/zha test fixture gledopto-gl-sd-301p-0x26013001.json,
                  herdsman-converters gledopto.ts; PRs #9034/#9039). "20651203"+gledopto: 0 hits.
                  Koenkk/zigbee-OTA index (555 KB): 13 Gledopto images (GL-B-007P 0x1422,
                  GL-D-004P 0x1452, GL-D-006P 0x1454, GL-D-007P 0x1423/0x1455, GL-S-006P 0x1434,
                  plus legacy imageType-0 entries) — NONE with imageType 5142 (0x1416),
                  no GL-SD-301P. Classification: EXACT_TARGET_STOCK = NOT_FOUND;
                  SAME_0x1416_LINEAGE public binary = NOT_FOUND (0x1422/0x1452/0x1454/0x1455/0x1434
                  siblings exist, proving the 0x14xx dimmer-family imageType scheme).
E2 reconstructed-stock contract = client "current" fileVersion is loaded from the INNER Telink
                  preamble (ota.c: zcl_attr_currFileVer = otaPreamble->fileVer); acceptance compares
                  the outer OTA fileVersion against it via ota_fileIdCmp (equal-or-wildcard; the
                  strict greater-than check lives in zcl_ota.c query handling — one read away).
                  Activation (ota_mcuReboot → ota_newImageValid) checks inner marker+size+xcrc32
                  ONLY — no version checks at activation. Therefore: a reconstructed stock
                  application can have its inner fileVersion at +0x02 raised and xcrc32 recomputed
                  while remaining otherwise byte-identical; the OTA wrapper must present an outer
                  fileVersion satisfying zcl_ota.c's comparison vs the stager's version. Standard
                  OTA then writes the inactive bank and performs the ordinary verified switch.
E3 failure states = per ota.c: bad new image at activation → mcuReboot returns, old bank intact;
                  power loss before old-invalidation → both banks valid → bank A priority;
                  after invalidation → new bank boots. NOR physics: 0x4B→0x00 is bit-clearing
                  (single write OK, as ota_mcuReboot does); 0x00→0x4B requires sector erase —
                  marker-only rollback is NOT physically possible on NOR (confirms supervisor note);
                  the host-side reconstruction avoids this entirely (byte fixed off-device).
```

## F. Production revision evidence

```text
PRODUCTION_MCU_EXACT           = UNKNOWN (no public teardown of the 2024 GL-SD-301P revision found)
PRODUCTION_FLASH_PART_OR_JEDEC = UNKNOWN (SDK flash_mid1460c8.c is consistent with the JEDEC seen in
                                 our own live probes, but that is not public proof)
PRODUCTION_FLASH_SIZE          = UNKNOWN (512 KiB model assumed per SDK map; target gate open)
PRODUCTION_MODULE_MARKING      = UNKNOWN
PRODUCTION_PCB_REVISION        = UNKNOWN (hwVersion 2 from live Basic cluster is the only signal)
POWER_STAGE_CONTROL            = UNKNOWN (no public schematic)
BEST_SPARE_MATCH_CRITERIA      = GL-SD-301P with seller photos showing (a) label with dateCode
                                 20240704 / V2 or hwVersion marking, (b) module PCB marking,
                                 (c) housing revision matching the installed unit's photos;
                                 confirm hwVersion==2 and swBuildId 20651203 by pairing before use.
```

## G. Isolated Z2M integration verification

Supervisor-provided isolated tests (never touching the production instance) all pass: `test_z2m_dump_integration.py` (3 tests), `node test_glsd_wireless_dump_contract.mjs` (PASS), `node --check` on the extension (OK). These prove the MQTT bridge contract (only PING/INFO/READ/ABORT accepted; wrong-target rejection; IEEE/EP11 resolution via the extension path). An independent 9-point mock harness was not separately built — the supervisor's tests are the evidence of record; per-check mapping is a small residual.

## H. Forbidden-surface audit (head e3549c84)

`firmware/wireless-dump-stager/*`: zero executable references to flash_write/flash_erase/NV-write/reset/leave-network/bind/group/factory-read/MAC-read — all matches are documentation lines (PROTOCOL.md, TELINK_SDK_ADAPTER.md). The supervisor's new files (2,925 insertions across 25 files: transport adapter, SDK adapter, Z2M extension/contract, guarded runner, dump runner, stager-index builder + tests) introduce no flash mutation path; the extraction core remains env->read-only; the OTA-index builder writes only local JSON evidence files.

```text
READ_ONLY_RANGE_PROOF (updated) = pass at source level for the full current branch
                                  (core+dispatch+transport+adapter);
                                  linked-binary proof = partial (core/dispatch/transport objects
                                  compiled with TC32 4.5.1 — see C; full link pending B1)
```

```

