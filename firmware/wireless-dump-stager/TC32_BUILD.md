# TLSR8258 / TC32 target-build status

Status: **offline compile mechanics only; production board unresolved; no live OTA authorization**.

This document separates three things that must not be conflated:

1. a reproducible TC32 compiler + public Telink TLSR8258 SDK configuration;
2. compilation/link mechanics for the read-only GL-SD extraction sources;
3. a deployable GL-SD-301P application for the exact production hardware revision.

Only (1) and part of (2) are currently established.  (3) remains blocked.

## Batch 5 toolchain evidence

The executor independently acquired the Telink TC32 v2.0 compiler from the
Telink OSS distribution referenced by known Telink build tooling and recorded
its hash/provenance.  The reported compiler is:

```text
tc32-elf-gcc (Telink TC32 version 2.0 build) 4.5.1.tc32-elf-1.5
```

The executor also compiled 79+ objects from a public TLSR8258 sampleLight tree
and reached the real linker/symbol-resolution stage.  No device or OTA action
was performed.

## Public 8258 project configuration is not wholly missing

Batch 5 initially described the original 8258 Eclipse project configuration as
a blocker.  That statement is too broad.

The public Telink source contains the actual MCU/board/link mechanics required
to establish an 8258 compiler context.  In the pinned SDK lineage,
`apps/zigbee/sampleLight/app_cfg.h` explicitly maps:

```text
MCU_CORE_8258
  -> BOARD_8258_DONGLE (sample profile only)
  -> CLOCK_SYS_CLOCK_HZ 48000000
```

and includes the 8258 board header.  Telink's boot/link configuration also
exposes the application-offset and boot-image symbols used by the TC32 link,
including `__FW_OFFSET`, `__BOOT_LOADER_IMAGE`, and the RAM-code size gate.

The older public `V3.7.2.0` tag additionally contains the `build/tlsr_tc32`
Eclipse metadata and explicit 8258 sample board headers.  Therefore the lack of
a particular legacy IDE export is **not** a reason to guess target parameters.

### Critical restriction

`BOARD_8258_DONGLE` is **not** the GL-SD-301P board.  It is permitted only as a
compile/link mechanics fixture.  It does not authorize use of any sample GPIO,
PWM, LED, button, UART, zero-cross or power-stage initialization on the dimmer.

The exact production module/PCB revision and `POWER_STAGE_CONTROL` remain
unresolved deployment gates.

## GL-SD TC32 object harness

Use:

```bash
TELINK_SDK_ROOT=/path/to/tl_zigbee_sdk \
TC32_CC=/path/to/tc32-elf-gcc \
TC32_NM=/path/to/tc32-elf-nm \
bash tools/build_glsd_tc32_objects.sh
```

The harness:

- compiles objects only;
- defines `GLSD_TELINK_SDK` and `MCU_CORE_8258`;
- applies the legacy TC32 `size_t` guard macros identified in Batch 5;
- uses the public sampleLight `app_cfg.h` only to supply a known-good SDK header
  context;
- records compiler version, checkout SHA when available, object SHA-256 values
  and undefined symbols;
- does not link an application;
- does not generate an OTA container;
- does not access Zigbee or a device.

A successful result must end with:

```text
GLSD_TC32_OBJECT_COMPILE=PASS_4_OF_4
```

Anything else is a compile failure to be returned to the supervisor.  Do not
patch around failures in an executor evidence pass.

## Adapter header ordering

The real Telink application pattern is:

```c
#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
/* application headers afterwards */
```

`tl_common.h` itself imports `app_cfg.h` before platform/compiler headers.  The
GL-SD adapter now follows this order before including portable `glsd_*` headers.
This is necessary for the legacy TC32 type/compiler-attribute environment and
matches the Batch 5 failure diagnosis.

## Link experiment boundary

An offline link experiment may use Telink's public 8258 sample project solely to
prove:

- compiler/linker flags;
- bank A/B `__FW_OFFSET` mechanics;
- required SDK libraries;
- section placement;
- undefined-symbol closure;
- map-file inspection.

It must not be called a production GL-SD build, and it must not introduce sample
board I/O into GL-SD sources.

Before any future deployable build, the supervisor still requires at minimum:

```text
PRODUCTION_MCU_EXACT             proven or runtime-fail-closed
PRODUCTION_FLASH_GEOMETRY        proven on exact revision
POWER_STAGE_CONTROL              proven
PRODUCTION_BOARD_INIT            explicitly designed from evidence
NETWORK/NV_PRESERVATION          proven in final link/config
STANDARD_OTA_RECOVERY_CHANNEL    included and tested offline
FORBIDDEN_WRITE_SYMBOL_AUDIT     pass for extraction path
TARGET_LINK_MAP                  reviewed
STAGER_OTA_CONTAINER             separately reviewed
LIVE_CUSTOM_OTA                  explicit later authorization
```

Current gate remains:

```text
TC32_OBJECT_BUILD_MECHANICS = IMPLEMENTED / VERIFICATION REQUIRED
TARGET_LINK_MECHANICS       = PARTIAL / OFFLINE ONLY
PRODUCTION_TARGET_BUILD     = BLOCKED
LIVE_CUSTOM_OTA             = NO_GO
PRODUCTION_DEVICE_MUTATION  = NO_GO
```
