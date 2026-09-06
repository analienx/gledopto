# GL-SD-301P production-lineage evidence

## Purpose

This ledger separates **firmware-family corroboration** from the stronger
**physical silicon/flash proof** required before a production flash write.
Matching model/build/OTA tuples materially reduce lineage uncertainty, but they
do not by themselves prove the exact MCU package or flash density inside the
installed unit.

## Installed target tuple

```text
friendly name       LivingRoomCircleLightDimmer
model               GL-SD-301P
manufacturer        GLEDOPTO
endpoint            0x0B / 11
applicationVersion  1
stackVersion        2
hwVersion           2
dateCode            20240704
swBuildId            20651203
OTA manufacturer    0x124F / 4687
OTA imageType       0x1416 / 5142
OTA fileVersion     0x26013001 / 637612033
```

## Independent public exact-tuple twin — September 2024

Source:

- https://github.com/sprut/Hub/issues/3504

The independently reported GL-SD-301P has the same:

```text
model               GL-SD-301P
manufacturer        GLEDOPTO
endpoint            11
applicationVersion  1
stackVersion        2
hwVersion           2
dateCode            20240704
swBuildId            20651203
OTA manufacturer    0x124F
OTA imageType       0x1416
OTA fileVersion     0x26013001
```

It also reports endpoint 242 as `ZGP_PROXY_BASIC`, consistent with a Telink
router build carrying Green Power proxy support. This is strong evidence that
the installed target is not a one-off/custom firmware tuple.

Classification:

```text
EXACT_PUBLIC_FIRMWARE_TWIN = YES
EXACT_PUBLIC_HARDWARE_TUPLE = YES at Zigbee-exposed hwVersion level
EXACT_PHYSICAL_MCU_PACKAGE = NOT_PROVEN
EXACT_PHYSICAL_FLASH_DENSITY = NOT_PROVEN
```

## Independent 2026 field evidence — same shipping family

Sources:

- https://community.hubitat.com/t/re-release-tuya-zigbee-dimmer-module-w-healthstatus/120180?page=4
- https://community.hubitat.com/t/virtual-switch-control/163456/8

April 2026 field reports show GL-SD-301P units with endpoint `0x0B` and the same
GLEDOPTO firmware lineage. One independently purchased unit still reports
`softwareBuild=20651203`; another later unit reports:

```text
model               GL-SD-301P
manufacturer        GLEDOPTO
endpoint            0x0B
softwareBuild       20851203
OTA firmwareMT      124F-1416-28013001
```

This matters because `0x124F/0x1416` is not merely a single 2024 artifact: the
same image-type lineage persisted into later GL-SD-301P production.

Classification:

```text
MODEL_LINEAGE_PERSISTED_2024_TO_2026 = YES
IMAGE_TYPE_0x1416_PERSISTED           = YES
EXACT_2024_BUILD_STILL_SEEN_IN_2026   = YES
```

## What this does and does not close

The public evidence now supports the following pre-flash statement:

```text
PRODUCTION_FIRMWARE_LINEAGE_CORROBORATED = STRONG
PRODUCTION_MODEL_BUILD_TUPLE_CORROBORATED = STRONG
```

It does **not** justify asserting either of the following from Zigbee metadata
alone:

```text
PRODUCTION_MCU = TLSR8258
PRODUCTION_FLASH_SIZE = 0x80000
```

Those remain fail-closed in `glsd_flash_preflight.py` until corroborated by a
matching physical spare/current-revision teardown or an equivalently strong
source tied to this exact hardware revision.

## Current risk boundary

The remaining production uncertainty is therefore deliberately narrow:

1. physical MCU/package and 512-KiB flash geometry for the current hardware
   revision;
2. return-to-stock proof on a matching sacrificial spare;
3. final live tuple/scheduler/automatic-OTA/hash preflight;
4. explicit operator authorization.

No conclusion in this ledger authorizes serving or installing firmware.
