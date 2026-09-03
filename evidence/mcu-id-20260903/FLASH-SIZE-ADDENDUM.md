# Flash-size forensic addendum — 2026-09-03 (supervisor order 5524449062)

## Ordered checks and results

1. **App header size field @0x18** = `0x33EC4` — **equals the extracted payload
   size exactly** (212,676 bytes).
2. **Distance to 0x34000** (512K-map NV_1 boundary) = **316 bytes (0x13C)** —
   confirms the supervisor's observation.
3. **Last non-padding byte** = `0x33EC3` = last byte of the file. **Zero tail
   padding** — the image is genuinely built to the 512K firmware ceiling, not
   padded or truncated.
4. **512K-map constants**:
   - `0x76000` (MAC) — exactly 1 occurrence, at `0x33d20`
   - `0x77000` (factory config) — exactly 1 occurrence, at `0x33d1c`
   - both sit **adjacent in an end-of-image const table**, next to the
     app-range markers `0x32ffc` ×2 (`0x33d14`/`0x33d18`) and
     `0x33d28: 0x31ef9`, `0x33d6c: 0x32ee4` (size/checksum fields), followed
     by the Gledopto identity block (`0x24013001`, `0x124f/0x1416`, the
     GL-C-003P/006P/007P/008P/009P strings, "GLEDOPTO", "20451203",
     "20240227") and CRC/OTA fields.
   - referenced by code at `0x33a34` (`tloadr` from `0x33d3c`) and
     `0x33a3e` (`tloadr` from `0x33da4`); a PC-relative pointer to the
     `0x76000` word exists at `0x33d1c` (`tadd r0, pc, #0` → `0x33d20`).
   - `0x34000` / `0x78000` / `0x7A000`: 0 raw occurrences (NV boundary logic
     likely derived arithmetically or in library code not linked with the
     constant).
5. **1M-map constants**: `0x96000` / `0xFC000` / `0xFE000` / `0xFF000` —
   **0 occurrences**. `0x80000` — 1 occurrence at `0x2c9ce`, which the
   disassembly shows is **inside the instruction stream** of a Zigbee
   callback (TC32 opcode bytes), i.e. a false positive, not a flash-map
   reference. `0x33000` — 1 occurrence at `0x13cf8` in a mixed literal/data
   region (values like `0x0084xxxx` RAM pointers) with no map context.
6. **No runtime flash-size detection** (option C) was found: no JEDEC-ID
   branch selecting between 512K/1M layouts, no 1M-map constants at all.

## Required return

```text
APP_SIZE_FIELD = 0x33EC4 (== payload size, exact match)
APP_PAYLOAD_SIZE = 212676 (0x33EC4)
DISTANCE_TO_0x34000 = 316 (0x13C)
LAST_NON_PADDING_OFFSET = 0x33EC3 (zero tail padding)
FLASH_MAP_512K_REFS = YES — hardcoded MAC 0x76000 + factory-config 0x77000
  (end-of-image const table 0x33d1c/0x33d20, code-referenced), app built
  flush against the 0x34000 NV_1 boundary
FLASH_MAP_1M_REFS = NO (single 0x80000 hit = instruction-byte false positive;
  all other 1M constants absent)
FLASH_MAP_SELECTION_LOGIC = A) hardcodes/selects the 512K map
FLASH_SIZE_CLASS = 512K
MCU_EXACT_CANDIDATE = TLSR8258F512ET32 (QFN32 5x5, 512 KiB)
MCU_EXACT_CONFIDENCE = high for the recovered 0x1416-lineage binary;
  2024/2026 GL-SD-301P revision confirmation still requires the spare
```

Caveat kept per order: full library-level RE of the NV driver was not
performed, so option C cannot be excluded with absolute certainty — but the
hardcoded 512K-only addresses, the app flush against the 512K ceiling, and the
complete absence of any 1M-map constant make A the strongly supported reading.

`POWER_STAGE_CONTROL=UNKNOWN` unchanged (mains PCB not accounted for).
Production-device restrictions unchanged and honored.