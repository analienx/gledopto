# GL-SD-301P wireless stock-firmware recovery design

Status: **host + target implementation complete offline through real TC32 dual-bank link/finalization; NO LIVE CUSTOM OTA AUTHORIZED**

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
separately validated recovery route.

The temporary extraction firmware is deliberately narrower than the eventual
custom dimmer firmware. Its only job is to boot on the proven Telink lineage,
prove the inactive stock application geometry, expose only that application
through a read-only unicast protocol, and retain standard Zigbee OTA as a
separate recovery channel.

## Proven lineage and flash model

Historical same-lineage GLEDOPTO firmware plus the public Telink SDK establish
the TLSR8258/B85 512-KiB dual-bank architecture strongly enough for offline
implementation and real target compilation:

```text
0x00000..0x33FFF  bank A application region
0x34000...         NV / non-application storage
0x40000..0x73FFF  bank B application region
0x76000...         MAC region in historical 512K lineage
0x77000...         factory region in historical 512K lineage
0x80000            512-KiB flash end
```

The historical application contains 512K-only upper-flash constants. This is
strong lineage evidence, **not permission to assume the installed 2024/2026
module is the exact same physical revision**.

The extraction core therefore fails closed at runtime unless:

```text
flash size       exactly 0x80000
stager base      exactly 0x00000 or 0x40000
executing marker 0x544C4E4B
old marker       0x544C4E00
old app size     >= 0x20 and < 0x34000
old app CRC      valid after virtual +0x08 reconstruction
```

Only old-application bytes are readable. MAC, network/factory/calibration/NV
sectors are outside protocol scope.

## Gate 0 — offline OTA/container/CRC forensics: PASS

Established:

- historical GLEDOPTO image is a structurally valid Zigbee OTA with a complete
  upgrade-image sub-element consuming to EOF;
- Telink xcrc32 is reflected CRC32, init `0xFFFFFFFF`, no final XOR;
- inner Telink identity is not treated as a universal copy of outer Zigbee OTA
  identity;
- manufacturer code `0x124F` is not treated as uniquely GLEDOPTO;
- normal stager index generation is target locked and rejects malformed or
  deliberately bad-CRC probes.

## Gate 1 — invalid-CRC acceptance probe: QUARANTINED / LIVE NO-GO

`tools/make_ota_acceptance_probe.py` remains an offline diagnostic artifact. It
must not be served to the production target. The normal stager-index builder
rejects it by design.

The live acceptance question is no longer a build prerequisite; it is part of
the later spare-device/live-transition evidence gate.

## Gate 2 — read-only protocol/core/host: PASS OFFLINE

Protocol v1 uses private cluster `0xFC00`, endpoint 11 and only:

```text
PING
INFO
READ
ABORT
```

`STATUS=0x04` is reserved/unsupported. There is no private command for flash
write/erase, OTA, boot-marker change, reset, leave, bind/group or network
management.

READ data is capped at 48 bytes. The guarded host permits one outstanding READ,
persists a strictly increasing application sequence before transmission and
requires exact session/sequence/offset/length agreement before accepting a
response.

Persistence validates chunk CRC, SHA-256 journal state, crash ordering and the
complete reconstructed stock Telink CRC before finalization.

## Gate 3 — coordinator/Zigbee2MQTT transport: PASS OFFLINE

Pinned integration contract:

```text
Zigbee2MQTT                2.14.0
zigbee-herdsman            10.9.1
zigbee-herdsman-converters 26.103.0
```

The external extension and Python bridge are compile-time locked to the exact
production IEEE, endpoint 11 and cluster `0xFC00` and expose only
PING/INFO/READ/ABORT.

Correlation is independent at two layers:

1. herdsman waits for exact device/endpoint/cluster/response command/ZCL TSN;
2. guarded host validates protocol session/sequence/offset/length.

Synthetic end-to-end CI forces a lost READ response, verifies the retry gets a
fresh persisted sequence, completes reconstruction and checks final stock CRC.

No production extension has been loaded and no production private-cluster
command has been sent.

## Gate 4 — Telink radio/application adapter: PASS TARGET COMPILE

The stack-independent transport rejects before dispatch:

```text
non-unicast
wrong endpoint
wrong direction
unsecured APS request
```

The real Telink adapter compiles under the official TC32 toolchain and uses the
SDK's actual `zclIncoming_t`/`apsdeDataInd_t` metadata. Replies return to the
exact requester endpoint/profile with the same ZCL sequence and APS security.

The minimal stager application advertises EP11:

```text
input:  Basic 0x0000, private extraction 0xFC00
output: OTA client 0x0019
```

It does not initialize sample-light LED, button, PWM, power-stage, reporting,
binding, factory-reset or network-steering application logic.

## Gate 5 — real TLSR8258 TC32 build + dual-bank geometry: PASS OFFLINE

GitHub Actions now uses:

```text
SDK        telink-semi/telink_zigbee_sdk V3.7.2.0
compiler   tc32-elf-gcc 4.5.1.tc32-elf-1.5
toolchain  SHA256 33b854be3e3db3dba4b4dacdda2cd4ea1c94dfd4d562864a095956de7991b430
```

All six target translation units compile with the real TC32 compiler. The
stager links against the public TLSR8258 startup/linker and router library for
both banks.

Validated mechanics geometry:

```text
BANK A
  base                     0x00000
  raw bytes                156692
  finalized inner bytes    156708
  physical end exclusive   0x26424
  slot end                 0x34000
  .text VMA                0x00001670
  Telink xcrc32             0x9EB539EB

BANK B
  base                     0x40000
  raw bytes                156692
  finalized inner bytes    156708
  physical end exclusive   0x66424
  slot end                 0x74000
  .text VMA                0x00041670
  Telink xcrc32             0x1CD95E73

.text delta                0x40000
reserved geometry           PASS
```

The `.text` delta is checked explicitly, proving bank B is genuinely linked
through Telink `__FW_OFFSET=0x40000`, not merely tagged with a different C
constant.

Final ELF/build safety gates require:

```text
FINAL_OTP_SYMBOL_SCAN           NONE
APPLICATION_POWER_STAGE_SCAN    NONE
APPLICATION_RESET/STEERING_SCAN NONE
PRIVATE_MUTATION_IMPORT_SCAN    NONE
```

The whole ELF is intentionally **not** described as write-free: standard Telink
OTA recovery is a separate mutation-capable subsystem. The private extraction
surface cannot invoke it.

## Gate 5A — Telink inner-image finalization: PASS OFFLINE

Real TC32 output proved the linker leaves `00 00` at offsets +6..+7. The
lineage's post-link mechanics are implemented in `tools/telink_app_finalize.py`:

1. validate raw identity, marker and linker-declared size;
2. accept only raw `00 00` (or already populated `5D 02`) at +6;
3. pad the body to 16-byte alignment;
4. write TLSR8258 magic `5D 02`;
5. patch declared size to include the trailing CRC;
6. append Telink xcrc32;
7. revalidate identity, size, slot limit and CRC.

The finalizer runs in Python 3.11 and 3.14 regression CI and against both real
TC32-linked banks. It creates only an inner Telink application binary; the TC32
proof **does not create or serve a Zigbee OTA container**.

## Gate 5B — recovery channel: IMPLEMENTED OFFLINE / NOT LIVE-AUTHORIZED

The minimal stager retains Telink's standard OTA client solely as the recovery
channel. It deliberately never calls `ota_queryStart()`.

Public Telink source proves an incoming Image Notify directly causes Query Next
Image, so recovery can be coordinator-initiated when explicitly needed without
periodic OTA-server discovery/polling. On successful validated OTA completion,
the normal SDK callback reboots through `ota_mcuReboot()`.

This recovery path is separate from private extraction and remains untested on a
matching spare.

## Gate 6 — production revision + return path: BLOCKING

The remaining blockers are physical/production-specific, not missing host or
compiler work:

1. identify the exact 2024/2026 GL-SD-301P MCU/package/module and flash part or
   JEDEC on a matching spare;
2. confirm the actual unit uses the 512-KiB dual-bank map and determine which
   bank stock OTA writes first;
3. obtain the stock application from the spare and validate reconstruction;
4. construct the return-to-stock image from that recovered stock app;
5. perform stock -> stager -> dump -> reconstructed-stock recovery on the spare,
   including reboot/power-loss and Zigbee NV/network-preservation checks;
6. only then review the exact target-locked OTA provider and decide whether a
   production live gate can open.

The read-only extraction protocol itself intentionally contains no rollback or
marker-write command.

## Current decision

```text
OFFLINE_FORENSICS              PASS
HOST_GUARD                     PASS
Z2M_TRANSPORT                  PASS
SYNTHETIC_END_TO_END           PASS
TELINK_TARGET_COMPILE          PASS 6/6
TC32_BANK_A_LINK               PASS
TC32_BANK_B_LINK               PASS
TELINK_INNER_FINALIZER         PASS
RESERVED_FLASH_GEOMETRY        PASS
NOTIFY_DRIVEN_RECOVERY_DESIGN  PASS SOURCE/OFFLINE
PRODUCTION_REVISION            OPEN
SPARE_RETURN_PATH              OPEN / BLOCKING
LIVE_ACCEPTANCE_PROBE          NO_GO
LIVE_BOOTABLE_STAGER           NO_GO
PRODUCTION_DEVICE_MUTATION     NO_GO
```

## Hard invariants

1. No custom OTA is served to production without a new explicit supervisor gate.
2. No stock dump, Zigbee key, credential or unsanitized secret is committed.
3. Network/factory/MAC/calibration/NV sectors remain outside dump scope.
4. The old application bank is read-only during extraction.
5. Unknown production geometry/revision facts fail closed.
6. Return/recovery must be proved on a matching sacrificial spare before a
   bootable production stager is considered.
