# GL-SD-301P client hardening — durable checkpoint

Branch: `codex/glsd301p-client-hardening` (from frozen `760c141925f29e517831c92afd61bd9c15e1b7b5`).
Control: `analienx/gledopto#1`. Goal dispatch brief sha256
`8cd622afbade2f42d7f4da9a14eeb1b96986258da60b53b6967ff17dabf0f33b` (verified).

## Authority (user, via managed goal dispatch 2026-10-03)

Authorized: bounded hardening implementation on the separate branch, hosted CI
runs at explicit candidate SHAs, one draft PR targeting main, concise issue-ledger
updates. Local work is inspection/authoring only (PUBLIC EDIT-ONLY).

NOT authorized: deployment, merge, release, live-device actions, OTA
transfer/publication, override index, HA mutation, factory reset/re-pair, binding
mutation, coordinator-wide change, unknown manufacturer writes. Green CI is not
live-device success. Frozen PR #6 branch/SHA must be preserved untouched.

## Bootstrap record (M1)

- Canonical `analienx/config` main at bootstrap: `b0a91d01383a` (2026-10-03).
  Skill 2.2 + capability profiles + mutation-safety + project-context artifact +
  registry all loaded authenticated.
- Registry `project_id`: `gledopto-gl-sd-301p`; execution `host_specialist`
  default / `foundry_isolated` optional; executor env `windows_notebook`.
- Repo visibility: public → GitHub-hosted Actions only for
  build/test/lint/verify/package/benchmark/evidence.
- Stale-metadata note: neither `supervisor/projects.yaml` (gledopto entry) nor
  local `.supervisor/project.yaml` declares `repository_visibility: public` /
  `executor.runtime: github_actions`. Execution follows the public rule
  regardless; host-specialist device lane preserved for separately authorized
  live work only. No registry edit attempted (separate canonical repo).
- Frozen baseline confirmed: PR #6 OPEN draft,
  `cleanroom/glsd301p-interoperability-20260907` at exactly
  `760c141925f29e517831c92afd61bd9c15e1b7b5`; checks green
  (runs 34276666901, 34276666846).
- Frozen artifacts: final 130436 `d6f41fb7…6551`; OTA 130502 `43e6996f…899b`;
  vendor recovery 208946 `16595a38…dd72`.
- Candidate branch did not exist; created from the exact frozen SHA in isolated
  worktree `C:\Workspace\repos\gledopto-hardening`. Main checkout untouched
  (one pre-existing untracked handoff doc left in place).
- Tuya `tuya-zigbee-switch#61` (merged) learnings reconciled: owned static
  events, accumulated elapsed ms across wrap/sleep, 5 s rejected-start pacing
  with SDK-owned backoff, read-only 0xFF10 health v1. Socket 60 s poll policy
  explicitly NOT imported (no evidence for this device); GLSD health is v2
  with the GLSD-specific layout from the brief.
- Dev identity pre-check: `GLSD-ED-002` / `APP_BUILD` 03 / `0x7F030001` /
  date 20261003 unallocated at baseline (current: `0x7F020001`). Formal
  allocation in M5.

## Source map (baseline tree)

- App/runtime: `src/` (runtime_core, output_guard, power_stage_policy,
  push_input, pb4_compat, uart_frame, uart_transport).
- Target: `firmware/glsd301p-ed/` (telink_target, inert glue, link sentinels,
  app/stack/version cfg, contracts).
- Host tests: `tests/test_glsd301p_*.c` (+ target-contract sh).
- Build/finalize: `tools/build_glsd301p_ed_tc32.sh`, `telink_app_finalize.py`,
  `make_glsd301p_ed_ota.py`, preflight/release-plan, cleanroom guard.
- Workflows: `cleanroom-guard.yml`, `glsd301p-final-readiness.yml`,
  `migrate-glsd301p-vendor-reference.yml`.
- Recovery: `devices/gl-sd-301p/vendor-firmware/` (OTA original + manifest).

## Status

- [x] M1 bootstrap + isolation + ledger mirror (this file + issue #1 comment)
- [ ] M1 source inventory read-through (in progress)
- [ ] M2 review + findings register + hosted reproductions
- [ ] M3 timer ownership / elapsed time / output safety
- [ ] M4 network recovery / protocol validation
- [ ] M5 health snapshot / decoder / dev identity
- [ ] M6 hosted validation + two clean rebuilds
- [ ] M7 draft PR + evidence + stop (**independent acceptance review pending**)

Next action: read baseline sources and build/CI lanes; open findings register.

---

# Remediation of independent review R1-R8 (goal d71ed0a9, brief sha256 `716420ff…560bb64` verified)

Reviewed candidate `7184eec40d8141ccd92a005d2d6d5ef0c2bcb853` (PR #8 draft,
quarantined) + old identity GLSD-ED-002/0x7F030001 preserved in the ledger;
frozen baseline `760c1419` / PR #6 untouched. Forward commits on the same
branch; no force push. Direct user authorization: "do full code review and
for the findings set new goals for muse" (bounded remediation + hosted
validation + PR/issue evidence; minimal mirror to issue #1 at M7).
PUBLIC EDIT-ONLY: local inspection/authoring only; all validation hosted.

## M0 bootstrap record (2026-10-03)

- Canonical `analienx/config` main `b0a91d01383a` (2026-10-03); skill 2.2,
  capability, mutation-safety, project-context, registry loaded authenticated.
- Main checkout `629a2c5` + user-authorized AGENTS.md standing-authorization
  edit + 4 untracked handoff/review artifacts (all preserved in place).
- Worktree `codex/glsd301p-client-hardening` at `7184eec`, clean except the
  same user-authorized AGENTS.md edit (committed with M0b, not unexplained).
- Review copied verbatim to `HARDENING-INDEPENDENT-REVIEW-20261003.md`
  (sha256 `7b783b25…bbe8b1`); R1–R8 added to `HARDENING-FINDINGS.md` as
  CONFIRMED with repro/fix/proof TBD; ingress/ownership map in
  `HARDENING-INGRESS-MAP.md` (public C vs opaque archives; audit limits).

## Status

- [x] M0/M0b bootstrap + evidence map
- [x] G1 UART fault boundary (R1,R2) — red 37142138291, fix green @ 6eb2bef (boundary 37142423473)
- [ ] G2 joined reconciliation (R3) — target-wired adapter + fix
- [ ] G3 elapsed-time transitions (R4) — repro + interpolation fix
- [ ] G4 protocol bounds (R6,R7,R8) + bounded follow-ups
- [ ] G5 exact-head coverage (R5) + claims + fresh identity
- [ ] M6 seal + full gates + two matching clean TC32 builds
- [ ] M7 PR #8 + issue #1 + stop (**independent acceptance review pending**)
