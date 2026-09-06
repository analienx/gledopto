# GL-SD-301P sacrificial spare qualification plan (Batch 7 task 2)

Executable checklist for qualifying a sacrificial GL-SD-301P as sufficiently
matching the installed production target
(`0xa4c13850cfcdb3a4`, hwVersion 2, dateCode 20240704, swBuildId 20651203,
OTA tuple `0x124F:0x1416:0x26013001`) before any production flash authorization
can even be considered. Nothing here authorizes touching the installed unit.

## Stage 0 — purchase screening (before buying)

| # | Check | PASS evidence | FAIL action |
|---|---|---|---|
| 0.1 | Listing photos show the exact model name `GL-SD-301P` on the housing/label (not GL-SD-001/301/301P-M variants) | legible label photo | reject listing |
| 0.2 | Listing/box photos show Gledopto retail packaging with a date/batch code; prefer stock photographed with 2024+ packaging | photo with batch code | deprioritize undated stock |
| 0.3 | Seller confirms the item is an unopened retail unit, not a returned/refurbished/repaired unit | seller statement | reject |
| 0.4 | At least one high-resolution photo of any module/PCB area visible through vents or the label window | photo | request more photos |

## Stage 1 — pre-OTA archive (before ANY firmware change)

| # | Check | Tool/procedure | PASS evidence |
|---|---|---|---|
| 1.1 | Pair to the isolated test coordinator; record full Basic cluster + endpoint list | Z2M (isolated instance) | `modelId=GL-SD-301P`, `manufacturerName=GLEDOPTO`, `swBuildId=20651203`, `dateCode=20240704`, `hwVersion=2`, EP11 (in [0,3,4,5,6,8,768,4096], out [25]), EP242 GP |
| 1.2 | Record OTA tuple via a read-only OTA check | Z2M `ota_update/check` | manufacturerCode 4687, imageType 5142, fileVersion 637612033 (0x26013001) |
| 1.3 | **Full physical flash backup** before any write | SPI programmer (TlsrPgm/EZP/xgflash), in-circuit or desoldered | raw dump ≥ 512 KiB (record exact size), SHA-256 + SHA-512 recorded, stored in quarantine |
| 1.4 | Forensically validate the dump | `tools/telink_ota_forensics.py` + manual parse | boot bank has valid `4B 4E 4C 54` marker at +8, declared size, xcrc32 PASS; opposite bank state recorded |
| 1.5 | **Record flash JEDEC ID + capacity** | programmer ID command (cross-check markings) | JEDEC triple recorded; capacity measured from dump size and ID |
| 1.6 | **Record MCU/package markings** | hi-res photos of the Telink chip top marking + package dimensions | marking text transcribed; photos archived |
| 1.7 | Bind evidence to the artifact | naming + sidecar | dump named `spare-<ieee>-<date>-preota.bin` + sidecar JSON with hashes |

**Stage-1 gate:** if JEDEC ID ≠ `0x1460C8` (or dump size ≠ 512 KiB), the spare is
NOT a geometry match for the current stager fixtures — stop and record.

## Stage 2 — stager OTA on the spare

| # | Check | Tool | PASS evidence |
|---|---|---|---|
| 2.1 | Build the stager OTA exactly as CI does (pinned TC32 + SDK V3.7.2.0/d5bc2f7b) | `tools/build_glsd_tc32_objects.sh` + `build_glsd_tc32_link_probe.sh` + `make_glsd_stager_ota.py` | 6/6 objects; both banks PASS; final.bin SHA-256 == CI values (0e0db4b3… / b55d671b…); sidecar JSON produced |
| 2.2 | Verify the OTA container | `telink_ota_forensics.py` | outer 0x124F/0x1416, fileVersion > 0x26013001, inner preamble `5D 02`, inner xcrc32 PASS |
| 2.3 | Serve to the SPARE ONLY (isolated coordinator, no production devices present) | Z2M per-device `{id,url}` request | spare accepts, downloads, reboots |
| 2.4 | Post-stager state | Z2M + forensics | stager reachable; OTA client alive (Image-Notify responds); private 0xFC00 on EP11; device remains joined WITHOUT rejoin |

## Stage 3 — stock-bank extraction on the spare

| # | Check | Tool | PASS evidence |
|---|---|---|---|
| 3.1 | PING/INFO gate | guarded runner | INFO geometry matches validated profile; old bank = opposite of executing stager bank; reconstructed old-bank CRC proven |
| 3.2 | Full dump | guarded runner (one outstanding READ, byte-bound) | byte count == old declared size; per-chunk sha256 log complete; no duplicates/conflicts |
| 3.3 | Reconstruction | host finalizer | exactly one diff at +8 (0x00→0x4B); reconstructed Telink CRC PASS |
| 3.4 | **Extraction ground truth** | compare reconstructed dump vs the Stage-1.3 physical backup of the stock bank | SHA-256 of reconstructed stock app == SHA-256 of the corresponding bank region in the physical backup (modulo the invalidated marker byte) |

**Stage-3 gate:** this converts "the dump protocol works" into "the dump protocol
is *correct*" — wireless extraction must reproduce the physically read bank.

## Stage 4 — return to stock on the spare

| # | Check | Tool | PASS evidence |
|---|---|---|---|
| 4.1 | Build reconstructed-stock OTA | supervisor's wrapper per the E2 contract (inner +0x02 fileVersion raised, xcrc32 recomputed, outer fileVersion satisfying the zcl_ota comparison) | forensics PASS; byte-diff vs original stock limited to the intended version fields |
| 4.2 | Serve via standard OTA to the spare | isolated Z2M | spare accepts, downloads, reboots back into stock |
| 4.3 | Stock identity restored | Z2M Basic read | swBuildId/dateCode/OTA tuple back to Stage-1.1 values (raised fileVersion per scheme expected) |
| 4.4 | Network persistence | Z2M | spare resumes on the SAME network WITHOUT re-pair; binds/reporting intact; boot bank = stock bank |
| 4.5 | Staging hygiene | forensics | formerly-stager bank back in validated/invalidated state per SDK semantics |

**Stage-4 gate:** PASS = spare returned to fully functional stock operation on its
original network credentials with stock verified against the physical backup.

## Minimum matching conditions (spare ⇒ production target)

The spare may be treated as sufficiently matching for the **return-path proof**
(Stages 3–4) when ALL hold:

1. Stage-1.1 tuple identical (model/manufacturer/hwVersion/dateCode/swBuildId/OTA tuple);
2. Stage-1.5 JEDEC ID identical and flash size identical (512 KiB);
3. Stage-1.6 MCU package/marking consistent with a TLSR8258-family part (for the
   stronger "production MCU" claim the marking itself must be legible and archived);
4. Stage-3.4 ground-truth match PASS on the spare.

Even a full PASS proves the *procedure* (stager → extraction → reconstruction →
return) on identical-lineage hardware. It does **not** by itself prove the
installed unit's physical geometry — the installed unit's own Stage-1.3/1.5
equivalents (physical backup + JEDEC read) remain the production-side evidence,
per the fail-closed preflight.

