# GL-SD-301P final production preflight

## Functional freeze

The current supervisor-owned functional preflight/release implementation is:

```text
FUNCTIONAL_SHA = 910389354a038693639d2895d0f69f14fc910b30
CI_RUN         = 34026590479
CI_RESULT      = SUCCESS
```

All three jobs pass on that SHA:

```text
Python 3.11 + native GCC + Z2M contract                    PASS
Python 3.14 + native GCC + Z2M contract                    PASS
Pinned Telink TC32 + TLSR8258 bank-neutral link proof      PASS
```

This SHA adds an explicit distinction between **direct installed-unit hardware
proof** and **exact-revision-spare hardware inference**. Neither path grants
final authorization and neither tool publishes MQTT.

## Target frozen identity

```text
IEEE                0xa4c13850cfcdb3a4
modelId             GL-SD-301P
manufacturerName    GLEDOPTO
swBuildId            20651203
dateCode             20240704
hwVersion            2
OTA manufacturer     0x124F
OTA imageType        0x1416
OTA fileVersion      0x26013001
```

The stager remains a single logical-address-0 TLSR8258 multi-address image. The
first OTA does not require advance knowledge of which physical bank is active;
`mcuBootAddrGet()` is used at runtime and only physical bases `0x00000` and
`0x40000` are accepted.

## Hardware evidence mode A — strongest: `installed-direct`

Use this only if the installed target itself has been physically inspected and
the production facts are directly established:

```text
MCU family          TLSR8258
flash size          0x80000 / 512 KiB
revision evidence   direct installed-unit evidence
```

A matching spare still must pass the return-to-stock canary before production.

Offline preflight shape:

```bash
python tools/glsd_flash_preflight.py CANDIDATE.ota.quarantine.json \
  --production-mcu TLSR8258 \
  --production-flash-size 0x80000 \
  --production-revision-proven \
  --hardware-evidence-source installed-direct \
  --return-to-stock-spare-passed
```

A PASS is only a precondition result. `AUTHORIZATION_GRANTED` remains false.

## Hardware evidence mode B — wireless objective: `exact-revision-spare`

This path deliberately avoids opening the installed dimmer. It is allowed only
when a sacrificial spare satisfies **all** of the following:

1. complete live revision tuple matches the installed target exactly:
   `GL-SD-301P / GLEDOPTO / 20651203 / 20240704 / hwVersion 2 /
   0x124F:0x1416:0x26013001`;
2. endpoint/cluster fingerprint matches the archived production fingerprint;
3. legible physical MCU marking identifies the expected TLSR8258 family;
4. physical programmer evidence establishes 512-KiB flash geometry and the
   expected JEDEC profile; the complete pre-OTA spare flash is archived and
   cryptographically hashed;
5. the bank extracted wirelessly by the stager matches the corresponding bank
   from the physical spare backup, modulo the intentionally invalidated boot
   marker mechanics;
6. reconstructed-stock OTA returns the spare to normal stock operation;
7. Zigbee network identity/credentials, binds/reporting and normal operation
   survive the round trip without re-pairing;
8. the operator separately accepts the residual risk that a vendor could have
   shipped a different physical BOM under otherwise identical visible revision
   identifiers.

Only after 1–7 are evidenced may the following offline mode be considered:

```bash
python tools/glsd_flash_preflight.py CANDIDATE.ota.quarantine.json \
  --production-mcu TLSR8258 \
  --production-flash-size 0x80000 \
  --hardware-evidence-source exact-revision-spare \
  --exact-revision-spare-match-passed \
  --return-to-stock-spare-passed \
  --accept-spare-inference-for-production
```

The result must explicitly show:

```text
FLASH_WRITE_PRECONDITIONS_PASS                    = true
directProductionGeometryProven                    = false
productionGeometryInferredFromExactRevisionSpare  = true
spareInferenceAccepted                            = true
AUTHORIZATION_GRANTED                             = false
```

This is intentionally different from claiming the installed unit was directly
inspected.

## Exact candidate binding

The release planner requires the actual local OTA bytes. It rechecks file name,
byte count, SHA-256 and SHA-512 against the quarantined sidecar before any plan
can exist. Batch-7 independent adversarial testing confirmed that a one-byte
flip, truncation, extension, rename, sidecar SHA tamper or sidecar-size tamper
all fail closed.

Current quarantined stager evidence from the unchanged TC32 firmware build:

```text
outer bytes       156806
manufacturerCode  0x124F
imageType         0x1416
fileVersion       0x7F010001
hw min/max        2 / 2
SHA-256           1b05e890fdebf753f8d588286bd67c956fbd24e48c8df1eaa16fd6d4477b50f8
SHA-512           36d4eb2be5f949160fd5c0dd9115a9263bffce9b94df80c9bd8feed9d1aabd2d66e172f108de25154c80273a8d6c544865d8be237cbf4bca3827db3c144a814c
```

These hashes identify CI evidence; they do not authorize serving the artifact.

## Release-plan generation remains non-executing

After a hardware preflight passes, the exact candidate can be bound to the
single production IEEE in a non-executing plan:

```bash
python tools/glsd_release_plan.py CANDIDATE.ota.quarantine.json \
  --candidate CANDIDATE.ota \
  --url https://ONE-USE-LOCATION/CANDIDATE.ota \
  --production-mcu TLSR8258 \
  --production-flash-size 0x80000 \
  --hardware-evidence-source exact-revision-spare \
  --exact-revision-spare-match-passed \
  --return-to-stock-spare-passed \
  --accept-spare-inference-for-production \
  --out release-plan.json
```

The generated plan must satisfy all of these:

```text
targetIeee                         0xa4c13850cfcdb3a4
usesGlobalOverrideIndex            false
candidateByteAttestation           matchesQuarantineSidecar=true
preflight                          FLASH_WRITE_PRECONDITIONS_PASS=true
updateRequest.payload.id            0xa4c13850cfcdb3a4
authorizationGranted               false
```

The tool does not publish the generated request.

## Immediately-before-write live gate

Even after the spare path passes, re-read the installed target immediately
before a production transaction and require the frozen identity to be unchanged:

```text
modelId             GL-SD-301P
manufacturerName    GLEDOPTO
swBuildId            20651203
dateCode             20240704
hwVersion            2
OTA                  0x124F:0x1416:0x26013001
```

Also prove at that time:

- Zigbee2MQTT automatic OTA checks remain disabled;
- no scheduled OTA exists;
- the one-use candidate URL resolves to the exact bytes already attested;
- the candidate hashes match the frozen sidecar;
- no global override index exposes the candidate;
- the update target is exactly `0xa4c13850cfcdb3a4`;
- a separate explicit human go/no-go is recorded after reviewing the live
  preflight output.

## Current state

```text
SOFTWARE_BUILD_AND_RECOVERY_PATH        = PASS
BANK_NEUTRAL_FIRST_OTA                  = PASS
EXACT_IEEE_RELEASE_PATH                 = PASS
EXACT_CANDIDATE_BYTE_BINDING            = PASS
DIRECT_INSTALLED_HARDWARE_MODE          = IMPLEMENTED / TESTED
EXACT_REVISION_SPARE_INFERENCE_MODE     = IMPLEMENTED / TESTED
MATCHING_SPARE_PHYSICAL_QUALIFICATION   = NOT_RUN
MATCHING_SPARE_RETURN_TO_STOCK          = NOT_RUN
SPARE_INFERENCE_RISK_ACCEPTANCE         = NOT_GIVEN
FINAL_LIVE_PREFLIGHT                    = NOT_RUN
FINAL_OPERATOR_AUTHORIZATION            = NOT_GIVEN
LIVE_CUSTOM_OTA                         = NO_GO
```

The practical remaining work is now concentrated in the sacrificial spare. If
an exact-revision spare passes the full physical + round-trip qualification, the
installed unit no longer needs to be opened merely to satisfy the software
preflight policy; the residual inference is surfaced explicitly instead of
being mislabeled as direct proof.
