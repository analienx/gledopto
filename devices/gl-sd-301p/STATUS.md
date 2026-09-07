# STATUS — gl-sd-301p

## 2026-09-07 — Core power-stage + PUSH + PB4 behavior solved and safety-converged offline

- The exact support-supplied GLEDOPTO OTA is retained as a third-party reference
  artifact with provenance/hashes and is explicitly outside the project licence.
  Implementation code remains independently authored from the interoperability
  contract rather than copied/translated vendor implementation expression.
- `POWER_STAGE_CONTROL = SECOND_MCU_UART`, **HIGH software confidence**.
- Confirmed transport required by the independent implementation:
  - TLSR8258 UART;
  - 9600 baud, 8 data bits, no parity, 1 stop bit;
  - TX = PB1, RX = PA0;
  - six application bytes on the UART wire;
  - Telink's internal 4-byte DMA length prefix is not wire data;
  - stock RX callback is effectively a no-op for the analyzed control path.
- Confirmed control frame:

  ```text
  A5 5A CC VV 04 AA
  ```

- Normal lamp output is family `0x01`:
  - OFF = `A5 5A 01 00 04 AA`;
  - ON/Level = `A5 5A 01 LL 04 AA`, where `LL` is normal Zigbee
    `currentLevel` after configured minimum-output handling;
  - stock startup re-sync uses the same normal family-`0x01` refresh.
- PC2 physical PUSH behavior is solved without wiring:
  - external PUSH sense path, ~1 ms pulse decoder;
  - short activation toggles normal On/Off;
  - long activation performs standard Level steps;
  - direction reverses after long release;
  - independently implemented and host-tested.
- Onboard keyboard mapping is separated from PC2/PB4:
  - PC4 / keycode 1 = RESET path (HIGH confidence);
  - PC3 / keycode 2 = LEVEL path (HIGH confidence);
  - Telink `kb_scan_key(0,1)` compatible scanner.
- The formerly unexplained `currentLevel >> 1` path is now tied to PB4:
  - PB4 (`0x0110`) is an active-high auxiliary input with stock 100 kΩ pulldown;
  - after 11 consecutive high ~1 ms polls, stock requests
    `A5 5A 01 (currentLevel >> 1) 04 AA` on every poll while PB4 stays high;
  - logical Zigbee `currentLevel` is not changed;
  - a low sample resets qualification;
  - exact physical/business role remains `UNKNOWN` and must not be guessed;
  - independent compatibility module + golden-vector tests are green.
- Family `0x02` (`00,01,02,03,04,0F`) is outside ordinary On/Off/Level and the
  solved PC2/PB4 family-`0x01` behavior. Individual semantic names remain optional
  full-parity work rather than blockers for core dimming.
- A fail-closed independent output guard is now implemented and CI-tested:
  - boot/reset starts locked OFF;
  - the confirmed OFF frame remains available even with invalid restored state;
  - ZCL `currentLevel=0xFF` cannot be emitted as an ON level;
  - logical OFF dominates PB4 and all other auxiliary output requests;
  - runtime faults latch OFF and require explicit clear + validated re-arm;
  - family `0x02` is excluded from the core application runtime path.
- The solved UART output policy, PC2 PUSH decoder, PB4 compatibility handler and
  safety guard are now converged into `glsd301p_runtime_core.[ch]`. Application,
  PUSH and PB4 requests all pass through one guarded output interface; restored
  application state is an independent mandatory gate in addition to the guard's
  own ready state.
- A compile-time target architecture firewall now exists at
  `firmware/glsd301p-ed/glsd301p_target_contract.h`. CI proves a valid target
  configuration compiles and adversarially proves compilation fails for:
  - Router role or coordinator role;
  - non-End-Device role;
  - PM enabled;
  - RX-on-when-idle disabled;
  - missing Groups, OnOff or Level Control support;
  - empty group capability;
  - endpoint drift away from 11;
  - non-TLSR8258 target selection.
- Repository/host CI enforces the vendor-reference/implementation boundary and
  all UART/PC2/PB4/output-guard/runtime-core/architecture-contract tests.
- `CORE_ONOFF_LEVEL_ENCODER_READY = true`.
- `PHYSICAL_PUSH_BEHAVIOR_READY = true`.
- `PB4_AUX_BEHAVIOR_READY = true`.
- `RUNTIME_SAFETY_GUARD_READY = true`.
- `HOST_RUNTIME_CORE_READY = true`.
- `TARGET_ARCHITECTURE_FIREWALL_READY = true`.
- `PRODUCTION_ENCODER_READY = false` because the actual Telink End Device target
  build/glue has not yet been converged and proven on the pinned toolchain.
- `FIRST_FLASHABLE_CANARY_ALLOWED = false`.
- Canonical interface: `devices/gl-sd-301p/interoperability/INTERFACE.md`.
- Canonical safety policy: `devices/gl-sd-301p/SAFETY.md`.

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
  recovered 0x1416 lineage. `POWER_STAGE_CONTROL=UNKNOWN` at that historical point.
- Evidence: `evidence/mcu-id-20260903/FLASH-SIZE-ADDENDUM.md`.

## 2026-09-03 — MCU ID pass 2 (supervisor 5523981212): TLSR825x family confirmed

- Telink TC32 binutils (2.20.tc32-elf-1.5, linux build under WSL2) obtained
  after the rgov win32 toolchain proved unusable (missing cygwin DLL).
- Full TC32 disassembly of the hash-verified historical payload produced.
- **Startup path matches the 2018-generation B85 `cstartup_8258.S`**:
  0x81→0x80006f flash wake, 0xAB wake sequence, no efuse delay (the 8278
  reset path's efuse-delay discriminator is ABSENT), identical IRQ handler,
  identical pool constants (0x80060c/0x80063e/0x80000c/0x80058a).
- FCC package quantified: QFN32-class (~8 pads/side), ≈5 mm body —
  TLSR8258-compatible; TLSR8278 (QFN48-only) incompatible.
- Decisions at that point: `MCU_FAMILY=Telink TC32/B85 (TLSR825x)` confidence HIGH;
  `MCU_EXACT_CANDIDATE=TLSR8258F512ET32`, exact-part confidence MEDIUM;
  `POWER_STAGE_CONTROL=UNKNOWN` pending later GL-SD-301P-specific evidence.
- Evidence: `evidence/mcu-id-20260903/`.

## 2026-09-03 — Phase 1 forensics pass executed (supervisor 5522442315)

- Historical GL-C-009P Mini OTA (same imageType 0x1416) recovered from
  `Koenkk/zigbee-OTA` git blob, SHA-512 verified, parsed offline.
- **Boot layout = CLASSIC_TC32 (Telink B85 family), ISA = TC32.** Platform
  narrowed from "Telink" to the TLSR8258/8278 generation (B91 ruled out).
- FCC (2A6ZUGL-C-009P) internal photos: single QFN32-class SoC on a castellated
  Zigbee module, no second MCU visible on that sibling module — historical
  family evidence only, not a GL-SD-301P mains-PCB conclusion.
- Live standard-attribute read pass (authorized): cluster revisions = 1;
  OTA client `currentZigbeeStackVersion=2` matches historical header.
- Evidence: `evidence/phase1-forensics-20260903/`.

## 2026-09-03 — Phase 1 (software-only fingerprinting) executed

- Executor session per `analienx/config:skills/supervisor-executor/SKILL.md` v2.1.
- SAFETY_CLASS: SOFTWARE_READONLY. No writes to the device. No OTA update.
- Live mutations used only for the authorized diagnostic procedure were reverted
  and bindings/configured reporting verified against baseline.
- All software-only probes available at that time were exhausted without changing
  the production unit's firmware or physical state.
- Evidence: `evidence/phase1-software-only-20260903/` (raw originals remain
  outside the implementation repository).

## No-spare constraint

No sacrificial GL-SD-301P is available. A spare is therefore **not** represented
as a pending mandatory prerequisite. The project uses an explicitly higher-risk
production-only/no-spare track with separate live authorization.

The earlier production unit already passed two deliberately non-bootable OTA
acceptance experiments, including an exact-size shadow covering the real
candidate's inactive-bank range; both were rejected as `INVALID_IMAGE`, and stock
subsequently survived normal control checks and a physical power cycle. Those
experiments reduce inactive-bank/OTA-path uncertainty but do not eliminate the
irreducible first-valid-custom-boot risk.

Nothing in the clean-room safety/runtime work above grants live-flash
permission. `FIRST_FLASHABLE_CANARY_ALLOWED=false` remains authoritative until a
specific reproducible Telink End Device artifact and its preflight are reviewed.

## Next

1. Integrate `glsd301p_runtime_core` as the only application-facing power-stage
   path in an independently authored TLSR8258/Telink End Device target. Include
   `glsd301p_target_contract.h` after final Telink role/feature configuration so
   architecture drift becomes a compile failure.
2. Rebuild with the pinned Telink SDK/toolchain and require, on the same SHA:
   - real target links against the End Device stack, never `libzb_router.a`;
   - `ZB_ED_ROLE=1` and no Router/coordinator role symbols/configuration;
   - RX-on-when-idle enabled and PM disabled;
   - mains power source;
   - endpoint 11;
   - standard On/Off + Level server behavior;
   - unicast/direct-binding and **group-addressed** On/Off + Level reception;
   - reporting compatibility;
   - UART PB1 output path routed only through the guarded runtime core;
   - boot/reset first power-stage frame = confirmed OFF;
   - no accidental family-`0x02` application call path.
3. Keep family-`0x02`, vendor configuration-state fields, and PB4 physical-role
   naming as optional/full-parity work unless a concrete required feature depends
   on them; do not guess them into the runtime path.
4. After the integrated firmware artifact is reproducible and all software gates
   pass, make a separate no-spare production go/no-go decision with exact artifact
   attestation and explicit operator authorization. Do not silently substitute a
   nonexistent sacrificial-spare gate.
