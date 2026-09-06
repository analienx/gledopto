# GL-SD-301P software freeze

## Authoritative pre-flash software state — 2026-09-06

This file is the authoritative software freeze for the wireless recovery/stager path.
Historical mechanics notes in `STATUS.md` that describe separately linking physical
bank A at `0x00000` and bank B at `0x40000` are superseded. The deployable model is
Telink bank-neutral multi-address startup: one logical-address-0 image, with
`mcuBootAddrGet()` determining whether the executing physical bank is `0x00000` or
`0x40000`.

Functional implementation freeze:

```text
FUNCTIONAL_SHA = 910389354a038693639d2895d0f69f14fc910b30
CI_RUN         = 34026590479
CI_RESULT      = SUCCESS
```

Independent executor verification:

```text
BATCH_8_EVIDENCE_COMMIT = b639262e44b3c32223d0d7e1e6f5f8911e462c6c
BATCH_8_ISSUE_COMMENT   = 5558919878
FULL_REGRESSION         = PASS
POLICY_BYPASS_MATRIX    = PASS
```

Batch 8 independently verified all of the following against the frozen functional
SHA:

- Python/Node/native regression passed with no WSL skips;
- direct installed-unit evidence mode fails closed unless all direct proof exists;
- exact-revision-spare inference mode fails closed unless the exact spare match,
  return-to-stock canary, MCU/flash facts, and explicit inference acceptance exist;
- conflicting direct/inferred evidence modes are rejected;
- live hwVersion or stock fileVersion drift blocks release planning;
- a one-byte OTA mutation blocks plan construction even when all hardware flags are
  otherwise satisfied;
- all generated check/update/abort requests are locked to IEEE
  `0xa4c13850cfcdb3a4`;
- no code path can set `authorizationGranted` / `AUTHORIZATION_GRANTED` to true;
- no global OTA override index is used;
- `FINAL-PREFLIGHT.md` matches the tested CLI and CI-produced candidate evidence.

Current software verdict:

```text
SOFTWARE_PATH                           = PRE-FLASH READY
BANK_NEUTRAL_FIRST_OTA                  = PASS
TC32_BUILD_AND_FINALIZER                = PASS
PRIVATE_EXTRACTION_READ_PATH            = PASS
NOTIFY_DRIVEN_STANDARD_OTA_RECOVERY     = PASS
EXACT_CANDIDATE_BYTE_BINDING            = PASS
EXACT_IEEE_RELEASE_PATH                 = PASS
DIRECT_HARDWARE_EVIDENCE_MODE           = PASS / TESTED
EXACT_REVISION_SPARE_INFERENCE_MODE     = PASS / TESTED
SOFTWARE_POLICY_BYPASS_REVIEW           = PASS / INDEPENDENT
```

No further software gate should be added without a concrete defect.

The only remaining prerequisites before any production write can be considered are:

1. matching-spare physical qualification;
2. full spare stager -> extraction -> reconstructed-stock -> normal-operation
   round trip, including wireless-extraction comparison against the physical stock
   backup;
3. immediately-before-write re-read of the installed target's frozen identity and
   OTA tuple, plus proof that automatic/scheduled/global OTA exposure is absent;
4. exact candidate re-attestation at the serving location;
5. separate explicit human go/no-go.

Current live state remains:

```text
MATCHING_SPARE_PHYSICAL_TEST   = NOT_RUN
RETURN_TO_STOCK_SPARE_TEST     = NOT_RUN
FINAL_LIVE_PREFLIGHT           = NOT_RUN
FINAL_OPERATOR_AUTHORIZATION   = NOT_GIVEN
LIVE_CUSTOM_OTA                = NO_GO
PRODUCTION_DEVICE_MUTATION     = NO_GO
```

See `FINAL-PREFLIGHT.md` for the executable Mode A / Mode B preflight contract and
`evidence/batch7-final-evidence-20260905/SPARE-QUALIFICATION-PLAN.md` for the
sacrificial-spare procedure.
