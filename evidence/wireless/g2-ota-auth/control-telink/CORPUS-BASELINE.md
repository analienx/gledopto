# Telink control baseline — 218 real public Zigbee OTA images

**Purpose:** replace the single-artifact basis of Batch 1's Gate-0 item 4 with a measured
baseline, and calibrate which "Telink" properties are generic versus GLEDOPTO build habits.
**Method:** every image downloaded from `Koenkk/zigbee-OTA` @ `b56c8cd445762c2b5d22224f314620eb0bf5457a`
into gitignored `.local/ota-secondaries/` (**no binary committed**), then parsed twice — once by
the committed `tools/glsd_ota_forensics.py` v2, once by an independent table-free bit-serial
implementation. The fast path used for bulk CRC was asserted bit-serial-equal on the 212 KiB
GLEDOPTO region before being trusted at scale.

Generator: [`corpus_baseline.py`](corpus_baseline.py) — raw per-file results:
[`corpus-baseline.json`](corpus-baseline.json). Snapshot: 218 binaries.

## Headline

**Gate-0 item 4 is no longer resting on one image.** The CRC convention reproduces on
**203 independent images from 10 publishers across 8 distinct Zigbee manufacturer codes**, none
of which is the Batch 1 artifact's own build.

```text
203 VERIFIED_PLAIN_TELINK_OTA   (193 of them non-GLEDOPTO)
 11 INVALID_TELINK_APP_HEADER   correctly rejected non-Telink builds
  3 INVALID_SIZE_MISMATCH
  1 INVALID_TELINK_CRC
```

Manufacturer codes in the verified set: `0x1141`×69, `0x1166`×59, `0x6565`×29, `0x1233`×19,
`0x124F`×11, `0x1407`×9, `0x132F`×5, `0xDB15`×2.

`0x1141` is **Telink's own** code and `0x124F` is GLEDOPTO's; the other six are unrelated
vendors. `headerString` "Telink OTA Sample Usage" appears on 105 of them — i.e. it is the
unmodified SDK default that GLEDOPTO never bothered to change, **not** a GLEDOPTO marker.

| Property | Result across the verified set |
|---|---|
| CRC = reflected CRC-32 `0xEDB88320`, init `0xFFFFFFFF`, no final XOR, region minus trailing 4 bytes | **203/203 PASS** |
| inner marker `0x544C4E4B` @+0x08 | 204 images carry it; **203/204** satisfy the CRC → 99.5% |
| inner `+0x18 == containerLen` | **203/203** |
| sub-element chain ends exactly at EOF | **203/203** trailing 0 |
| `imageSize` still at offset 52 | 203/203, including the one `headerLength=60` image |
| v2 false positives on real input | **0** |
| v2 false negatives on real input | **0** |

## What is generic, and what was GLEDOPTO-only

Confirmed **generic to the Telink SDK build chain**: the 16-bit `fieldControl` / 56-byte header
framing, plain sub-element tag `0x0000`, `5D 02` @+0x06, marker @+0x08, `+0x18 == containerLen`,
and the CRC verifier. Batch 1's central findings therefore generalize.

**Not generic — GLEDOPTO build habit.** All 11 GLEDOPTO-family images populate the inner
identity fields and they match the outer header. The `0x1141` control leaves
`+0x02/+0x12/+0x14` **zero-filled** and still passes cleanly. So Gate-0 item 3 must not be
promoted to a structural expectation. See [`SUMMARY.md`](SUMMARY.md).

## Finding that cuts against my own Batch 1 note — flash geometry

Batch 1 stated that the GL-C-009P payload sitting "exactly 316 bytes below Telink's documented
`0x34000` application ceiling" *strongly corroborates* the dual-bank 512 KiB model.
**That corroboration is much weaker than I claimed, and this baseline is what shows it.**

```text
81 of 218 images have a payload LARGER than 0x34000 (212992)
   Tuya  oem_zg_tl8258_plug_OTA_1.1.2.bin    0x4BEB4  (310964)   <-- the Batch 2 control
   Telink oem_zg_tl8258_plug_OTA_1.0.13.bin  0x4B1A4  (307620)
   Tuya  oem_tl8258_zg_breaker_1.0.10.bin    0x4D214  (315924)
```

Those are **TLSR8258** builds — the same part family assumed for the GL-SD-301P — shipping
~310 KB payloads, which cannot fit a `0x34000` application bank at all. So:

- `0x34000` is a property of a particular 512 KiB dual-bank **flash map**, not of the TLSR8258
  silicon. A 310 KB image proves at least one other live layout (larger flash, or non-dual-bank).
- Within the GLEDOPTO family the ceiling does bind: **11/11** GLEDOPTO images are at or below
  `0x34000`, and two sit within 400 bytes of it (largest `0x33F44`, 188 bytes of headroom).
  Consistent with a 512 KiB dual-bank part — but it is now *one* consistent observation rather
  than strong independent corroboration, since same-chip images elsewhere break the model.

**Consequence for the stager design:** the `0x00000` / `0x40000` bank pair and the `0x34000`
ceiling must be **measured on the target**, not inferred from public image sizes. The transfer
document's fail-closed geometry gates are the right response; what changes is that they cannot
be relaxed on the strength of this lineage argument. `MCU_EXACT` stays unresolved.

## Two smaller corrections recorded while measuring

1. `headerLength = 60` does **not** move `imageSize`. The single `60` image in the verified set
   (ThirdReality scale) keeps `imageSize` at offset 52 and carries 4 reserved bytes before the
   sub-element chain. My own first-pass heuristic (`imageSize at headerLength − 4`) mis-located
   it and produced a garbage 4293918720; v2's fixed offset 52 was correct. I nearly reported
   that as a v2 bug — it is not one, and the note is here so the mistaken reading is not reused.
2. `full\Yandex__132f-0213-00000018-YNDX-00531.zigbee` carries a valid Telink marker and sane
   framing but its trailing u32 (`0x0AA34ACC`) is not this CRC (`0x268D8C43`). v2 correctly
   returned `INVALID_TELINK_CRC`. It is the corpus's only marker-bearing CRC failure and is
   sufficient to show the convention is not *universally* implied by the marker alone.

## Scope note

This is a **container-format** baseline on **other vendors' public images**. It says nothing
about the GL-SD-301P bootloader's acceptance policy, and nothing about our own target's flash
geometry. `production_ota_go` remains `false` throughout; no device was contacted.
