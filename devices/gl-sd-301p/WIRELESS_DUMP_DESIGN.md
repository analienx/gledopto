# GL-SD-301P wireless stock-firmware recovery design

Status: **host/radio-independent implementation complete offline; TC32 target build blocked; NO LIVE CUSTOM OTA AUTHORIZED**

Target:

```text
friendly name LivingRoomCircleLightDimmer
IEEE          0xa4c13850cfcdb3a4
model         GL-SD-301P
hwVersion     2
stock         20651203 / 0x124F:0x1416:0x26013001
endpoint      11
```

## Objective

Recover the stock application image from the user's own GL-SD-301P without
opening the installed enclosure or attaching SWS/UART wires, while preserving a
bounded physical-recovery fallback.

The temporary extraction firmware is deliberately narrower than the eventual
custom dimmer firmware. Its only job is to boot safely, prove the inactive stock
application geometry, expose that application through a read-only unicast
protocol, and retain a separately proven route back to stock/custom firmware.

## Proven lineage and flash model

Historical same-lineage GLEDOPTO firmware plus the pinned Telink SDK establish
the TLSR8258/B85 512-KiB dual-bank architecture strongly enough for offline
implementation:

```text
0x00000..0x33FFF  bank A application region
0x34000...         NV1 / non-application storage
0x40000..0x73FFF  bank B application region
0x76000...         MAC/factory/user/NV areas in the historical 512K lineage
```

The recovered historical application was built almost to the `0x34000` ceiling
and contains the 512K-only upper-flash constants. That is strong platform
lineage evidence, **not permission to assume the installed 2024/2026 module has
identical silicon without a runtime/physical proof**.

The extraction core therefore fails closed unless runtime INFO matches exactly:

```text
flash size       0x80000
banks            0x00000 / 0x40000
executing marker 0x544C4E4B
old marker       0x544C4E00
old app size     >= 0x20 and < 0x34000
old app CRC      valid after virtual +0x08 reconstruction
```

Only the old application bytes are readable. Network keys, MAC, factory,
calibration and NV sectors are out of protocol scope.

## Gate 0 — offline container/CRC proof: PASS

Completed evidence includes:

- structurally valid historical GLEDOPTO Telink OTA;
- complete OTA sub-element enumeration;
- exact Telink xcrc32 convention: reflected polynomial, init `0xFFFFFFFF`, no
  final XOR;
- broad public corpus validation;
- no generic assumption that Zigbee outer identity must equal inner Telink
  identity;
- no assumption that manufacturer code `0x124F` identifies GLEDOPTO uniquely.

The normal stager OTA-index builder now additionally requires the target
`0x124F/0x1416`, version above stock, a boot-valid Telink inner image and the
exact GL-SD/hwVersion-2 metadata lock.

## Gate 1 — download-acceptance probe: IMPLEMENTED OFFLINE / LIVE NO-GO

`tools/make_ota_acceptance_probe.py` creates a structurally plausible target
container with a higher file version but deliberately invalid Telink CRC and
non-boot marker.

Pinned Telink SDK source shows the normal activation path validates the inactive
image before changing boot markers, so this probe is expected to fail before a
bank switch on that lineage. However the installed target's exact revision is
still gated. **The probe must not be served to production without a separate
supervisor authorization.**

The normal stager-index generator deliberately rejects this bad-CRC probe so it
cannot be accidentally promoted through the production packaging path.

## Gate 2 — read-only protocol/core/host: PASS OFFLINE

Protocol v1 uses private cluster `0xFC00`, endpoint 11 and only:

```text
PING
INFO
READ
ABORT
```

There is no flash write, erase, marker-change, reset, bind/group or network
management command. `STATUS=0x04` is reserved/unsupported, not part of the v1
wire surface.

READ payload data is capped at **48 bytes**. The live guarded host permits one
outstanding READ, persists a strictly increasing protocol sequence before
transmission and requires exact session/sequence/offset/length agreement before
persisting a response.

Host persistence also validates per-chunk CRC, SHA-256 journal records,
crash-ordering and complete final Telink application CRC after reconstructing
exactly the old bank marker byte.

## Gate 3 — coordinator/Z2M transport: PASS OFFLINE

Version-pinned integration is implemented for:

```text
Zigbee2MQTT               2.14.0
zigbee-herdsman            10.9.1
zigbee-herdsman-converters 26.103.0
```

The external Z2M extension and Python bridge are compile-time locked to the
exact production IEEE, endpoint 11 and cluster 0xFC00. The bridge exposes only
PING/INFO/READ/ABORT.

Two independent correlation layers are used:

1. herdsman's response waiter matches device/endpoint/cluster/response command
   and ZCL transaction sequence;
2. the guarded host matches protocol session/sequence/offset/length.

Synthetic end-to-end CI forces a dropped READ response, verifies the replacement
READ uses a higher persisted sequence, completes the dump, reconstructs only
byte +0x08 and validates the Telink CRC.

No production extension has been loaded and no production cluster command has
been sent.

## Gate 4 — radio adapter boundary: PASS SOURCE/NATIVE, TARGET COMPILE PENDING

The stack-independent transport adapter rejects before dispatch:

```text
non-unicast
wrong endpoint
wrong direction
```

The pinned Telink SDK gives the real handler the original `apsdeDataInd_t *` and
the `UNICAST_MSG()` helper, which explicitly excludes broadcasts and groupcast.
The handler also retains requester short address, endpoint, profile ID, ZCL TSN
and incoming APS-security state.

The thin Telink adapter therefore has enough source-level information to:

- call the pure dispatcher only for unicast client-to-server EP11 requests;
- send the response to the exact source address/endpoint/profile;
- reuse the incoming ZCL sequence;
- request APS ACK;
- preserve APS security where it was present.

Native GCC tests exercise the generic adapter. A build without
`GLSD_TELINK_SDK` compiles only a deliberate fail-closed stub. The real branch
must still be compiled by TC32 against the pinned TLSR8258 SDK.

## Gate 5 — bootable TC32 stager image: BLOCKED

Still required before a real OTA artifact exists:

1. reproducible TC32 compiler/toolchain with acceptable provenance;
2. required TLSR8258 low-level SDK support objects/headers;
3. enough exact installed-module silicon/flash/clock/RF/board facts to avoid
   guessing target configuration;
4. minimal target endpoint/simple-descriptor cluster set, including 0xFC00 and
   the separately designed return/recovery path;
5. target compile with `GLSD_TELINK_SDK`;
6. linker-map/symbol/address audit proving application fits its slot and does not
   reference flash write/erase or forbidden storage paths;
7. offline OTA packaging validation through `make_glsd_stager_index.py`.

The extraction adapter should preserve normal SDK watchdog behavior; it must not
add an unproven watchdog disable/feed scheme. READ work remains bounded to <=48
bytes per request.

## Gate 6 — return path before any bootable deployment: BLOCKING

A bootable stager must never be a one-way transition. Before serving one to the
production device, independently prove the return path. Possible mechanisms are
kept separate from extraction v1 because they involve mutation:

- an exact recovered stock OTA image; or
- a separately built/verified OTA-capable recovery image; or
- a canary-only transactional rollback mechanism whose marker writes and
  power-loss behavior have been fault-injection tested; plus
- SWS/spare-device recovery as the physical last resort.

The read-only extraction protocol itself intentionally contains no rollback or
marker-write command.

## Current decision

```text
OFFLINE_FORENSICS             PASS
HOST_GUARD                    PASS
Z2M_TRANSPORT                 PASS
SYNTHETIC_END_TO_END          PASS
GENERIC_RADIO_ADAPTER         PASS / native test required on latest head
TELINK_ADAPTER_SOURCE_PIN     PASS
TC32_TARGET_BUILD             BLOCKED
RETURN_PATH                   BLOCKED
LIVE_ACCEPTANCE_PROBE         NO_GO
LIVE_BOOTABLE_STAGER          NO_GO
PRODUCTION_DEVICE_MUTATION    NO_GO
```

## Hard invariants

1. No custom OTA is served to production without a new explicit supervisor gate.
2. No raw stock dump, Zigbee key, credential or unsanitized secret is committed.
3. Network-key/factory/MAC/calibration/NV sectors remain outside dump scope.
4. The old application bank is read-only during extraction.
5. Unknown target geometry/authentication/toolchain facts fail closed.
6. Return/recovery is independently proved before a bootable production stager.
