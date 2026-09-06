# GL-SD-301P flash-readiness gate

## Current supervisor freeze — 2026-09-06

Current branch head:

```text
ae810b888f4d2db9b7701a70370282309fa7a93f
```

Latest functional hardening commits:

```text
2b9a9842be3d8c60d1784dfbf0f8b06acbc45698  bind release plan to exact OTA bytes
c64bb773e180cff0170e1df79093ceea6b669700  adversarial byte-binding tests
```

Both the exact functional push and PR runs are green:

```text
run 34022589386 = SUCCESS (push)
run 34022593113 = SUCCESS (pull_request)
Python 3.11                         PASS
Python 3.14                         PASS
native GCC / cross-language        PASS
Z2M contract / synthetic E2E       PASS
TC32 target objects                PASS 6/6
TLSR8258 bank-neutral link         PASS
Telink finalizer / xcrc32          PASS
multi-address placement A/B        PASS
quarantined OTA wrapper            PASS
flash-preflight tests              PASS
exact-IEEE release-plan tests      PASS
exact OTA byte attestation         PASS
```

The release plan is now cryptographically bound to the actual local OTA file:
its file name, byte count, SHA-256 and SHA-512 must all match the quarantined
sidecar before a release plan can be constructed. Any content, hash, size or
filename mismatch fails closed. Even after all other preconditions pass,
`authorizationGranted` remains false.

## Critical architecture correction

The earlier mechanics experiment that separately linked bank A at `0x00000` and
bank B at `0x40000` is **superseded and must not be used for deployment**.

Telink's normal TLSR8258 Zigbee SDK uses hardware multi-address startup. The
normal application is linked once at logical `APP_IMAGE_ADDR = 0x00000`; the
same bytes may physically boot from `0x00000` or `0x40000`. Standard OTA writes
the logical-0 image into the inactive physical bank.

The current stager therefore:

- links exactly once at logical address `0x00000`;
- imports and calls `mcuBootAddrGet()` at runtime;
- accepts only physical boot base `0x00000` or `0x40000`;
- passes the detected physical base to the read-only extraction core;
- infers the old stock image as the opposite physical bank;
- does **not** require the operator to know the active bank before first OTA.

Current real TC32 image facts from the frozen functional build:

```text
raw bytes                       156724
finalized inner bytes           156740
logical .text VMA               0x00001670
physical A end exclusive        0x00026444
physical B end exclusive        0x00066444
bank A app-slot end             0x00034000
bank B app-slot end             0x00074000
inner xcrc32                    0x75eca6e2
inner SHA-256                   923d2aa70390931c3bd17cb8ec0d73285b27d964913cf5f84cd0c67cc73783c8
```

The generated quarantined Zigbee OTA from the same exact build was:

```text
bytes                            156806
manufacturerCode                 0x124F
imageType                        0x1416
fileVersion                      0x7F010001
hardwareVersionMin/Max           2 / 2
SHA-256                          1b05e890fdebf753f8d588286bd67c956fbd24e48c8df1eaa16fd6d4477b50f8
SHA-512                          36d4eb2be5f949160fd5c0dd9115a9263bffce9b94df80c9bd8feed9d1aabd2d66e172f108de25154c80273a8d6c544865d8be237cbf4bca3827db3c144a814c
```

These hashes are CI evidence for the frozen build, not a published release or
authorization to serve the file.

## Mutation-surface interpretation

The final router ELF necessarily contains normal Telink OTA/NV stack mutators,
including flash write/erase and NV write/reset routines. Their mere presence is
not treated as a failure because the standard OTA recovery subsystem needs
flash mutation.

The enforced safety boundary is narrower and testable:

```text
private GL-SD extraction/app object imports of flash write/erase/NV reset = NONE
periodic ota_queryStart import                                      = NONE
BDB network-steering import                                         = NONE
application light/PWM/factory-reset/steering symbols                = NONE
surviving OTP symbols                                                = NONE
standard notify-driven OTA recovery                                  = PRESENT
runtime mcuBootAddrGet                                                = PRESENT
```

Executor Batch 6 independently reproduced the frozen predecessor and confirmed
that the mutation-capable symbols present in the final router ELF are stack/OTA
infrastructure rather than imports of the private extraction path. The safety
claim is intentionally scoped to reachability/import surface, not to claiming
that a recoverable OTA-capable router binary contains zero write routines.

## Production serving path

Do not use a global Zigbee2MQTT OTA override index for the first production
transaction. Zigbee2MQTT 2.14.0 supports an explicit OTA source URL on a
per-device request. The release tooling therefore targets only:

```text
IEEE 0xa4c13850cfcdb3a4
```

with a per-device `{id, url}` check/update plan. `tools/glsd_release_plan.py`
never publishes MQTT. While any preflight blocker remains, it deliberately
omits the mutating update request entirely. It always leaves final operator
authorization false.

As of schema version 2, `glsd_release_plan.py` also requires `--candidate` and
re-hashes the exact local OTA bytes against the quarantine sidecar before it can
emit any plan.

Automatic OTA checks must remain disabled and no scheduled OTA may exist during
the eventual controlled transaction.

## Current evidence for the production lineage

Live target:

```text
model               GL-SD-301P
manufacturer        GLEDOPTO
swBuildId            20651203
dateCode             20240704
appVersion           1
stackVersion         2
hwVersion            2
OTA                   0x124F / 0x1416 / 0x26013001
```

Independent public evidence is now recorded in
`devices/gl-sd-301p/PRODUCTION-LINEAGE.md`.

Most importantly, an independent September 2024 GL-SD-301P reports the exact
same model/date/build/hw/OTA tuple, and 2026 field reports show both the same
`20651203` build and a later `20851203 / 0x28013001` build retaining the
`0x124F/0x1416` lineage. This makes the firmware-family correlation strong and
reduces the remaining uncertainty to the physical silicon/flash revision.

Historical 0x1416 binary forensics strongly identify the family as Telink
TC32/B85/TLSR8258 with a 512-KiB flash map. The current installed hardware has
not yet been physically marked/verified, so the production-MCU/revision
preflight intentionally remains fail-closed.

## Remaining blockers before a real write can be authorized

Only these substantive gates remain:

1. **Physical production revision corroboration** — exact current module MCU and
   512-KiB flash geometry must be established strongly enough to promote the
   TLSR8258F512ET32 lineage inference to target fact.
2. **Matching sacrificial spare** — obtain a matching GL-SD-301P, archive its
   complete stock flash first, then prove stager OTA -> extraction ->
   reconstructed-stock OTA -> normal operation and Zigbee persistence on that
   spare.
3. **Final live preflight** — re-read the target tuple immediately before the
   transaction, prove automatic checks disabled/no scheduled OTA, validate the
   exact candidate file/name/size/SHA-256/SHA-512 and require
   `glsd_flash_preflight.py` to pass.
4. **Explicit authorization** — a separate supervisor/user go/no-go after the
   above evidence. The tooling itself never grants authorization.

Current state:

```text
EXACT_PUBLIC_FIRMWARE_TWIN          = YES
FIRMWARE_LINEAGE_CORROBORATION      = STRONG
ACTIVE_BANK_REQUIRED_FOR_FIRST_OTA  = NO
BANK_NEUTRAL_STAGER                 = PASS
EXACT_IEEE_RELEASE_PATH             = PASS / OFFLINE
EXACT_CANDIDATE_BYTE_BINDING        = PASS / CI
PRODUCTION_MCU_REVISION             = OPEN / PHYSICAL FACT REQUIRED
RETURN_TO_STOCK_SPARE               = OPEN
FLASH_WRITE_PRECONDITIONS           = BLOCKED / FAIL-CLOSED
LIVE_CUSTOM_OTA                     = NO_GO
PRODUCTION_DEVICE_MUTATION          = NO_GO
```
