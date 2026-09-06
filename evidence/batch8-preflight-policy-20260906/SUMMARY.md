# Batch 8 final preflash policy verification — 2026-09-06

Functional SHA tested: `910389354a038693639d2895d0f69f14fc910b30` (detached worktree, clean).
Remote/doc-only head: `657b863bc678b4472b1deb05d4a2cc7494bc2e88` (adds FINAL-PREFLIGHT.md only — reviewed separately).
CI run 34026590479 on 9103893: SUCCESS (all three jobs incl. pinned TC32 bank-neutral link proof).

## Full regression on 9103893 (Windows Python 3.14.4 + node v24.18.0; WSL gcc 15.2.0)

9 Python suites × 2 environments + node contract: ALL exit 0.
WSL totals (no skips, native executed): 12 + 11 + 1 + 3 + 3 + 11 + 6 + 7 + 7 = 61 tests OK.
(node: `glsd_wireless_dump_contract: PASS`; `node --check` OK.)

## Independent A–K policy matrix (executor-built script, not the repo's tests)

```text
A_defaults           : FAIL blockers=[PRODUCTION_FLASH_GEOMETRY_UNPROVEN, PRODUCTION_MCU_UNPROVEN,
                                PRODUCTION_REVISION_UNPROVEN, RETURN_TO_STOCK_SPARE_NOT_PASSED] upd=None
B_direct_no_revision : FAIL blockers=[PRODUCTION_REVISION_UNPROVEN, RETURN_TO_STOCK_SPARE_NOT_PASSED] upd=None
C_direct_full        : PASS direct=True inferred=False auth=False upd=ieee:0xa4c13850cfcdb3a4
D_spare_no_accept    : FAIL blockers=[SPARE_GEOMETRY_INFERENCE_NOT_ACCEPTED] upd=None
E_spare_no_match     : FAIL blockers=[EXACT_REVISION_SPARE_MATCH_NOT_PASSED] upd=None
F_spare_no_return    : FAIL blockers=[RETURN_TO_STOCK_SPARE_NOT_PASSED] upd=None
G_spare_wrong_mcu    : FAIL blockers=[PRODUCTION_MCU_UNPROVEN] upd=None
H_spare_wrong_flash  : FAIL blockers=[PRODUCTION_FLASH_GEOMETRY_UNPROVEN] upd=None
I1_spare_hw_drift    : FAIL blockers=[LIVE_HW_VERSION_MISMATCH] upd=None
I2_spare_cfv_drift   : FAIL blockers=[LIVE_STOCK_VERSION_UNEXPECTED] upd=None
J_spare_full_valid   : PASS direct=False inferred=True accepted=True auth=False upd=ieee:0xa4c13850cfcdb3a4
K_spare_plus_direct  : FAIL blockers=[CONFLICTING_HARDWARE_EVIDENCE_MODE] direct stays False (no silent upgrade)
C_MUTATED (1 byte)   : RAISED -> SHA256_MISMATCH,SHA512_MISMATCH (before any plan exists, full hw flags)
J_MUTATED (1 byte)   : RAISED -> SHA256_MISMATCH,SHA512_MISMATCH (before any plan exists, full hw flags)
```

All 11 cases match the supervisor's expected outcomes exactly, including that
inference never masquerades as direct proof (K keeps `directProductionGeometryProven=false`).

## Impossibility probes (source + runtime)

- `authorizationGranted` / `AUTHORIZATION_GRANTED` exist only as literal `False`
  (glsd_release_plan.py:143, glsd_flash_preflight.py:181) — no code path sets True.
- `TARGET_IEEE = "0xa4c13850cfcdb3a4"` is a module constant used in every request payload and the plan;
  no parameter can change it.
- `usesGlobalOverrideIndex` is a literal `False` (glsd_release_plan.py:140).
- Exact candidate name/size/SHA-256/SHA-512 binding is mandatory before any plan object exists
  (proven again under full hardware flags in C_MUTATED/J_MUTATED).
- schemaVersion 3 propagates `hardwareEvidenceSource` into every plan.

## Doc consistency (FINAL-PREFLIGHT.md @ 657b863)

- Mode-A command and Mode-B command match the actual CLI flags on 9103893.
- The Mode-B required output fields match my J-run exactly
  (PASS / direct=false / inferred=true / accepted=true / AUTHORIZATION_GRANTED=false).
- The exact-binding section matches Batch-7/8 adversarial results.
- The doc's quarantined CI evidence (`bytes 156806`, sha256 `1b05e890fdebf753…`) matches the actual
  CI log of run 34026590479 (TC32 job sidecar print) — verified directly.
- No mismatch found between documented requirements and code behavior.
