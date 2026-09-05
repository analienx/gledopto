# Batch 4 pre-integration evidence — 2026-09-05

Executor: read-only evidence pass at `research/wireless-dump-stager` @ `fa8869dde2cd06ab3f2b3e9314f795c99a86e442`.
No OTA served, no private-cluster send, no device mutation, no implementation changes.

## A. Baseline

```text
RESEARCH_BRANCH_HEAD = fa8869dde2cd06ab3f2b3e9314f795c99a86e442 (== PR #2 head, verified)
CI_RUNS_FOR_HEAD     = push 33973002693 SUCCESS; pull_request 33973005618 SUCCESS
                       jobs: "Python 3.14 + native GCC" and "Python 3.11 + native GCC" both success
PYTHON_VERSION       = 3.14.4 (Windows host and WSL)
OS                   = Windows 11 x64 notebook + WSL2 Ubuntu 26.04 (gcc 15.2.0, make)
Z2M_VERSION          = 2.14.0 (HAOS addon, ghcr.io/zigbee2mqtt/zigbee2mqtt-aarch64:2.14.0-1)
ZHC_VERSION          = 26.103.0
HERDSMAN_VERSION     = 10.9.1 (pnpm layout, /app/node_modules/zigbee-herdsman)
NODE_VERSION         = v24.18.1 (inside Z2M addon container)
COORDINATOR_ADAPTER  = ember, SONOFF Dongle Max MG24 (EFR32MG21), 115200
COORDINATOR_STACK    = EmberZNet 9.1.1 [GA] (ezsp 19, build 0)
```

Test re-runs at this head (all exit 0):

- Windows Python: `test_wireless_dump.py` 12 tests OK (skipped=2, GCC-only); `test_wireless_dump_guard.py` 11 tests OK.
- WSL Ubuntu gcc 15.2.0: `test_wireless_dump.py` **12 tests OK, no skips** (native stager-core + dispatcher/cross-language fixtures EXECUTED and PASSED); guard suite 11 OK.
- ASan+UBSan (`-fsanitize=address,undefined`, gcc 15.2.0): `stager_core_test: PASS`, `stager_dispatch_test: PASS`, e2e fixture INFO/DATA frames produced, **no sanitizer reports**.

## B. Z2M transport surface (installed source proof; nothing sent)

Files: `zigbee-herdsman@10.9.1/dist/controller/model/endpoint.js|d.ts`, `dist/zspec/zcl/zclFrame.js`, `z2m 2.14.0 /app/dist/extension/otaUpdate.js`.

### B1 Outbound private-cluster unicast

```text
API/SYMBOL   = Endpoint.command / Endpoint.commandResponse
MODULE/PATH  = zigbee-herdsman/dist/controller/model/endpoint.js (impl at :648/:654; d.ts :136)
SIGNATURE    = commandResponse<Cl,Co,Custom>(clusterKey: Cl, commandKey: Co,
               payload: ClusterOrRawPayload<Cl,Co,Custom>, options?: Options,
               transactionSequenceNumber?: number): Promise<void>
               command(...) -> zclCommand(..., FrameType.SPECIFIC) -> returns frame.payload
DEVICE       = Z2M zigbee.resolveEntity / herdsman controller device by IEEE
ENDPOINT     = device.getEndpoint(n) / endpoint lookup by epId
CLUSTER_ARG  = numeric or name key; resolved via this.getCluster(clusterKey, device,
               options?.manufacturerCode) incl. device.customClusters (endpoint.js:657,661)
COMMAND_ARG  = numeric or name via Zcl.Utils.getClusterCommandResponse(cluster, commandKey)
RAW_BUFFER   = yes — payload type is ClusterOrRawPayload; Zcl.Frame.create accepts raw
               payload with customClusters (endpoint.js:661)
MANUF_CODE   = options.manufacturerCode (defaults merged from cluster.manufacturerCode)
DISABLE_DEF  = options.disableDefaultResponse (endpoint.d.ts:27)
TIMEOUT      = herdsman request timeout; baseline default 150000 ms
               (controller/helpers/ota.js:372 "ensures never zero")
RETRY        = herdsman adapter queue/recovery; commandResponse asserts TSN via parameter
               (endpoint.js:655), not options
RETURN       = command(): frame.payload of matched response; commandResponse(): Promise<void>
SIDE_EFFECTS = normal APS unicast to endpoint; no state published by herdsman itself
```


### B2 Incoming private-cluster response path — PARTIAL

- herdsman forwards incoming events to device and controller listeners (`controller.js:605-606`: `device.emit(event,...); this.emit(event,...)`).
- Z2M supports external converters/extensions (`dist/extension/externalConverters.js`, `externalExtensions.js`); custom cluster definitions ride on `device.customClusters` (endpoint.js:661) and `Zcl.Frame.create(..., device.customClusters, ...)` accepts numeric/private clusters.
- Raw ZCL payload reachability (Buffer without declared fields) is implied by `ClusterOrRawPayload` but the exact incoming dispatch probe (event name, per-command match) was not completed — one small follow-up probe against `dist/controller/model/device.js` + `zspec/zcl/zclFrame.js` closes this.
- Response can be routed to a pending request without publishing to HA: an extension receives frames directly (no MQTT publish required).

Sketch (non-executed, evidence only):

```js
// Z2M external extension (evidence sketch, installed API surface)
class GlStagerExtension {
  constructor(zigbee, mqtt, state, publishEntity, eventBus) { ... }
  async start() {
    this.zigbee.on('zcl', ...)            // exact event name: follow-up probe
    // or device.customClusters registration + fromZigbee converter on cluster 0xFC00
  }
}
```

### B3 Frame budget / MTU — PROVEN by vendor precedent + spec constants

```text
NWK_MAX_PAYLOAD            = spec ~117 B (single-frame, no frag)
APS_MAX_PAYLOAD (16-bit)   = 82 B (Zigbee APS; no fragmentation)
ZCL header used            = frame control + seq + cmd id (3 B) + cluster payload
CURRENT_DATA_FRAME_MAX     = 13 (hdr) + 48 (data) + 5 (crc32+status) = 66 B
MARGIN                     = 82 - 66 = 16 B (worst case, no manufacturer frame)
APS_FRAGMENTATION          = not used
```

Strongest installed evidence: Telink's own Zigbee OTA implementation caps block payloads at `OTA_IMAGE_MAX_DATA_SIZE = 48` (`tl_zigbee_sdk/stack/zigbee/ota/ota.h:30`) — the vendor itself ships 48-byte chunks over this stack generation. Herdsman/ember per-adapter constant was not located in the installed dist (grep empty) — residual uncertainty; ember EZSP APS frame limits accommodate ≥82 B.

### B4 Timing and correlation

- TSN: auto (`zclTransactionSequenceNumber.next()`) or explicit parameter; guard binds on `(session_id, seq, offset, length)`, TSN is transport-local.
- Timeouts: herdsman baseline 150000 ms; Z2M OTA settings `image_block_request_timeout` (default 150000), `image_block_response_delay` (default 250 ms) — `dist/extension/otaUpdate.js:119-120`.
- Device-side (SDK): `OTA_MAX_IMAGE_BLOCK_RETRIES = 10`, `OTA_MAX_IMAGE_BLOCK_RSP_WAIT_TIME = 5 s` (ota.h:42,48).

## C. Live GL-SD read-only snapshot

From `/config/zigbee2mqtt/database.db` (full herdsman entity copy in `gl-sd-database-entity.json`):

```text
IEEE           = 0xa4c13850cfcdb3a4
FRIENDLY_NAME  = LivingRoomCircleLightDimmer
NWK_ADDRESS    = 17901 (0x45ED)
MODEL          = GL-SD-301P / MANUFACTURER = GLEDOPTO (manufId 4687 = 0x124F)
SW_BUILD_ID    = 20651203 / DATE_CODE = 20240704 / HW_VERSION = 2
APP_VERSION    = 1, ZCL_VERSION 3, STACK_VERSION 2
POWER_SOURCE   = "Mains (single phase)" (zcl powerSource=1)
TYPE           = Router
ENDPOINT_11    = profId 260, devId 257, inClusters [0,3,4,5,6,8,768,4096], outClusters [25]
EP242          = profId 41440 (Green Power), devId 97, outClusters [33]
CURRENT_BINDS  = genOnOff + genLevelCtrl -> 0xfdb1122d004b1200 EP1 (do not mutate)
REPORTING      = onOff 0/65000/1 ; currentLevel 5/65000/1
LAST_SEEN      = epoch-ms 1788620566159 (from database.db)
LQI            = not retained by broker (Z2M republishes retained in-process, mqtt.js:150);
                 safe standard read did not return within probe window — optional follow-up
OTA_IDENTITY   = 0x124F / 0x1416 / 0x26013001 (live-captured 2026-09-03; no new OTA check run)
```

Hashes (read-only):
- `/config/zigbee2mqtt/database.db` (184463 B) sha256 `384829665f1f3d4e9b6637ef1b70e30fed21bbaab608362e077f849406337830`
- `/config/zigbee2mqtt/configuration.yaml` sha256 `daf2b0e9b7f24ddc0fe1a7108cc10e8caf6370fe12ba8244aeac2badb6532482`
- Existing snapshots/backups: `/config/zigbee2mqtt/backup-production-20260904/`, `bseed_ota/`, `bseed_probe/`. No secrets exposed.

## D. Telink SDK/toolchain

### D1 Toolchain inventory

```text
SDK_NAME/VERSION     = telink-semi/telink_zigbee_sdk (public, master)
SDK_SOURCE/COMMIT    = 09fa2c3483b3aa2f0a6f2e2cc7e267cd6f1f9277 (pinned in
                       devices/gl-sd-301p/WIRELESS_EXTRACTION_TRANSFER.md)
TC32_COMPILER        = NOT AVAILABLE locally (tc32-elf-gcc / tc32-elf-objcopy not found)
REFERENCE_8258_TARGET= tl_zigbee_sdk/apps/zigbee/sampleLight (+ board_8258_evk_v1p2.h)
REFERENCE_LINKER     = tl_zigbee_sdk/platform/boot/8258/boot_8258.link
REFERENCE_OTA        = tl_zigbee_sdk/stack/zigbee/ota/ota.c|ota.h + apps/bootLoader/
UNCERTAINTIES        = chip_8258 low-level drivers (clock/flash-reg/watchdog/trng) are not
                       present as sources at this ref (only boot/board files) — they ship in
                       the Telink IDE toolchain/libraries; exact TC32 compiler version and
                       GL-SD board pinout remain to be sourced from the IDE package.
```

### D2 Adapter-primitive matrix

```text
ADAPTER_PRIMITIVE        | SDK_SYMBOL                          | FILE                        | CONF
flash raw read           | void flash_read(u32,u32,u8*)        | proj/drivers/drv_flash.h:22 | high
flash write (forbidden)  | flash_write/flash_writeWithCheck/flash_erase | drv_flash.h         | high
flash JEDEC ID           | NOT LOCATED in public ref           | (toolchain lib?)            | missing
random/session id        | NOT LOCATED (no trng at ref for 8258) | (toolchain lib?)          | missing
endpoint registration    | bool af_endpointRegister(u8 ep, af_simple_descriptor_t*, af_endpoint_cb_t rx_cb, af_dataCnf_cb_t cnfCb) | stack/zigbee/af/zb_af.h:475 | high
RX callback              | af_endpoint_cb_t cb_rx (zb_af.h:262) | zb_af.h                    | high
outgoing AF send         | u8 af_dataSend(u8 srcEp, epInfo_t*, u16 clusterId, u16 cmdPldLen, u8 *cmdPld, u8 *apsCnt) | zb_af.h:498 | high
watchdog                 | NOT LOCATED at ref (8258)           | (drv_pm / toolchain lib?)   | missing
reboot                   | SYSTEM_RESET()                      | ota.c:252 usage             | high
OTA bank discovery       | u32 mcuBootAddrGet(void)            | stack/zigbee/ota/ota.c:139  | high

## E. Telink OTA/boot state machine (pinned source: ota.c @ 09fa2c34)

Proven facts:
- Boot choice `mcuBootAddrGet()` (ota.c:139): read u32@0x0+8; if `TL_START_UP_FLAG_WHOLE 0x544C4E4B` boot 0x0; else read u32@0x40000+8; if flag → boot 0x40000; else invalid. **Bank A priority on ties.**
- `ota_newImageValid()` (ota.c:161): size ≤ `FLASH_OTA_IMAGE_MAX_SIZE` (from +0x18); marker check `(flag32 & 0xffffff00) == 0x544C4E00` (accepts +8 = 0x4B or 0x00); xcrc32 over `fw_size-4` bytes with byte+8 forced to `TL_IMAGE_START_FLAG 0x4B` (ota.c:35,189); compare tail.
- `ota_mcuReboot()` (ota.c:206): validate opposite bank FIRST; if invalid → **plain return, nothing written**; else `flash_writeWithCheck(newAddr+8, 1, 0x4B)`, then `flash_write(baseAddr+8, 1, 0x00)` (invalidate old), then `SYSTEM_RESET()`.
- `fileVersion` participates only in OTA-client acceptance (`ota_fileIdCmp`, ota.c:792: manuCode/imageType/fileVersion each equal-or-0xFFFF-wildcard), **not** in boot selection.
- Download: `ota_imageDataProcess` validates header magic during download (abort `ZCL_STA_INVALID_IMAGE`); progress persisted to NV (`ota_saveUpdateInfo2NV`, NV_MODULE_OTA) for crash resume.
- Old bank physically intact except the single +8 byte after a completed OTA (single-byte invalidation write).

State table:

```text
STATE                        | A+8 | B+8 | EXEC | NEXT_BOOT            | POWER-LOSS RESULT
normal (booted A)            | 4B  | FF  | A    | A                    | n/a
download complete (booted A) | 4B  | 4B  | A    | B after upgrade      | NV-resumed if lost
pre-activation (booted A)    | 4B  | 4B  | A    | B (validate→invalidate) | both valid → A boots (safe)
old invalidated (booted A)   | 00  | 4B  | A→B  | B                    | B boots
bad new image (CRC fail)     | 4B  | any | A    | A (mcuReboot returns, nothing written) | A keeps running
both invalid                 | 00  | 00  | -    | 0xFFFFFFFF (invalid) | only by external corruption;
                             |     |     |      |                      | recovery = rewrite 0x4B at +8
```

Automatic rollback after invalidation: none — but invalidation only occurs after full validation; "rollback" = rewriting the single marker byte (exactly the proven reconstruction).

## F. Acceptance-probe safety (source analysis; nothing served)

```text
OUTER_ZIGBEE_OTA_HEADER_CHECKS      = magic 0x0BEEF11E + header parse + identity match
                                      (ota_fileIdCmp: manuCode/imageType/fileVersion equal-or-wildcard)
INNER_CRC_CHECKED_BEFORE_DOWNLOAD_ACCEPT = header magic during download (ota_imageDataProcess);
                                      full inner CRC NOT checked per-block
INNER_CRC_CHECKED_BEFORE_UPGRADE_END_SUCCESS = no — full inner CRC checked at activation
                                      (ota_mcuReboot → ota_newImageValid)
BOOTLOADER_CHECK_AFTER_REBOOT       = mcuBootAddrGet marker-only (full 0x544C4E4B)
BAD_NEW_BANK_FALLBACK               = none needed pre-invalidation: ota_mcuReboot returns
                                      without writing when validation fails; device continues
                                      on old bank
OLD_BANK_MARKER_STATE_IF_BAD_NEW    = unchanged (0x4B) — old bank remains bootable
POWER_CYCLE_RECOVERY                = NV-resumed download (NV_MODULE_OTA); boot selection

## G. Z2M OTA provider path (installed 2.14.0; source only)

- Custom OTA index: `settings.ota.zigbee_ota_override_index_location` (`dist/extension/otaUpdate.js:80`, `settings.schema.json:371`, `types/api.d.ts:164`) → local/custom OTA index supported.
- Control plane: MQTT bridge topics `bridge/request/device/ota_update/(update|check|schedule|unschedule)/(downgrade|abort)` (otaUpdate.js:73).
- Per-device override: `device.definition.ota` or `device.zh.scheduledOta?.url` (otaUpdate.js:111) — definition-scoped OTA source; combined with a target-locked external converter this gates by device identity, and an extension can additionally filter by IEEE before any image block is served.
- Block timing: `image_block_request_timeout` (150000 ms), `image_block_response_delay` (250 ms) defaults.
- Upgrade End generation/abort: inside herdsman OTA helper + otaUpdate extension; server can abort before Upgrade End (unschedule/abort route).
- Automatic OTA checks during experiment: do not schedule; `check` is request-driven only.
- IEEE pre-gating: not proven in stock flow — cleanest hook is a custom external converter (`definition.ota`) bound only to the GL-SD + extension-side IEEE check. `isUpdateAvailable` candidate-selection internals = residual small probe.

## H. Network/NV preservation (pinned SDK map)

```text
IEEE/MAC_STORAGE        = 0x76000
FACTORY_CONFIG_STORAGE  = 0x77000 (F_Cfg_Info)
U_Cfg_Info              = 0x78000
NETWORK_NV              = 0x34000 (NV_1) and 0x7A000 (NV_2)
APP_BANK_A              = 0x00000..0x34000
APP_BANK_B / OTA        = 0x40000..0x74000
NORMAL_OTA_WRITE_RANGE  = opposite application bank only (ota_mcuReboot writes only +8 markers)
```

- Dual-bank OTA by design never writes outside the opposite application slot → IEEE, network key, frame counters, PAN/channel, groups/binds NV survive (map proven; target flash-size gate still open: 512 KiB TLSR8258 assumption).
- Stager must reuse the stack NV init path (drv_nv / ss_ib) — a normal Zigbee application keeping network state, not factory-new commissioning.
- Binds reference clusters 6/8 on EP11; retaining EP11 with existing profileId/devId/cluster list (adding 0xFC00 only) does not disturb the binding table — removals would.
- Minimum identity to preserve: EP11 (profId 260, devId 257), Basic identity (GLEDOPTO / GL-SD-301P / swBuildId), clusters 0/3/4/5/6/8 (+768/4096 unchanged), EP242 GP optional.
- `NETWORK_NV_PRESERVATION_PROVEN = partial` (map+behavior proven from SDK; target-geometry confirmation pending).

## I. Stager audit matrix (17 properties)

```text
#  PROPERTY                          | RESULT   | EVIDENCE
1  session ID generation wired       | NO       | env.session_id supplied by integrator; no TRNG wired — supervisor action at SDK adapter
2  PING nonce freshness end-to-end   | PARTIAL  | device echoes nonce (dispatch.c:47-73 + native test); host PING client not built yet
3  INFO geometry validated pre-READ  | PASS     | core_init geometry+marker+CRC gate; dispatch requires ctx->ready
4  READ overflow/wrap safe           | PASS     | core.c:225-229 (offset < size checked before subtraction); Python range checks
5  48-byte max agrees C/Python       | PASS     | GLSD_DUMP_MAX_CHUNK=48 / MAX_CHUNK=48; cross-language fixture passes (WSL gcc + CI)

## J. Missing-input matrix for supervisor deliverables

```text
DELIVERABLE                          | HAVE_ALL_INPUTS | MISSING_FACTS | SOURCE
1 buildable Telink stager app        | no  | TC32 toolchain (version+install); chip_8258 clock/flash/watchdog/trng headers+libs; GL-SD board pinout; flash-size confirm | Telink IDE package; historical firmware extract; target/spare probe
2 private-cluster transport adapter  | ~yes| exact herdsman incoming-event name + customClusters/fromZigbee registration nuance; ember APS payload constant | one small probe of installed dist (device.js, zspec) + herdsman 10.9.1 docs
3 guarded dump CLI/controller        | yes | (nothing) — guard+host+protocol complete offline | repo @ fa8869dd
4 offline OTA wrapper for stager     | yes | (nothing) — forensics + probe exist | repo
5 target-locked Z2M provider/ext     | ~yes| isUpdateAvailable candidate-selection internals; external converter OTA override exact schema | installed z2m 2.14.0 dist (otaUpdate.js) + zhc 26.103.0
6 dry-run/synthetic E2E harness      | yes | transport mock only (design-level) | repo
7 live acceptance decision package   | no  | target MCU_EXACT / 512K flash-size confirmation; supervisor-approved acceptance criteria | target/spare evidence; F analysis above
8 recovery/rollback package          | yes | single-byte marker rewrite proven; live decision pending | E state machine + host reconstruction
```

## Supervisor summary gates (executor-verified)

```text
Z2M_PRIVATE_TRANSPORT_IMPLEMENTABLE         = yes (source-proven outbound + custom clusters; one small incoming-path probe outstanding)
MAX_48_BYTE_CHUNK_SAFE                      = yes (SDK ota.h:30 precedent + 66 B ≤ 82 B APS budget)
TELINK_STAGER_BUILDABLE_WITH_IDENTIFIED_SDK = partial (SDK pinned; TC32 toolchain + 8258 drivers missing locally)
TELINK_DUAL_BANK_BOOT_STATE_MACHINE_PROVEN  = yes (pinned ota.c source, full transition set)
INVALID_CRC_ACCEPTANCE_PROBE_FAILSAFE       = yes (SDK-lineage source evidence; target gate noted)
NETWORK_NV_PRESERVATION_PROVEN              = partial (map proven; target geometry gate)
READ_ONLY_RANGE_PROOF                       = pass (source + tests + ASan; linked-binary proof needs TC32)
SUPERVISOR_HAS_ENOUGH_INPUT_FOR_NEXT_FULL_IMPLEMENTATION = no
```

Remaining factual inputs (single list):
1. TC32 toolchain package (exact version/installer) and chip_8258 driver headers/libs (clock, flash registers, watchdog, TRNG) — from Telink IDE or prior forensics archive.
2. GL-SD board pinout / module schematic facts (power-stage gating, unchanged).
3. Target 512 KiB flash + TLSR8258-family confirmation (existing open gate).
4. Herdsman 10.9.1 incoming private-cluster event name + customClusters registration detail (one probe).
5. Ember APS max-payload constant from installed adapter (one probe) — spec budget 82 B already sufficient.
6. Z2M 2.14.0 external-converter `definition.ota` override schema detail (one probe).
7. Supervisor decision items: CMD_STATUS implement-or-remove; WDT strategy for the stager.

6  unknown commands fail closed      | PASS     | dispatch default → GLSD_DISPATCH_ERR_UNSUPPORTED
7  CMD_STATUS defined, undispatched  | NOTE     | fails closed (unsupported); supervisor to decide implement-or-remove
8  ABORT alters device state         | NO       | deliberate no-op (dispatch.c:193-200)
9  watchdog during ~200KB dump       | UNPROVEN | stager never feeds WDT; 8258 WDT config unlocated — supervisor decision
10 response buffer sizing           | PASS     | GLSD_DISPATCH_MAX_RESPONSE = 13+48+5 = 66 (exact max DATA frame)
11 packing/alignment                | PASS     | packed structs + explicit serializers; fixtures agree
12 endianness                       | PASS     | explicit u32le/put_u32le little-endian both languages
13 dynamic allocation               | NONE     | static ctx, fixed buffers
14 recursion                        | NONE     |
15 write/erase/NV reachable         | NONE     | only env->read linked; source scan + ASan tests
16 unauthenticated reboot/reset cmd | NONE     | no such command; SYSTEM_RESET unreferenced in stager
17 ASan/UBSan                       | PASS     | WSL gcc 15.2.0 -fsanitize=address,undefined: core+dispatch+e2e clean
```

                                      only on valid flags
CAN_BAD_PROBE_STRAND_DEVICE         = no per source — CRC-invalid image fails ota_newImageValid,
                                      activation refused, old bank untouched. Caveat: SDK-lineage
                                      behavior; GL-SD (softwareBuildID 20651203) targets the same
                                      lineage but MCU_EXACT remains a target gate.
```

`INVALID_CRC_ACCEPTANCE_PROBE_FAILSAFE = yes` at source level for the SDK lineage; the probe design (valid marker, deliberately invalid CRC) exercises exactly the fail-closed path.

NV read/write (network)  | nv_flashReadNew/nv_flashWriteNew (NV_MODULE_OTA, ota.c:812) | drv_nv | high
RX-on-when-idle control  | af_nodeDescRxOnWhenIdleUpdate(bool) | zb_af.h:322                 | high
```

### D3 Build attempt — BLOCKED (environment)

```text
COMPILE_REACHED = no — TC32 toolchain absent on this host; chip_8258 driver sources absent
                  from the public SDK ref (library-only in Telink IDE)
LINK_REACHED    = no
BLOCKERS        = Telink IDE TC32 toolchain; chip_8258 driver headers/libs; GL-SD board pinout
NOTE            = host-GCC compile of the transport-neutral core + tests + fixtures SUCCEEDS
                  (WSL, incl. ASan/UBSan); TC32-specific compile errors therefore unknown
```

### D4 Forbidden-memory proof

Source level: `firmware/wireless-dump-stager/*` references only `env->read` (`glsd_flash_read_fn`); zero references to `flash_write`/`flash_erase`/NV-write; no constants in 0x34000..0x3FFFF, 0x74000+, 0x76000, 0x77000, 0x78000; reads clamped to `old_base + offset < old_declared_size` (core.c:225-229); ASan core test asserts `max_read_end <= bank B + 12`.

```text
READ_ONLY_RANGE_PROOF = pass at source level (source + tests + ASan);
                        linked-binary-level proof UNPROVEN (requires D3 toolchain)
```

- Response-after-timeout: possible; guard rejects any DATA not matching the pending request, so late frames are harmless by construction.
- Recommendation: set `disableResponse`/`disableDefaultResponse` for private READ commands; use explicit TSN per request (both supported per source).

