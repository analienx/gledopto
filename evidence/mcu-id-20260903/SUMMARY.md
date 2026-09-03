# MCU identification pass 2 — 2026-09-03 (supervisor order 5523981212)

Continuation of `phase1-forensics-20260903`. Offline analysis of the
hash-verified historical GL-C-009P payload only. No device I/O beyond the
already-reported standard reads. Production-device restrictions honored.

## TC32 toolchain

- `rgov/Ghidra_TELink_TC32` win32 toolchain is Cygwin-based and ships **no
  cygwin1.dll** → exes fail with STATUS_DLL_NOT_FOUND (0xC0000135); posix/bin
  entries are stub scripts. Documented, then worked around.
- Used the official Telink TC32 binutils from `flyskywhy/tc32` branch `linux`
  (same Telink toolchain lineage, `GNU objdump (Telink TC32 version 2.01build)
  2.20.tc32-elf-1.5`) under WSL2 Ubuntu. Verified `objdump -i` supports
  `binary` target with `tc32` arch.
- Full disassembly produced: 101,560 lines (`raw/gledopto-disasm-head.txt` is
  the first 0x400 region; full disassembly retained locally, not committed).

## SDK trailer

`tc32-elf-strings -a` over the raw payload: 1576 strings; **no**
`$$$tc_platform_sdk…$$$`, no `zigbee_sdk`, no `TLSR`/`8258`/`8278` strings.
Checked explicitly before declaring absent — this build predates the trailer
convention or it was stripped. The only product strings are GL-C-003P/006P/
007P/008P/009P.

## Startup path — B85/8258-generation match (structural)

Firmware startup disassembles as:

```text
0x00  tj 0xb4                     reset
0x04  0x025d2401                  FILE_VERSION
0x08  0x544C4E4B                  Telink marker
0x10  tj 0x220                    IRQ -> push {lr}/{r0-r7}/mrss/… handler
0x12  0x124F  0x14  0x1416        mfg / imageType
0x18  0x33EC4                     bin size
0x20  start_suspend: tpush {r2,r3}; tmovs r2,#0x81; tloadr r3,=0x80006f;
      tstorerb r2,[r3]            *(u8*)0x80006f = 0x81   (flash wakeup)
      <NOP sled>
0xb0  pool 0x0080006f; .data copy loops; CPSR 0x12/0x13 + SP inits;
      flash-wake sequence: wr 0, wr 0xAB, delay #6, wr 1;
Matches Telink's 2018-generation `cstartup_8258.S` (as shipped in
Ai-Thinker Telink_825X_SDK, 2018-05-08) **structurally, instruction-for-
instruction**:

| feature | 2018 8258 cstartup | 2020 8278 cstartup | firmware |
|---|---|---|---|
| 0x81 → 0x80006f | yes | yes | **yes** |
| flash wake 0/0xAB/delay6/1 | yes | yes | **yes** |
| EFUSE delay loop (~110 iters) after wake | **absent** | **present** | **absent** |
| IRQ handler shape (push{lr},{r0-r7},mrss…) | yes | yes | **yes** |
| pool: 0x80060c, 0x80063e, 0x80000c, 0x80058a | yes | yes | **all present** (×1/×1/×13/×1) |

The supervisor-stated discriminator — the 8278 reset path's flash-wake/**efuse**
delay handling — is **absent** from the firmware startup. The firmware startup
is the B85/8258-generation structure.

## MMIO map coherence

Both 8258 (B85) and 8278 (B87) register headers parsed
(`register_8258.h` / `register_8278.h`, Telink SIG_Mesh SDK_3.1.5 +
Telink_825X_SDK): 325 vs 298 addresses, 287 common.

- 64 common MMIO addresses appear as 32-bit literals in the firmware
  (coherent shared map; incl. `0x800740` system-tick block, DMA 0xc00 block).
- Chip-unique literal hits are confined to the stimer block boundary
  (0x80074f vs 0x800750/0x800754) — weak, borderline noise.
- The startup pool constants above (all B85 flash-controller block) are the
  strong signal.

## Flash map / size class

- Literal scan: `0x40000` ×1, `0x80000` ×1, `0x20000` ×4 — no decisive
  dual-image boundary signature.
- Raw app 207.7 KiB fits the classic 512 KiB dual-OTA map (~245 KiB max app
  half). 512K vs 1M remains unresolved without the spare.

## FCC package — quantitative

Re-measured on the original embedded image (p2_img0_11.jpeg), 12× Lanczos
grayscale crop (`qfn_tight.png`), manual count:

- pads per visible edge ≈ **8** (left ~8, bottom ~8, right ~8; top edge
  obscured by image crop) → QFN32-class
- package width ≈ **5 mm** (body ≈ 5.0–5.4 mm measured against the adjacent
  3.2 mm 3225 crystal)
- TLSR8258 (QFN32 5×5: F512ET32 / F1KET32 / F1KAT32): **compatible**
- TLSR8278 (QFN48 7×7 only): **incompatible**
      IC-tag init; bss zero; FLL_STK; jl 0x1885c (main)
```

## Required return

```text
TC32_TOOLCHAIN_USED = YES (Telink TC32 binutils 2.20.tc32-elf-1.5, linux build, WSL2)
GHIDRA_TC32_USED = NO (not required; objdump path sufficed)
SDK_TRAILER_FOUND = NO (explicitly checked; 1576 strings, none)
FCC_PIN_COUNT = 32-class (~8 pads/side on 3 visible edges)
FCC_PACKAGE_WIDTH_MM ≈ 5 (3.2 mm crystal reference)
TLSR8258_PACKAGE_COMPATIBLE = YES
TLSR8278_PACKAGE_COMPATIBLE = NO (QFN48-only family)
REFERENCE_8258_MATCH = YES (startup structural match, 2018-gen B85 cstartup)
REFERENCE_8278_MATCH = NO (efuse-delay discriminator absent; package incompatible)
STARTUP_MATCH = B85/8258 (flash-wake 0x80006f/0xAB sequence identical, no efuse delay)
MMIO_MAP_MATCH = COHERENT (64 shared-register literal hits; unique hits = stimer-block noise)
FUNCTION_LEVEL_MATCH = PARTIAL (startup-level + pool constants; full library-level diff still pending reference builds)
MCU_FAMILY = Telink TC32 / B85 (TLSR825x)
MCU_EXACT_CANDIDATE = TLSR8258F512ET32 (QFN32 5x5, 512 KiB) — flash size still open vs F1KET32/F1KAT32
MCU_CONFIDENCE = high (family) / medium (exact part: flash size + 2024-revision confirmation open)
FLASH_MAP_SIGNATURE = no decisive dual-image boundary literals
FLASH_SIZE_CLASS = unresolved (207.7 KiB app consistent with 512 KiB map)
POWER_STAGE_CONTROL = UNKNOWN (per order: module photo alone does not exclude a second MCU on the mains PCB)
SPARE_STILL_REQUIRED = YES
WHAT_EXACTLY_THE_SPARE_WOULD_STILL_PROVE =
  exact part suffix/flash size (marking), 2024+ GL-SD-301P revision identity
  (the FCC/startup evidence is 0x1416-lineage, not our board), power-stage
  control path on the mains PCB, SWS pad mapping, and full stock flash backup.
```

Note: this evidence derives from the historical same-imageType binary and the
2022 FCC filing — strong lineage evidence, not proof about the 2024/2026
GL-SD-301P board itself. The spare remains the closing gate.
