# Batch 2 control sample — non-GLEDOPTO Telink image

**Task:** analienx/gledopto#1, Supervisor comment `2026-09-05T09:15:13Z`, Batch 2 step 2.
**Goal:** confirm `VERIFIED_PLAIN_TELINK_OTA` + `NO_CONTAINER_AUTH_DETECTED` are generic to
Telink SDK output and not a GLEDOPTO property.
**Answer: confirmed**, on a non-GLEDOPTO image and on 203 further independent builds
([`CORPUS-BASELINE.md`](CORPUS-BASELINE.md)).

```text
GIT_BRANCH:   feat/forensics-and-wireless-prep
GIT_COMMIT:   0b3151574d831f166919395d367912066b7d0fe5   (v2 tool pull)
OS:           Windows (win32), cmd.exe
PYTHON:       3.14.4
TOOL:         tools/glsd_ota_forensics.py v2
```

## Control selected

```text
vendor folder : Tuya
filename      : 1730081189-oem_zg_tl8258_plug_OTA_1.1.2.bin
repository    : Koenkk/zigbee-OTA @ b56c8cd445762c2b5d22224f314620eb0bf5457a
device        : Tuya OEM "ZG" TLSR8258 smart-plug firmware (TL8258 = Telink part naming)
size          : 311026
sha256        : 4c16a0403e1633fa7098f91fcd378ebe4a7a76724f42b4e877a8ad8090213b0e
sha512        : 8c981bb6d2c25a2ca013fbf63fa34f974606c34b60a25ab62daad22af663685
                5356178694f4c4557902e6dc91705825636ed5bfb8f64e68efa3d69dadc6d881e
raw binary    : NOT committed (lives under gitignored .local/ota-secondaries/)
```

Chosen because it is a **Telink-published OEM build carrying Telink's own manufacturer code
`0x1141`**, which makes it a far better control than a third-party device merely suspected of
using Telink silicon. Vendor attribution from a filename is not evidence; the framing below is.

## v2 output — [`forensics.json`](forensics.json)

```text
container_verdict = VERIFIED_PLAIN_TELINK_OTA
auth_indicator    = NO_CONTAINER_AUTH_DETECTED
production_ota_go = false
headerLength 56, fieldControl 0x0000, mfg 0x1141, imageType 0xD3A3, fileVersion 0x00000052,
headerString "Telink OTA Sample Usage", totalImageSize 311026
subelements: one, tag 0x0000, length 310964, payload @62; trailing_bytes 0
boot_marker 0x544C4E4B, magic_5d02 5d02, declared_size 310964
crc_validation: PASS  stored 0xCE929C6E == computed 0xCE929C6E
```

Identical verdict and identical framing to the GLEDOPTO Batch 1 artifact:

| Property | GLEDOPTO GL-C-009P | Tuya TL8258 control |
|---|---|---|
| headerLength | 56 | 56 |
| fieldControl | `0x0000` (16-bit) | `0x0000` (16-bit) |
| imageSize offset | 52 (`== filesize`) | 52 (`== filesize`) |
| headerString | `Telink OTA Sample Usage` | `Telink OTA Sample Usage` |
| sub-element tag | `0x0000` plain | `0x0000` plain |
| trailing bytes | 0 | 0 |
| inner `5D 02` @+0x06 | present | present |
| inner marker @+0x08 | `0x544C4E4B` | `0x544C4E4B` |
| inner `+0x18` vs containerLen | `==` | `==` |
| CRC convention | reflected CRC-32, init `0xFFFFFFFF`, no final XOR, region minus trailing 4 | same, `PASS` |

**Conclusion:** the framing and the CRC verifier are properties of the **Telink SDK build
chain**, not of GLEDOPTO. Gate-0 item 4 is no longer resting on a single image.

## New finding this control forces — the inner identity fields are not reliable

```text
outer  mfg 0x1141   type 0xD3A3   ver 0x00000052
inner  +0x12 0x0000  +0x14 0x0000  +0x02 0x00000000     <- zero-filled
```

GLEDOPTO populates the inner manufacturer/imageType/version fields and they match the outer
header exactly (that was Gate-0 item 3 in Batch 1). This control leaves them at zero and still
verifies cleanly. So:

- Gate-0 item 3 must **not** be promoted to a generic structural expectation, and v2 correctly
  only *reports* those fields rather than gating on them.
- **Consequence for the stager we would build:** we cannot assume the GL-SD's bootloader ignores
  those fields. Since the shipped GLEDOPTO firmware *does* fill them, a stock-derived acceptance
  probe and any custom image should populate `+0x02/+0x12/+0x14` consistently with the outer
  header, so acceptance never hinges on a field whose role is unproven.
- Conversely, an image we build with those fields zero-filled is *format-conformant*, so a
  rejection keyed on them would be evidence of a GLEDOPTO-specific policy hook. That asymmetry is
  exactly what the download-only acceptance probe can cheaply discriminate.

### Correction to how I first stated this

My first draft implied "the Telink SDK leaves these zero". **That is wrong — it is build-dependent,
not an SDK behaviour.** Measured across seven marker-positive publishers
([`inner-identity-survey.py`](inner-identity-survey.py),
[`inner-identity-survey.json`](inner-identity-survey.json)):

```text
IMAGE                        OUTER mfg/type/ver      INNER mfg/type/ver      RESULT
pvvx  Z03MMC                 0x1141 0x020A 0x01393001  0x1141 0x020A 0x01393001  inner == outer
Yandex YNDX-00530            0x132F 0x0212 0x0000000D  0x132F 0x0212 0x0000000D  inner == outer
Candeo C-ZB-LC20v2           0x124F 0x1419 0x32013001  0x124F 0x1419 0x32013001  inner == outer
GLEDOPTO GL-C-002P           0x124F 0x1419 0x25013001  0x124F 0x1419 0x25013001  inner == outer
Tuya  oem_zg_tl8258 3.0.0    0x1141 0xD3A3 0x000000C0  0x0000 0x0000 0x00000000  inner ALL-ZERO
ThirdReality SmartPlug V101  0x1233 0xD3A6 0x10013065  0x1233 0xD3A6 0x0065D3A6  inner DIFFERS
Innr  bb262                  0x1166 0x0109 0x17103685  0x1141 0x0381 0x10013001  inner DIFFERS
```

**4 of 7 populate the fields consistently, 1 zeroes them, and 2 disagree with their own outer
header.** The Innr case is the decisive one: its outer header says `0x1166` (Innr) while its inner
Telink header says `0x1141` (**Telink's own code**) — a shipping vendor image whose two identity
words name *different manufacturers*. Any bootloader that compared them would reject Innr's own
firmware. So outer==inner identity demonstrably cannot be a validated invariant across this SDK
family; it is filler whose population is left to each publisher.

That is a *stronger* basis for "do not hard-gate on inner identity" than my original claim. It
also sharpens the design guidance rather than reversing it: consistency costs nothing, GLEDOPTO's
own firmware happens to be in the 4-of-7 consistent group, and we cannot prove the GL-SD's
bootloader is not in a minority that does compare — so a custom image should still fill
`+0x02/+0x12/+0x14` to match the outer header. What changes is that a *rejection* keyed on those
fields should now be read as evidence of an unusual target-specific hook, not as normal Telink
behaviour.


### Separately: manufacturer code `0x124F` is not exclusive to GLEDOPTO

The Candeo-path image above carries `manufacturerCode 0x124F` — GLEDOPTO's code — while the
`images/Candeo/` path attributes it to a different vendor. Whatever the cause (shared module
supplier or manifest misfiling), the operational consequence is direct: **`0x124F` alone does not
identify a GLEDOPTO image**, which widens the tuple-addressability concern from Batch 1. Probe
gating should key on explicit target IEEE + version floor, never on manufacturer code.


## Also confirmed

- `production_ota_go` remains `false` in output.
- No live device interaction; no Tuya guided migration; no raw control firmware committed.
