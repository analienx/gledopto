# Batch 7 final evidence — 2026-09-05

Tested head: `research/wireless-dump-stager` @ `fab638ba2c30ee72f38a526d0f666440272e9a0f`
(remote == frozen; CI runs 34022682128/34022623402 + 34022593113 (c64bb773) SUCCESS).
No implementation edits, no production extension load, no private 0xFC00, no custom OTA served,
no production-device mutation.

## Task 3 — independent byte-binding guard check (commit c64bb773)

Executor-built adversarial script (independent of the repo's own tests; same sidecar schema).
Results against `tools/glsd_release_plan.py` at the frozen head:

```text
1_EXACT_MATCH         : PLAN_BUILT attestation=True updateRequest=None authorization=False
                        preflightPass=False targetIeee=0xa4c13850cfcdb3a4
2_ONE_BYTE_FLIP       : BLOCKED -> SHA256_MISMATCH,SHA512_MISMATCH
3_TRUNCATED_1B        : BLOCKED -> SHA256_MISMATCH,SHA512_MISMATCH,SIZE_MISMATCH
4_EXTENDED_1B         : BLOCKED -> SHA256_MISMATCH,SHA512_MISMATCH,SIZE_MISMATCH
5_RENAMED_FILE        : BLOCKED -> FILENAME_MISMATCH
6_TAMPERED_SIDECAR256 : BLOCKED -> SHA256_MISMATCH
7_TAMPERED_SIDECARSZ  : BLOCKED -> SIZE_MISMATCH
FULL-EVIDENCE FLAGS   : preflightPass=True updateRequestTopic=…/ota_update/update
                        authorizationGranted=False mutatesFirmware=True
```

Conclusions:
- every single-byte/size/filename/sidecar mutation blocks release-plan construction
  BEFORE any plan object exists (ReleasePlanError);
- the exact matching candidate reaches only the existing preflight gate
  (updateRequest=None, authorizationGranted=False) while production blockers hold;
- even with ALL production-evidence flags supplied, the emitted update request
  targets ONLY `0xa4c13850cfcdb3a4` and `authorizationGranted` remains False —
  operator authorization is outside the tool by construction;
- repo's own suites on this head also green: test_glsd_release_plan 4 OK,
  test_glsd_flash_preflight 4 OK, test_glsd_stager_ota 7 OK.

## Task 1 — current-revision physical evidence hunt

Searches: GitHub code/issues (GL-SD-301P, 20651203+gledopto, 20851203, 28013001),
DuckDuckGo (bot-blocked), Bing (results unusable/unrelated), fccid.io (HTTP 403),
Hubitat search endpoint (JS-gated), gledopto.com product page (404 on guessed URL).

```text
EXACT_REVISION_PHYSICAL_EVIDENCE = NOT_FOUND in public sources
NEW_SOURCES_BEYOND_PRODUCTION-LINEAGE.md = none (Hub #3504 twin + Hubitat 2026
  field reports remain the strongest public evidence)
DISTINCTION maintained: no public source ties 20240704/20651203 or the
  20851203/0x28013001 revision to a MARKED Telink MCU or flash geometry;
  no public source justifies 512 KiB from imageType alone
GENERIC evidence exists only in the negative/positive firmware-lineage sense
  already recorded by the supervisor
```

## Task 4 — purchasable source clues

Live retail search was not reliably possible from this environment (search engines
blocked/unusable). Known actionable sources from existing evidence: Gledopto
official store + major marketplaces list GL-SD-301P; the Hubitat April-2026 thread
documents an independently purchased 2026 unit still shipping the same lineage
(one unit on 20651203). Any purchase should follow Stage-0 of
`SPARE-QUALIFICATION-PLAN.md`; no match is claimed until the Stage-1 tuple +
physical facts are observed on the actual device.

## Task 2 — spare qualification plan

Delivered as `SPARE-QUALIFICATION-PLAN.md` (this directory): Stage 0 purchase
screening → Stage 1 pre-OTA archive (tuple + full physical flash backup + JEDEC +
MCU markings) → Stage 2 stager OTA → Stage 3 extraction with **physical-backup
ground-truth comparison** → Stage 4 reconstructed-stock return with network
persistence — with per-step PASS/FAIL evidence, a Stage-1 geometry gate
(JEDEC 0x1460C8 / 512 KiB), and minimum spare⇒production matching conditions.
