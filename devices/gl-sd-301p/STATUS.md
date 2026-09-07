# STATUS — gl-sd-301p

## 2026-09-06 — Supervisor TC32 build/link/finalization milestone

The host-side extraction architecture is no longer waiting on a hypothetical
Telink target build. The supervisor moved the official TC32 toolchain and public
TLSR8258 SDK into GitHub Actions and iterated against the real compiler/linker.

Current proven offline state:

```text
HOST / Z2M DUMP STACK             = IMPLEMENTED / VERIFIED
CRASH-SAFE SESSION GUARD          = PASS
REAL TC32 TOOLCHAIN               = PASS
TC32 TARGET OBJECTS               = PASS 6/6
TLSR8258 BANK-A LINK              = PASS
TLSR8258 BANK-B LINK              = PASS
PRIVATE EXTRACTION MUTATION SCAN  = NONE
FINAL OTP SYMBOL SCAN             = NONE
POWER-STAGE/RESET/STEERING SCAN   = NONE
TELINK INNER-IMAGE FINALIZER      = IMPLEMENTED / TESTED
OTA RECOVERY MODE                 = IMAGE-NOTIFY-DRIVEN ONLY
PRODUCTION REVISION PROOF         = OPEN
RETURN-TO-STOCK SPARE TEST        = OPEN
LIVE_CUSTOM_OTA                   = NO_GO
PRODUCTION_DEVICE_MUTATION        = NO_GO
```

Important implementation facts:

- official Telink TC32 v2.0 compiler archive is SHA-256 pinned;
- public SDK tag `V3.7.2.0` supplies `cstartup_8258.S`, `link_cfg.S`,
  `boot_8258.link`, Zigbee router library and TLSR8258 platform code;
- minimal stager application compiles as a real router/EP11 Telink application;
- bank A is linked with `__FW_OFFSET=0x00000`; bank B with `0x40000`;
- the link proof requires both banks and compares `.text` VMAs so bank B must
  actually be offset-linked by exactly `0x40000`;
- finalized physical image size is constrained to each `<0x34000` application
  slot; bank B must stay below `0x74000`, before MAC `0x76000` and factory
  `0x77000` regions;
- the raw TC32 binary has `00 00` at +0x06; the new offline finalizer performs
  the real lineage post-link transition to `5D 02`, patches declared size and
  appends Telink xcrc32;
- no Zigbee OTA container is produced by the TC32 mechanics job;
- extraction commands remain read-only; whole-ELF flash writes exist only in
  the separate standard Telink OTA recovery subsystem;
- periodic `ota_queryStart()` was removed. The public Telink Image Notify
  handler directly issues Query Next Image, so recovery remains available when
  explicitly initiated without automatic OTA-server discovery/polling.

The previous real dual-bank link before geometry/finalizer hardening produced a
157,440-byte raw payload in each bank with no surviving OTP or application
power-stage/reset/steering symbols. Current CI additionally validates raw-to-final
Telink image mechanics and reserved-region geometry.

Remaining live blockers are now deliberately narrow:

1. exact production 2024/2026 GL-SD-301P MCU/package/flash/PCB revision evidence;
2. production confirmation of the 512-KiB dual-bank assumptions and which bank
   the first stock-to-stager OTA would populate;
3. a sacrificial matching spare to prove the recovery/reconstructed-stock path;
4. final target-locked OTA-provider review and explicit live authorization.

No private `0xFC00` traffic, OTA image, reset, binding/group/reporting change or
other production-device mutation has been performed.

## 2026-09-05 — Batch 4 + supervisor host/integration implementation

- Executor Batch 4 large evidence pass accepted as **PARTIAL / high-value**:
  exact installed stack pinned at Z2M 2.14.0, ZHC 26.103.0, herdsman 10.9.1;
  Telink SDK source pinned; 8258 dual-bank boot/CRC/marker semantics traced;
  NV/application regions separated; native C tests + ASan/UBSan clean.
- Supervisor independently closed the remaining host API questions from exact
  upstream tags:
  - herdsman private-cluster traffic uses normal controller `message` events;
  - Z2M exposes these as `eventBus.onDeviceMessage` with rawData/TSN metadata;
  - `Endpoint.command()` supports custom-cluster request/response waiters;
  - ZHC `definition.ota` is boolean/metadata, **not** a custom IEEE-lock function.
- Stronger host/integration stack implemented on `research/wireless-dump-stager`:
  - exact-target Z2M external extension for IEEE `0xa4c13850cfcdb3a4`, EP11,
    cluster `0xFC00`;
  - bridge RPC exposes only PING/INFO/READ/ABORT;
  - guarded MQTT dump CLI with fresh PING+INFO, strict retry sequence rotation,
    crash-safe resume, chunk journal validation and final Telink CRC gate;
  - fail-closed OTA override-index builder locked to `0x124F/0x1416`,
    `GL-SD-301P`, GLEDOPTO, hwVersion 2 and version > `0x26013001`;
  - intentionally bad-CRC acceptance probe is rejected by normal stager-index
    generation.
- Expanded offline CI passes Python 3.11 + 3.14, Node contract/syntax, native
  GCC/cross-language tests, synthetic dropped-response end-to-end dump and OTA
  index rejection/acceptance tests.
- `CMD_STATUS=0x04` is now documented as **reserved/unsupported**, not an
  implemented v1 command.
- Watchdog policy for eventual device adapter: preserve SDK/default stack WDT
  behavior; do not add a stager-specific disable/feed path without the exact
  target SDK/toolchain implementation. READs remain bounded to <=48 bytes.

Historical gates at the end of Batch 4 were:

```text
OTA_CONTAINER_FORENSICS       = PASS
TELINK_CRC_CONVENTION         = PASS
TELINK_8258_LINEAGE           = PASS / exact production revision still gated
HOST_PERSISTENCE_GUARD        = PASS (offline CI)
Z2M_PRIVATE_CLUSTER_TRANSPORT = PASS (source-pinned + offline contract CI)
SYNTHETIC_END_TO_END_DUMP     = PASS
STAGER_OTA_INDEX_GUARD        = PASS
BOOTABLE_TC32_STAGER_BUILD    = BLOCKED (now superseded by 2026-09-06 result)
LIVE_CUSTOM_OTA               = NO_GO
PRODUCTION_DEVICE_MUTATION    = NO_GO
```

No new live-device mutation was authorized or performed in this phase.

## 2026-09-03 — Flash-size forensic (supervisor 5524449062): 512K confirmed

- App header size field == payload size exactly; last non-0xFF byte is the
  last file byte — **app genuinely built flush to the 512K ceiling** (316
  bytes below the 0x34000 NV_1 boundary).
- **512K-only addresses hardcoded**: MAC `0x76000` + factory config `0x77000`
  (end-of-image const table, code-referenced); **zero 1M-map constants** (the
  lone `0x80000` hit is an instruction-byte false positive, context inspected).
- `FLASH_MAP_SELECTION_LOGIC = A (hardcoded 512K)`;
  `FLASH_SIZE_CLASS = 512K`;
  **`MCU_EXACT_CANDIDATE = TLSR8258F512ET32`, confidence high** for the
  recovered 0x1416 lineage. `POWER_STAGE_CONTROL=UNKNOWN` unchanged.
- Evidence: `evidence/mcu-id-20260903/FLASH-SIZE-ADDENDUM.md`.
- Family question closed (TLSR8258/B85). Remaining spare purpose: 2024/2026
  revision identity, mains-PCB power-stage path, SWS pads, stock flash backup.

## 2026-09-03 — MCU ID pass 2 (supervisor 5523981212): TLSR825x family confirmed

- Telink TC32 binutils (2.20.tc32-elf-1.5, linux build under WSL2) obtained
  after the rgov win32 toolchain proved unusable (missing cygwin DLL).
- Full TC32 disassembly of the hash-verified historical payload produced.
- **Startup path matches the 2018-generation B85 `cstartup_8258.S`**:
  0x81→0x80006f flash wake, 0xAB wake sequence, no efuse delay (the 8278
  reset path's efuse-delay discriminator is ABSENT), identical IRQ handler,
  identical pool constants (0x80060c/0x80063e/0x80000c/0x80058a).
- MMIO: 64 shared-register literal hits; unique hits = stimer-block noise.
- FCC package quantified: QFN32-class (~8 pads/side), ≈5 mm body —
  TLSR8258-compatible; TLSR8278 (QFN48-only) incompatible.
- Decisions: `MCU_FAMILY=Telink TC32/B85 (TLSR825x)` confidence HIGH;
  `MCU_EXACT_CANDIDATE=TLSR8258F512ET32` (flash size open vs F1K variants),
  exact-part confidence MEDIUM; `FLASH_SIZE_CLASS=unresolved`;
  `POWER_STAGE_CONTROL=UNKNOWN` (module photo doesn't clear the mains PCB);
  `SPARE_STILL_REQUIRED=yes` (marking, 2024+ revision identity, mains-PCB
  power-stage path, SWS pads, stock flash backup).
- Evidence: `evidence/mcu-id-20260903/`.

## 2026-09-03 — Phase 1 forensics pass executed (supervisor 5522442315)

- Historical GL-C-009P Mini OTA (same imageType 0x1416) recovered from
  `Koenkk/zigbee-OTA` git blob, SHA-512 verified, parsed offline.
- **Boot layout = CLASSIC_TC32 (Telink B85 family), ISA = TC32.** Platform
  narrowed from "Telink" to the TLSR8258/8278 generation (B91 ruled out).
- FCC (2A6ZUGL-C-009P) internal photos: single QFN32-class SoC on a castellated
  Zigbee module, no second MCU — single-SoC architecture leaning, 2022 lineage.
- Live standard-attribute read pass (authorized): cluster revisions = 1;
  OTA client `currentZigbeeStackVersion=2` matches historical header.
- Decisions: `MCU_FAMILY=Telink TC32/B85`, `MCU_EXACT=UNKNOWN`,
  `MCU_CONFIDENCE=medium`, `POWER_STAGE_CONTROL=UNKNOWN (single-SoC leaning)`,
  `SPARE_STILL_REQUIRED=yes`. Machine-code 8258-vs-8278 match NOT_TESTED
  (no TC32 toolchain on the executor host).
- Evidence: `evidence/phase1-forensics-20260903/`.
- Support letter draft ready: `SUPPORT-LETTER-DRAFT.md` (user to send).

## 2026-09-03 — Phase 1 (software-only fingerprinting) executed

- Executor session per `analienx/config:skills/supervisor-executor/SKILL.md` v2.1.
- SAFETY_CLASS: SOFTWARE_READONLY. No writes to the device. No OTA update.
  Device-initiated protocol replies (`queryNextImageResponse` NO_IMAGE_AVAILABLE)
  were protocol-mandated responses to the supervisor-authorized check only.
- Live mutations (authorized by the supervisor procedure comment): temporary
  external OTA-diagnostic converter, temporary `ota.disable_automatic_update_check`,
  one read-only diagnostic extension. ALL REVERTED and verified (bindings and
  configured reporting byte-identical to pre-probe baseline).
- Result: **PARTIAL PASS**. All software-only probes exhausted. Remaining
  unknowns (MCU exact, power-stage architecture) require a sacrificial spare.
- Evidence: `evidence/phase1-software-only-20260903/` (raw originals on the HA
  host under `/config/zigbee2mqtt/gledopto_probe/`).

## Next

1. Keep closing supervisor-owned offline build/geometry/packaging gates in CI.
2. Resolve production-module exact silicon/flash/board facts with read-only
   evidence if possible; otherwise keep the sacrificial-spare gate.
3. Prove return-to-stock on a matching spare before considering production OTA.
4. Keep the current host/Z2M stack offline until a separate live gate is opened.
5. Firmware product plan after recovery remains RX-on-when-idle End Device
   (`ZB_ED_ROLE=1`, `ZB_ROUTER_ROLE=0`, `RX_ON_WHEN_IDLE=1`, `PM_ENABLE=0`).
