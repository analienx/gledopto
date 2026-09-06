# Telink SDK adapter pin — wireless dump stager

Status: **real TC32 target objects compile; TLSR8258 bank-A/bank-B mechanics link; no live OTA authorization**.

Pinned public build inputs:

```text
SDK:       telink-semi/telink_zigbee_sdk tag V3.7.2.0
SDK commit d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e
MCU path:  TLSR8258 / B85
compiler:  tc32-elf-gcc (Telink TC32 version 2.0 build) 4.5.1.tc32-elf-1.5
toolchain archive sha256:
           33b854be3e3db3dba4b4dacdda2cd4ea1c94dfd4d562864a095956de7991b430
```

The public `boot_8258.link`, `cstartup_8258.S` and `link_cfg.S` are used directly.
The repository fixture supplies only fail-closed application configuration needed
to compile/link the public SDK; its sample board header is **not** evidence of the
GL-SD-301P production PCB.

## Implemented target files

```text
glsd_stager_core.[ch]                 read-only old-bank validator/reader
glsd_stager_dispatch.[ch]             PING/INFO/READ/ABORT protocol dispatcher
glsd_transport_adapter.[ch]            radio/request safety boundary
glsd_telink_sdk_adapter.[ch]           Telink ZCL binding
glsd_telink_stager_app.c               minimal router/EP11 application shell
glsd_telink_disabled_feature_glue.c    inert optional SDK hooks
telink_fixture/*                        mechanics-only SDK configuration
```

`tools/build_glsd_tc32_objects.sh` compiles all target translation units with the
real TC32 compiler. GitHub Actions currently requires:

```text
GLSD_TC32_OBJECT_COMPILE=PASS_6_OF_6
```

## Flash read primitive

The extraction core is given only:

```c
flash_read_page(address, length, dst)
```

There is deliberately no write/erase callback in `glsd_stager_env_t`.
The private extraction objects are audited through `tc32-elf-nm -u`; imports of
flash write/erase, NV reset/write, factory reset, leave or commissioning
primitives fail CI.

This statement is intentionally narrower than "the whole ELF is read-only".
The standard Telink OTA client remains a separate mutation-capable **recovery
subsystem** and necessarily contains flash erase/write code. Private cluster
commands cannot invoke it.

## Private 0xFC00 transport

The pinned SDK's `zclIncoming_t` provides raw command data plus profile, short
address, endpoints, sequence number, direction and APS-security state. The
Telink adapter normalizes those values into `glsd_transport_request_t`.

Before dispatch the transport requires:

```text
unicast
endpoint 11
client -> server direction
APS-secured request
known command
well-formed payload
```

Replies are unicast to the exact request source and preserve the incoming ZCL
sequence. APS ACK is enabled and APS security is retained.

The private extraction command set remains:

```text
PING
INFO
READ
ABORT
```

There is no write, erase, NV, reset, leave, binding or boot-marker command.

## Minimal application surface

The stager application advertises EP11 with:

```text
input:  Basic (0x0000), private extraction (0xFC00)
output: OTA client (0x0019)
profile: HA
```

It does not initialize the sample light, GPIO/PWM, button, LED, reporting,
binding or factory-reset application logic.

The application calls normal `zb_init()` / BDB restore paths so an already
commissioned router can reuse Zigbee NV. It contains **no network-steering call**;
a factory-new node therefore remains uncommissioned rather than silently joining
some network.

## OTA recovery is notify-driven only

`ota_init(OTA_TYPE_CLIENT, ...)` is retained so a recovery image can eventually
be supplied through standard Zigbee OTA. The stager intentionally does **not**
call `ota_queryStart()`.

Public Telink `ota_imageNotifyHandler()` directly issues Query Next Image after a
valid Image Notify. Therefore recovery can be explicitly initiated by the
coordinator while the stager avoids periodic OTA-server discovery/polling.

On a successfully validated OTA completion the normal SDK callback invokes
`ota_mcuReboot()`. This recovery path remains outside the private extraction
protocol and is not authorized for production use yet.

## Disabled TouchLink / Green Power / OTP hooks

The prebuilt router library retains application-owned TouchLink/Green Power hooks
even when those features are not exposed by this application. The stager supplies
fail-closed glue:

- TouchLink state disabled; registration returns unsupported;
- Green Power shared key/type are inert and device-announces are rejected;
- no optional TouchLink/GP endpoint is advertised.

Several public flash-vendor compatibility source files also carry unused OTP
wrappers. Their normal flash lock/unlock functions are required by the SDK, so
non-mutating generic OTP stubs allow those unused sections to link and then be
removed by `--gc-sections`. The final ELF gate requires:

```text
FINAL_OTP_SYMBOL_SCAN=NONE
```

If any OTP path survives GC, the build fails.

## Dual-bank link and physical geometry proof

`tools/build_glsd_tc32_link_probe.sh` links the same stager for:

```text
bank A: GLSD_STAGER_LINK_BASE=0x00000
bank B: GLSD_STAGER_LINK_BASE=0x40000
```

The public `link_cfg.S` maps this into `__FW_OFFSET`, consumed by
`boot_8258.link`. CI also compares `.text` VMAs and requires the bank-B delta to
be exactly `0x40000`, preventing a fake "bank B" build that merely changes a C
constant.

Physical-image gates are based on the finalized inner-image byte length:

```text
bank A must remain below 0x34000
bank B must remain below 0x74000
MAC region starts 0x76000
factory region starts 0x77000
512 KiB flash ends 0x80000
```

No generated Zigbee OTA container is produced by this link proof.

## Telink inner-image finalization

Raw TC32 linker output contains:

```text
FILE_VERSION at +0x02
raw 00 00 at +0x06
startup marker 4B 4E 4C 54 at +0x08
manufacturer at +0x12
image type at +0x14
raw linker size at +0x18
```

`tools/telink_app_finalize.py` implements the separate offline image-finalization
step used by this SDK lineage:

1. validate raw identity/marker/declared length;
2. pad body to 16-byte alignment when required;
3. write `5D 02` at +0x06;
4. patch declared size to include the trailing CRC;
5. append Telink xcrc32 (init `0xFFFFFFFF`, reflected polynomial, no final XOR);
6. re-validate identity, size and CRC.

It refuses malformed identity, unexpected raw magic, size mismatch,
double-finalization, bad CRC and slot overflow. CI exercises it on synthetic
regressions and on both real TC32-linked banks. The finalized binary remains a
**transient mechanics artifact**, not deployment authorization.

## Runtime extraction gates retained

```text
- runtime flash capacity must resolve to exactly 512 KiB
- stager base must be exactly 0x00000 or 0x40000
- executing stager bank marker must be 0x544C4E4B
- opposite old bank marker must be 0x544C4E00
- old bank must contain Telink 5D 02 identity
- old declared application size must be >=0x20 and <0x34000
- virtual restoration of old +0x08 marker must pass Telink xcrc32
- READ is relative only to validated old application bytes
- chunk length is 1..48
- no extraction access to MAC/NV/factory/calibration ranges
```

## Remaining live gates

Target compilation/linking is no longer the blocker. Before any live stager OTA,
we still need production-specific evidence that cannot be inferred safely from a
historical image or the public SDK:

1. exact 2024/2026 GL-SD-301P MCU/package and flash part/JEDEC or equivalent
   revision proof;
2. confidence that the production unit really uses the proven 512-KiB dual-bank
   layout and that the selected first OTA bank matches stock behavior;
3. a tested return path, preferably using a sacrificial matching spare and a
   recovered/reconstructed stock image;
4. final live target-lock/OTA-provider review and explicit authorization.

Until those gates are closed:

```text
LIVE_CUSTOM_OTA=NO_GO
PRODUCTION_DEVICE_MUTATION=NO_GO
```
