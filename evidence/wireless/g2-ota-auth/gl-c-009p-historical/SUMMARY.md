# G2 OTA-auth — historical `GL-C-009P(MINI)` container validation

**Task:** Offline OTA Forensics Validation (Batch 1) — analienx/gledopto#1, comment of `2026-09-05T08:28:04Z`
**Scope:** read-only, offline. No device interaction of any kind.
**Result:** `GATE0 = PASS` on all five Gate-0 items, reproduced by two independent implementations.

## Executor environment

```text
GIT_BRANCH:   feat/forensics-and-wireless-prep
GIT_COMMIT:   d934e4989eb776fc289be22178bc04142231704f  (contains origin/main @ 8b9efe4)
OS:           Windows (win32) — cmd.exe executor notebook
PYTHON:       3.14.4
```

`/tmp` does not exist on this host and `.local/` is gitignored, so the raw artifact was
downloaded to `.local/gl-c-009p.ota`. Placing it there makes an accidental commit of the
vendor binary structurally impossible rather than merely discouraged. It is **not** in
this repository and must never be added.

## G1 — artifact identity (three independent checks, all exact)

| Check | Expected (pinned) | Observed | Result |
|---|---|---|---|
| size | `212738` | `212738` | PASS |
| SHA-512 | `868e6712…8d5fd1aac` | `868e671255db3c753a282125cdc4c333771cf1032423968b1412f9760cb105f97874261ab56559dc1cf54c0742eec062ccf9b8a75b4ef5e85b1485e8d5fd1aac` | PASS |
| git blob | `09c1e5ad3874a422cbe1e87e351e6478d4e1272e` | `09c1e5ad3874a422cbe1e87e351e6478d4e1272e` | PASS |

SHA-256 (new, for future dedup): `196f06a3b0deaa0f0fd0f6c235182c7c5b64bd5c4bdeeae7a9bf8788377a8018`

Source: `Koenkk/zigbee-OTA` @ `f4260fe4dfa47561f607707ad38abb829eb95a83`,
path `images/Gledopto/GL-C-009P(MINI)_20451203_20240227.ota`.

## G2 — container parse

`tools/glsd_ota_forensics.py` returned, reproducibly (verbatim in
[`validator-console-output.txt`](validator-console-output.txt), structured in
[`forensics.json`](forensics.json)):

```text
container_verdict = VERIFIED_PLAIN_TELINK_OTA
auth_indicator    = NO_CONTAINER_AUTH_DETECTED
production_ota_go = false
headerLength = 56, mfg = 4687 (0x124F), imageType = 5142 (0x1416),
fileVersion = 604057601 (0x24013001), headerString = "Telink OTA Sample Usage",
totalImageSize = 212738, subelement tag = 0x0000, subelement length = 212676 (0x33EC4)
```

`evidence/wireless/g2-ota-auth/gl-c-009p-historical/verify_independent.py`
re-derives all of the above from the raw bytes **without reusing the committed tool's
logic** (framing recomputed, CRC recomputed with a table-free bit-serial
implementation rather than the tool's table loop). Its output is
[`independent-verification.json`](independent-verification.json).

### Gate-0 items, as defined in the `2026-09-04T21:53:09Z` comment

| # | Gate | Verdict | Evidence |
|---|---|---|---|
| 1 | outer OTA total size structurally valid | PASS | `totalImageSize=212738 = fileBytes = headerLength(56)+6+subLen(212676)`; trailing bytes = 0 |
| 2 | upgrade-image sub-element parsed correctly | PASS | tag `0x0000` (plain, **not** AES `0xF000`), `subLen == inner declaredSize @+0x18`, payload starts at 62 |
| 3 | GLEDOPTO outer mfg/image/version match Telink inner fields | PASS | outer `0x124F/0x1416/0x24013001` == inner `@+0x12/0x14/0x02` |
| 4 | final CRC/verifier convention identified from real image | PASS | stored tail `0xBC753C12`; matched by reflected CRC-32 `0xEDB88320`, init `0xFFFFFFFF`, **no final inversion**, over the declared region excluding its last 4 bytes. Alternates that did **not** match: init `0x00000000` (`0x4005987B`), CRC-field-zeroed (`0x47FEF1F4`), plain `zlib.crc32` (`0x438AC3ED`) |
| 5 | no unexplained signature/authentication material | PASS | 0 trailing bytes, `fieldControl = 0x0000`, no `0xF000` wrapper, no trailing Zigbee signature/certificate sub-element |

## Findings worth carrying forward

1. **The header is 56 bytes because `fieldControl` is 16-bit, not 32-bit.**
   The reading `mfg@10 / imageType@12 / fileVersion@14 / headerString@20 / totalImageSize@52`
   is the only framing under which all of these fields decode into
   mutually consistent values. It is confirmed by six independent cross-checks:
   `headerLength` self-agreement, `totalImageSize` equalling the file length,
   clean ASCII `headerString`, `0x124F` being the known Telink-lineage manufacturer code,
   `0x1416` matching the live device's own `commandQueryNextImageRequest`, and
   `0x24013001` matching the inner payload's own version field. Any future parser that
   assumes a 4-byte `fieldControl` (→ 60-byte header) will misframe this family.
   *I raised this as a suspected off-by-two bug in the tool mid-batch; the raw byte dump
   disproved my suspicion. Recorded here so the retraction is on the ledger, not just in chat.*
2. **`0x33EC4` sits exactly 316 bytes below Telink's documented `0x34000` application
   ceiling** (212992 − 212676 = 316, recomputed). Independently corroborates the
   dual-bank 512 KiB working model, and shows the real-world payload nearly saturates
   the declared application region — i.e. a dump stager has almost no headroom to
   co-reside with the stock application.
3. **Byte-order trap on the startup flag.** u32 `0x544C4E4B` is stored little-endian as
   bytes `4B 4E 4C 54`, which renders as `KNLT`; read MSB-first it is `TLNK`. A grep for
   the ASCII string will find neither spelling. Match on the u32, not the text.
4. **The committed tool validates the container but does not itself close Gate 0.**
   `tools/glsd_ota_forensics.py` never parses the inner Telink mfg/type/version fields
   (Gate-0 item 3) and treats a non-`0x0000`, non-`0xF000` sub-element tag as
   `UNKNOWN_SUBELEMENT` without enumerating further elements, so it cannot by itself
   prove "no unexplained authentication material" (item 5). Both gaps are closed by
   `verify_independent.py` in this directory. If the intent is for the committed tool to
   be the single source of truth, it needs items 3 and 5 added; I did **not** modify it
   (Batch 1 is run-only).

## Load-bearing consequence for the acceptance probe

The historical image shares `(manufacturerCode, imageType)` with the live device but its
`fileVersion` is **lower**:

```text
live GL-SD-301P   0x124F / 0x1416 / 0x26013001   (swBuild 20651203)
historical GL-C   0x124F / 0x1416 / 0x24013001   (swBuild 20451203)
delta             -0x02000000  →  a downgrade, not an upgrade
```

Device-side values are transcribed from
[`evidence/phase1-software-only-20260903/raw/ota-live-descriptor.json`](../../../phase1-software-only-20260903/raw/ota-live-descriptor.json)
(`commandQueryNextImageRequest`, captured 2026-09-03T06:52:02Z) — not re-read live in this batch.

Two consequences:

- Any future download-only acceptance probe must carry `fileVersion > 0x26013001`, since
  a lower version may be rejected on the version comparison alone and would then teach us
  nothing about signature policy.
- Because the same `(0x124F, 0x1416)` tuple is shared with a *different physical product*,
  a container in this lineage is tuple-addressable by the GL-SD. That is exactly the
  failure mode issue #1 warns about ("never identify compatibility from
  manufacturerCode/imageType alone") and it raises the blast radius of any mis-pinned
  image well above "wrong device gets wrong firmware". Identity gating for future probes
  should require an explicit target IEEE plus version floor, not tuple alone.

## What this does **not** prove

- Nothing about whether the GL-SD-301P **bootloader** would accept an image with this
  tuple. Container format ≠ acceptance policy. `GL_SD_OTA_ACCEPTANCE = UNKNOWN`, `G4` open.
- This is **cross-model** evidence (GL-C-009P(MINI)). The "plain, unsigned container"
  result is proven for *this pinned build*, not for GLEDOPTO generally; a newer GL-SD
  build may sign or AES-wrap. `G3` (SDK source alignment) and `G4` remain open.
- `production_ota_go = false`. No Gate beyond G2 has been satisfied.

## Open gap

Gate-0 item 4 was established from **one** real image. The standing ask for at least one
public non-GLEDOPTO TLSR8258 Zigbee OTA (e.g. `pvvx/ZigbeeTLc`) as a control, to show the
convention is generic Telink rather than GLEDOPTO-specific, is tracked separately and is
not closed by this document.
