# STATUS — gl-sd-301p

## 2026-09-07 — Power-stage architecture resolved + clean-room implementation gate

- Exact support-supplied GL-SD-301P firmware was analyzed in a private/research
  context solely to determine hardware interoperability information. The public
  implementation repository now retains only sanitized interface facts; vendor
  firmware and reconstructable chunks are prohibited by `CLEAN_ROOM.md` + CI.
- `POWER_STAGE_CONTROL = SECOND_MCU_UART`, **HIGH software confidence**.
- Confirmed interface required by the independent implementation:
  - TLSR8258 UART;
  - 9600 baud, 8 data bits, no parity, 1 stop bit;
  - TX = PB1, RX = PA0;
  - lighting/control application emits six-byte UART payloads;
  - the Telink SDK driver's internal 4-byte DMA length prefix is not wire data.
- Live lighting/transition control paths call the UART transmitter, so UART is
  not merely an unused/debug initialization path.
- TLSR PWM references in the analyzed application resolve to enable/disable
  control only; no corresponding variable PWM compare/cycle path was established.
  Therefore the new firmware will preserve the serial power-controller boundary
  rather than inventing mains phase-cut timing.
- Six-byte protocol is **PARTIAL**:
  - byte 2 is a command-family discriminator;
  - byte 3 is a dynamic family-specific value;
  - families 0x01 and 0x02 are confirmed in level/transition-related paths;
  - exact bytes 0/1/4/5, level mapping, mode mapping, checksum/counter semantics,
    startup sync and electrical-OFF sequence remain UNKNOWN.
- `PRODUCTION_ENCODER_READY = false`.
- `FIRST_FLASHABLE_CANARY_ALLOWED = false`.
- Shortest remaining engineering step: isolated black-box UART capture on the
  sacrificial spare, varying one normal control at a time, followed by publication
  of sanitized wire test vectors only.
- Canonical interface: `devices/gl-sd-301p/interoperability/INTERFACE.md`.

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

## Next

1. **Clean-room protocol completion on sacrificial spare:** trace PB1/PA0 at the
   low-voltage controller boundary and capture six-byte UART transactions for
   OFF, ON, stable levels and one controlled transition. Publish sanitized
   input/output vectors only.
2. Complete the independent `glsd_power_stage_*` UART adapter from the confirmed
   interface specification once the blocking fields are measured.
3. Build the RX-on-when-idle Zigbee End Device independently from public Telink
   SDK/standards: `ZB_ED_ROLE=1`, `ZB_ROUTER_ROLE=0`, `RX_ON_WHEN_IDLE=1`,
   `PM_ENABLE=0`, mains power.
4. Preserve endpoint/OnOff/Level/reporting/direct-binding behaviour and add
   regression tests before any canary image exists.
5. **Only after protocol + CI + spare hardware gates pass:** authorize the first
   sacrificial canary. The installed production unit remains out of scope.
