# Phase 1 forensics pass — 2026-09-03 (supervisor order 5522442315)

Executor: per `analienx/config:skills/supervisor-executor/SKILL.md` v2.1.
Boundaries held: no OTA update/serving, no memory-read experiments, no re-pair,
no unknown manufacturer writes, no opening the installed unit. Offline/public
forensics plus authorized standard-attribute reads only.

## A. Historical binary recovery — PASS

- Source: `Koenkk/zigbee-OTA` git blob `09c1e5ad3874a422cbe1e87e351e6478d4e1272e`
  (historical commit `f4260fe4`, later removed from index).
- SHA-512 verified: `868e671255db3c753a282125cdc4c333771cf1032423968b1412f9760cb105f97874261ab56559dc1cf54c0742eec062ccf9b8a75b4ef5e85b1485e8d5fd1aac`
- Binary kept local-only (`.local/`, gitignored) per policy; metadata archived here.

## B. OTA envelope

```text
magic              0x0BEEF11E
header_version     0x100
header_length      56
field_control      0x0
manufacturer_code  0x124F (4687)
image_type         0x1416 (5142)
file_version       0x24013001
stack_version      2
header_string      "Telink OTA Sample Usage"
total_image_size   212738
single subelement  tag 0x0000 (upgrade image)
```

## Raw payload

- size 212,676 bytes (≈207.7 KiB — near the ~208 KiB classic 512 KiB dual-image
  boundary; supporting constraint, not proof)
- SHA-256 `7413603aba92c83ef2499a386fbb355572cb103914669e15f15d1529ac67be62`

## C. Boot layout — CLASSIC_TC32 / ISA = TC32

```text
0x0000  0x30018058        TC32 reset entry (branch)
0x0004  0x025d2401        FILE_VERSION field
0x0008  0x544C4E4B        Telink boot marker "TLNK" (LE)
0x0012  0x124F            MANUFACTURER_CODE
0x0014  0x1416            IMAGE_TYPE
0x0018  0x00033EC4        firmware size
0x0020  startup code
```

Matches the official `cstartup_8258.S` / `cstartup_8278.S` (classic TC32 / B85
family) layout. **Not** the B91/RISC-V layout (which places the marker at 0x20).

## D. imageType convention

High byte 0x14 is not a stock Telink chip ID — GLEDOPTO redefined the
convention. Chip family therefore comes from boot layout + package evidence.

## E. Machine-code platform match — NOT_TESTED

No TC32 disassembler/toolchain (Ghidra+TC32 ext, radare2, objdump) available on
the executor host in this session. Recorded honestly: 8258-vs-8278 remains
unresolved by disassembly. Listed as the first task on the spare.
## F. Size constraint

212,676-byte raw app image is consistent with (not proof of) a classic
512 KiB B85/TC32 design.

## G. FCC internal photos (2A6ZUGL-C-009P, doc 5902275)

Original 1269×951 embedded images extracted from the PDF; manual visual
inspection (no OCR) after 8x Lanczos + grayscale + contrast + unsharp:

- Solder side: coil/meander antenna area, no ICs.
- Component side: silkscreen `+GLE-DIM-Controller-V1` (20220427). A **pre-built
  castellated Zigbee module** with meandered PCB antenna, its own crystal, and
  a **single QFN SoC (~4–5 mm, QFN32-class pad count)**. No second MCU anywhere
  on the module; only small passives/SOT-23-class parts toward the triac stage.
- Power stage: three DPAK packages + AC-DC section.
- SoC laser marking: **not readable** at this source resolution.
- Relevance: same 0x1416 firmware lineage, but a 2022 filing covering
  GL-C-001P..009P as one circuit — corroboration only, NOT evidence about the
  2024/2026 GL-SD-301P hardware.

## H. Public mining for exact-chip disclosures — NO DISCLOSURE FOUND

- GitHub code/issues search: no public disclosure tying `20451203`,
  `0x1416`/`0x1415`, or GL-C-009P/GL-D-002P to a specific TLSR part.
- One Hubitat device database CSV (jshimota01/hubitat) independently shows two
  GL-C-008P units as `124F-1415-24013001` / swBuild `20451203` — confirming the
  cross-SKU fingerprint (`0x1415` = sibling image type, same file-version
  family), still no chip disclosure.
- zigbee-OTA#463 (closed): Gledopto supplied the Mini 20451203 image; it
  bricked non-Mini GL-C-009P revisions; users observed endpoint layout changes
  (1→11) after that OTA; Gledopto explicitly warned that "internal parts
  updating" makes firmware non-transferable even under the same model name.

## I. Live standard-attribute reads (authorized, read-only; temporary
extension deployed and fully removed afterwards; bindings + configured
reporting verified identical to baseline after removal)

```text
genBasic           0xFFFD = 1
genOnOff           0xFFFD = 1
genLevelCtrl       0xFFFD = 1
lightingColorCtrl  0xFFFD = 1
genOta 0x0000 upgradeServerId            = 0xFFFFFFFFFFFFFFFF (none assigned)
genOta 0x0001 fileOffset                 = 4294967295
genOta 0x0002 currentFileVersion         = 637612033 (0x26013001) ✓ live tuple
genOta 0x0003 currentZigbeeStackVersion  = 2  ✓ matches historical header
genOta 0x0004 downloadedFileVersion      = 0xFFFFFFFF
genOta 0x0005 downloadedZigbeeStackVersion = 0xFFFF
genOta 0x0006 imageUpgradeStatus         = 0 (normal)
genOta 0x0007 manufacturerId             = 4687 (0x124F) ✓
genOta 0x0008 imageTypeId                = 0xFFFF (not self-reported)
genOta 0x0009 minimumBlockReqDelay       = 0
```

Cluster revision 1 across Basic/OnOff/Level/Color indicates an older Telink
SDK generation — consistent with the classic TC32-era family. No brute-forcing
of manufacturer ranges (per order).

## J. GLEDOPTO support letter

Draft committed at `devices/gl-sd-301p/SUPPORT-LETTER-DRAFT.md` (full live
tuple + exact questions). Not sent by the executor; user action item.

## K. Decision block

```text
HISTORICAL_OTA_RECOVERED = YES (Koenkk/zigbee-OTA blob 09c1e5ad, SHA-512 verified)
HISTORICAL_OTA_SHA512 = 868e67...aac (verified)
OTA_HEADER = mfg 0x124F / imageType 0x1416 / fileVer 0x24013001 / stackVer 2 / headerLen 56 / total 212738
RAW_PAYLOAD_SIZE = 212676
RAW_PAYLOAD_SHA256 = 7413603aba92c83ef2499a386fbb355572cb103914669e15f15d1529ac67be62

BOOT_MARK_OFFSET = 0x08
BOOT_LAYOUT = CLASSIC_TC32
ISA = TC32

REFERENCE_8258_MATCH = NOT_TESTED (no TC32 toolchain in session)
REFERENCE_8278_MATCH = NOT_TESTED
REFERENCE_B91_MATCH = N/A (B91 layout ruled out)
MMIO_MAP_MATCH = NOT_TESTED
FUNCTION_LEVEL_MATCH = NOT_TESTED

FCC_PCB_REV = +GLE-DIM-Controller-V1 (20220427)
FCC_SOC_PACKAGE = single QFN SoC, QFN32-class, on castellated Zigbee module
FCC_SOC_MARKING = unreadable at source resolution
FCC_LINEAGE_RELEVANCE = same 0x1416 lineage; 2022 filing; corroboration only

MCU_FAMILY = Telink TC32 / B85-class (TLSR8258/8278 generation)
MCU_EXACT = UNKNOWN (8258 vs 8278 unresolved)
MCU_CONFIDENCE = medium (boot layout + package + stack-version cross-check; no machine-code match)
MCU_EVIDENCE = CLASSIC_TC32 boot layout w/ Telink marker; raw app 207.7 KiB
  fits 512 KiB dual-image map; OTA client currentZigbeeStackVersion=2 ==
  historical header stack_version; cluster revisions=1 (old SDK gen);
  multi-SKU dimmer strings (GL-C-003P/006P/007P/008P/009P)

POWER_STAGE_CONTROL = UNKNOWN (single-SoC leaning)
POWER_STAGE_EVIDENCE = FCC module photo shows exactly one SoC; no second MCU;
  small passives only between module and triac stage — but 2022 lineage only

SPARE_STILL_REQUIRED = YES
REASON_SPARE_REQUIRED = exact part suffix/flash size, 2024+ revision
  confirmation, power-stage trace, SWS pad mapping and full stock flash
  backup all require unpowered physical access to a sacrificial unit.
```

## Also closed (supervisor 5522293882)

- `action: off` repeats after reads: **CLOSED / NOT A DEVICE ANOMALY**
  (`state_action: true` makes Z2M publish reads/states as `action`).

## Hard rule honored

The recovered GL-C-009P image was used offline for static analysis only. It was
never placed in any OTA index, never served, and never offered to the device.
Binary not committed to this repo (metadata + hashes only).
