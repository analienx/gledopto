# GL-SD-301P wireless stock-firmware recovery design

Status: **OFFLINE IMPLEMENTATION / NO LIVE CUSTOM OTA AUTHORIZED**

Target: `LivingRoomCircleLightDimmer`, IEEE `0xa4c13850cfcdb3a4`, stock
`20651203 / 0x124F:0x1416:0x26013001`.

## Objective

Recover the stock application image from the user's own GL-SD-301P without
opening the enclosure or attaching SWS/UART wires, while retaining a bounded
physical-recovery fallback.

## Why this is plausible

Telink's TLSR8258/B85 Zigbee OTA design for 512 KiB flash uses alternating
application banks at `0x00000` and `0x40000`. The recovered GLEDOPTO Telink
lineage application payload is `0x33EC4` bytes, only 316 bytes below the
`0x34000` application-bank ceiling documented by Telink. This is strong layout
corroboration, not yet proof of the exact installed PCB.

The standard Telink OTA path checks Zigbee OTA identity/version and verifies the
received image before marking it valid and rebooting. We have not found proof
that GLEDOPTO added mandatory public-key signing. Absence of evidence is not
permission to assume unsigned OTA, so acceptance is tested in stages.

## Flash model under test

```text
0x00000  bank A application
          ... up to application-bank ceiling (~0x34000)
0x40000  bank B application
          ...
upper flash: factory/MAC/NV/calibration areas — OUT OF DUMP SCOPE
```

The dump stager reads only the *other application bank*. It does not read or
export Zigbee network keys, MAC/factory data or calibration sectors.

## Gate 0 — offline format + verification proof

Use `tools/telink_ota_forensics.py` on the historical recovered GLEDOPTO
`GL-C-009P(MINI)_20451203_20240227.ota` and at least one upstream TLSR8258
reference OTA.

Required before any live custom image is served:

- Zigbee OTA container parses cleanly;
- upgrade-image sub-element is identified correctly;
- GLEDOPTO outer mfg/image/version match its Telink inner fields;
- actual final verification/trailer convention is identified, not guessed;
- no unexplained signature/authentication trailer exists.

If this gate fails, STOP. Physical SWS remains the deterministic path.

## Gate 1 — download-acceptance probe (future live test)

`tools/make_ota_acceptance_probe.py` creates a structurally valid **offline**
artifact with target `0x124F/0x1416`, a version higher than `0x26013001`, an
obviously non-bootable marker, and deliberately wrong payload CRC.

A future live test would answer only:

> Will stock GL-SD request/download blocks for an image we constructed?

Expected result, but only after Gate 0 proves the exact verification path:
the inactive bank receives blocks; final verification fails; the current active
bank remains active and there is no bank switch.

**This branch does not authorize serving this probe to the device.**

## Gate 2 — read-only stager build

Only after Gate 1 proves acceptance do we compile a minimal B85/TLSR8258
stager. Requirements:

- compatible Telink Zigbee/NV layout;
- determine current active bank at runtime;
- select opposite bank as source;
- `flash_read_page()` only for extraction;
- no erase/write operation exposed by the dump protocol;
- custom read protocol on `0xFC00`;
- source range capped to the proved application-bank size;
- mains dimmer output held in a safe OFF state while the stager runs.

## Gate 3 — wireless extraction

Host requests 64-byte chunks. Device replies with offset + data + CRC32.
Receiver supports out-of-order chunks, exact duplicate handling, missing-chunk
resume, and final SHA-256. Completion requires every expected chunk.

## Gate 4 — recovery before deployment

Before a bootable stager ever reaches the production device, independently
prove a return path to stock. Preferred is a known exact stock OTA when one is
recovered. Otherwise boot/bank-validity behavior must be proven offline first.
SWS remains the final physical recovery fallback.

Do not make the production device dependent on an untested one-way transition.

## Artifacts

- `tools/telink_ota_forensics.py` — OTA/sub-element parser + CRC detector
- `tools/make_ota_acceptance_probe.py` — quarantined CRC-invalid probe builder
- `tools/glsd_dump_protocol.py` — host reassembler/resume logic
- `firmware/wireless-dump-stager/glsd_dump_protocol.h` — wire contract
- `firmware/wireless-dump-stager/PROTOCOL.md` — protocol description

## Hard invariants

1. No custom OTA is served to the live GL-SD during this phase.
2. No raw stock dump is committed to GitHub.
3. Network-key/factory/MAC/calibration sectors are outside dump scope.
4. Previous application bank is read-only during extraction.
5. Any unexplained authentication/signature mechanism blocks live testing.
6. Recovery is designed and checked before a bootable stager is served.

## Evidence anchors

- Telink Zigbee SDK developer manual: TLSR8258/B85 multi-address OTA uses
  alternating `0x00000` / `0x40000` application locations.
- `pvvx/ZigbeeTLc`: TLSR825x firmware based on Telink SDK demonstrates ordinary
  `flash_read_page()` access and documents mfg/image matching for OTA.
- Historical `Koenkk/zigbee-OTA` GLEDOPTO image is platform-lineage evidence
  only; it is NOT GL-SD firmware and must never be flashed to this dimmer.
