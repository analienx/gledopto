# Fresh Muse phase — GL-SD-301P R17–R23

## Outcome, authority and immutable starting context

Resolve findings R17–R23 from the attached independent review of
`69831aa5c9c230bb8c3c8074abd9c618eb936ed5`. Deliver a quarantined, independently
reviewable candidate with A17–A23 and final gate A24 satisfied; then stop with
**independent acceptance review pending**.

The user directly requested a new deep review, adjusted goals and Muse
implementation/resume unless all defects are fixed; they prefer a new session
when retained context is large. This packet transfers sole implementation
ownership from the completed predecessor to this new visible Muse session.
It authorizes bounded source changes, candidate branch/PR #8 updates,
GitHub-hosted validation and concise issue #1 evidence. Do not ask for the same
authorization again. Capability profile: EXPERT, scope WORKSPACE_IMPLEMENTATION;
profile never grants device or machine-mutation authority.

### Exact identities and paths

- Native workspace AND implementation checkout: `C:/Workspace/repos/gledopto-hardening`.
- Branch: `codex/glsd301p-client-hardening`; clean at the reviewed SHA.
- PR: https://github.com/analienx/gledopto/pull/8 (draft; update this PR).
- Control ledger: https://github.com/analienx/gledopto/issues/1.
- Frozen PR #6: `760c141925f29e517831c92afd61bd9c15e1b7b5`, unchanged.
- Predecessor session: `01a1017d-681f-7303-af97-8caf85983806`, native goal
  `goal-01a107db-b370-7662-96eb-d4eeb5a02035`, complete/100%.
- Predecessor immutable request/receipt: `C:/Workspace/scratch/gledopto-remediation-20261004-request-v2.json`
  and `C:/Workspace/scratch/gledopto-remediation-20261004-receipt-v2.json`.
- Main checkout `C:/Workspace/repos/gledopto` has unrelated dirty AGENTS.md and
  historical review/handoff files: do not edit or bulk-stage those.
- New review/brief are authored under that main checkout's
  `devices/gl-sd-301p/`; copy only the two dated 20261005 documents into this
  candidate when making the documentation checkpoint.

Do not replay/export the predecessor's 86 MB transcript. Its goal completed and
final hosted jobs below are complete; no active writer/effect is reported. On
entry reconcile exact current SHA, clean state, newest PR/issue and outstanding
jobs/owners. If actual state contradicts the packet, preserve it and resolve
ownership before mutation. Never overwrite another owner or bypass uncertainty
by starting yet another session.

## Policy, model and hard boundaries

Read current authenticated EXTERNAL_GITHUB `analienx/config/main`:
supervisor-executor SKILL, CAPABILITY_PROFILES, mutation-safety SKILL,
PROJECT_CONTEXT_ARTIFACT and projects registry. Last authenticated main was
`b0a91d01383ab663bc5e83d95a728fe9f7ab744c`; verify current main, don't treat it
as permanently frozen. Then read target AGENTS/manifest/project context and
device README/STATUS. Current local Muse skill, GOAL_BRIEFS and WORKFLOWS govern
work design, including fresh/retained selection. General config PR #66 at
`ac43a8281e2b4c0ad39cdada91273888c5b4595b` is still unmerged and not main authority.
Use the installed controller API, model and provider; no upgrade/reconfiguration.

Execution style **direct**: integrate coupled edits in the parent. One mandatory
canonical internal read-only verifier may inspect source/raw evidence, returning
criterion-mapped PASS/FAIL/PARTIAL. It cannot replace later Codex acceptance.
No redundant scouts, recursive self-launch or concurrent writers. No token budget.

Public repo: launcher `public_edit_only` means local inspection/authoring only.
Every firmware build/test/lint/verification/ABI probe/packaging/evidence execution
runs on GitHub-hosted Actions at the exact candidate. Never use local Windows,
WSL or self-hosted runners for validation. Controller/task metadata hashes are
coordination, not permission to validate firmware locally.

Editable: independently authored src/firmware, tests and host adapters, SDK patch
definitions and hash pins, hosted workflows/build gates, identity assertions and
hardening docs. Public SDK/reference material remains read-only and separate;
SDK pin `d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e` (V3.7.2.0).

**OUTPUT AUTHORITY: Identify and Trigger Effect must not energize, blink,
breathe, dim, restore or otherwise change the power stage or a normal transition.
Remove that unauthorized overlay. Keep only RAM commissioning state. Do not
reinterpret this as permission to implement physical identification; truthful
unsupported optional effects and documented physical limits are required.**

No merge/release/deployment, OTA upload/index/transfer/publication, live hardware,
HA/MQTT, bindings/coordinator, flash/reboot, device or credential action. Preserve
quarantine, recovery contracts and frozen PR #6. Machine changes follow the named
broker contract; shell, child and controller capabilities are not bypasses.

## Source-verified prior state to preserve

UART queue expiry/DMA deadlines/fault-OFF priority, readiness, static timers,
shared BDB rejoin ownership, elapsed interpolation, R8 cleanup, Groups complete
counted-name/exact Membership guards, ragged response/request guards and R12
per-record reporting status have substantive fixes. Preserve relevant AP/R1–R16
regressions; replace only expectations that encode R17–R23 defects or unauthorized
behavior. Current identity GLSD-ED-003 / APP_BUILD04 / FILE_VERSION0x7F040001 /
20261004 is consistent; changed bits must get another fresh unused dev identity.

Verified current hosted evidence (reuse for orientation, not next final SHA):

- Boundary https://github.com/analienx/gledopto/actions/runs/37224141144:
  AP dispatch PASS, 22-case matrix failed=0. This includes unauthorized power
  effects and does not prove new findings fixed.
- Clean readiness https://github.com/analienx/gledopto/actions/runs/37224141202
  and rebuild https://github.com/analienx/gledopto/actions/runs/37224256709:
  both exact 69831aa, successful, matching ELF/raw/final/MAP/OTA hashes.
- Final BIN SHA256 `3c59119658cdf2cd273e4a097530f6c7218e182b5f1c649ce3dcb4bb7ee9cf20`;
  OTA SHA256 `5af42998cfb8222f4a40d00e485158e8347d569cb41e0963a3b8a4c2ee477009`.

Opaque archives and physical behavior are still limits, not authority or proof.
Public authored parser defects are actionable; don't label them opaque.

## Acceptance matrix

| ID | Requirement | Observable oracle | Phase |
|---|---|---|---|
| A17 / R17 | Remove power-effect overlay and enforce output-neutral Identify/Trigger Effect. | Actual SDK → target-shared adapter; OFF/ON/transition/fault/PUSH cases, unsupported status and no altered state/timers/frames. No saved-output restoration survives. | M1/M2 |
| A18 / R18 | Accepted IdentifyTime writes start/restart at receipt time, including same value. | Actual foundation command/write observer: phase boundaries, equal value, mixed invalid records, no-response/undivided as supported, Query/Groups, wrap. No decrement charged before receipt. | M1/M2 |
| A19 / R19 | Identify catch-up is constant bounded work and respects elapsed time. | Deterministic operation-bound oracle for large gaps/zero/max countdown/wrap through real shared tick and IO sequence, preserving UART/input deadlines. | M1/M2 |
| A20 / R20 | Move to Level preserves accepted finite duration; only clipped Step scales. | Real SDK target/WithOnOff below-min finite/zero/reserved/gaps/wrap/RemainingTime, plus clipped/unclipped Step controls. | M1/M3 |
| A21 / R21 | True upward TARGET On/Off effect is applied at successful command admission. | Inspect mirror/runtime/queued frames immediately after dispatch BEFORE time advance; rejection/readiness/fault/equality/downward/override cases. | M1/M3 |
| A22 / R22 | Typed/count/status/direction grammar rejects malformed/unsupported records truthfully. | Real root/foundation fixtures from review plus valid flat/string/zero-data controls; hook/status/pool/table/NV effects; no imaginary scalar treatment for compound types. | M1/M3 |
| A23 / R23 | Target foundation ABI/pool thresholds and production adapter wiring are proved; current ledger is coherent. | Hosted TC32 layouts/offsets/threshold metadata in both flag contexts, actual host comparisons/exhaustion tests, shared production command/write callback, current checkpoint rows and source/evidence mapping. | M2/M4 |
| A24 | New candidate has fresh identity, complete exact-head validation and reproducibility handoff. | Fresh unused identity agrees across source/assertions/manifests; all hosted checks + two clean independent TC32/quarantine-OTA jobs at one final SHA; matched ELF/raw/final/MAP/OTA hashes and provenance, updated PR/issue, acceptance pending. | M4/M5 |

## Ordered milestones and exit evidence

### M0 — Ownership/bootstrap and focused checkpoint

Confirm packet facts/policies/branch/PR/issue and predecessor completion. Record
new session/native goal revision and sole-writer transfer in the current
`devices/gl-sd-301p/HARDENING-CHECKPOINT.md`. Bring in dated review/brief only.
Add R17–R23 and A17–A24, preserving historical records but eliminating conflicting
current next actions. Do not restart the original baseline survey or re-run all
expensive builds before changes are authored. Exit: verified ownership and scope.

### M1 — Actual-entry hosted negative controls

Author behavioral regressions with named failure assertions for reviewed 69831aa.
Use actual SDK root/cluster dispatch and target-used adapters; mocks capture
dependencies but do not invent missing state/write notifications. Old tests that
bless Blink/Breathe or check onset only after 150 ms must be replaced with the
authorized contract. Negative-control infrastructure/compile failures are not
reproductions. Use ephemeral hosted source workspaces, not edits to frozen refs.
R19 uses a deterministic work-bound observation, not an uncontrolled CI hang.
Record case→trigger→expected failure→job/step. Exit: confirmed defect cases
distinguish reviewed behavior from the intended fix; valid controls remain useful.

### M2 — Minimal output-neutral Identify (R17–R19, A23 adapter)

Delete power overlay, saved-state restore and effect preemption complexity.
Reject unsupported optional effects before mutations. Preserve RAM command,
Query and Groups state, and document the lack of physical identification.
Introduce an explicit accepted-write observation shared with target/harness;
respect per-record success/failure and cleanup. Use command/write receipt time
and O(1) elapsed whole-second calculation with residual phase and saturation.
Cover zero/maximum/equal writes, stop/restart and uint32 wrap. Do not add timers
unnecessarily or move safety service behind new unbounded work. Exit: A17–A19
and ordinary-command/transport regressions green on hosted runners.

### M3 — Level and typed protocol correctness (R20–R22)

Separate Step proportional duration from Move to Level finite duration; preserve
existing accepted/clamped target policy. Apply increasing TARGET onset at actual
successful admission, preserving equality/downward/fault/readiness/OFF invariants.
Exercise wire frames and immediate attribute state, not only final interpolation.

Define a bounded supported datatype grammar before unsafe SDK size helpers. Full
wire count width and supported compound headers must be validated; reject
unsupported nested forms truthfully rather than implementing unbounded recursion.
Differentiate valid zero-data types from unknown/invalid types. Check reporting
directions and long status-dependent records as well as existing short forms.
Preserve valid supported interoperable controls and per-record status cleanup.
Exit: A20–A22 and affected AP cases green with source/fixture/evidence mapping.

### M4 — Target ABI, identity and final seal

Produce required target-compiled foundation type sizes/offsets and checked
allocation/pool thresholds (SDK/app flags, packing and host harness comparison).
Use TC32 compiler/ELF metadata where target code cannot be executed; no fictional
on-device assertions. Document compiler-checked facts versus runtime/opaque limits.
Integrate checks with supported count/exhaustion regressions and actual shared
command/write adapter. Complete A23; do not merely repeat cluster/timer probes.

Allocate a fresh unused dev identity from source/history/issue/PRs and update
source/assertions/wrapper metadata together. Seal final source SHA only after
relevant hosted behavioral/ABI/policy checks are green. Obtain two independent
clean GitHub-hosted TC32 AND quarantine-wrapper jobs at that same SHA, with
SDK/toolchain/patch provenance, geometry/role/recovery gates and matching
ELF/raw/final/MAP/OTA hashes/sizes. Keep no-upload quarantine. Logs/metadata suffice.
If code changes, invalidate affected evidence and re-seal. Await existing jobs;
do not duplicate them. Exit: A23 and A24 raw evidence complete for final code.

### M5 — Honest handoff and stop

Reconcile each row against source and raw hosted results. If the canonical skill
requires an internal verifier, give one bounded read-only criterion/evidence
packet; correct actionable failures within scope. A child PASS is not Codex
acceptance. Update current checkpoint/findings/ingress, PR #8 body and concise
issue #1 result with final SHA, fix/case/run mapping, new identity, matching
artifacts and remaining limits. Preserve historical SHAs with explicit labels.

Final result must say **independent acceptance review pending**. Mark the native
implementation goal complete only when this full contract passes; then stop.
No extra feature, runtime/device check, merge, release or publication continuation.

## Resume/compaction and blockers

Checkpoint HEAD/native revision, criteria/evidence, outstanding CI/children with
owners, next action and authority at phase boundaries and before compaction or
external waits. After compaction re-anchor from checkpoint/current delta; don't
use `/new` or `/clear` to shrink this active phase. Reuse valid evidence within
contract, but required final exact-SHA checks still run. Keep progress concise.

Diagnose deterministic failure before retrying; bound unchanged transient external
operations to three attempts. A genuine blocker reports exact gate, safe attempts,
missing action and owner, preserves evidence and stops only affected work. Don't
evade policy/API rejections with another tool or executor. Don't silently broaden
scope to solve an unsupported feature or hardware dependency. No billed-cost
claim from native aggregate counters.

## Attached review

The controller's task snapshot appends the complete dated independent review
after this focused context packet. Read it before coding; raw evidence links
and exact source findings are preserved without copying the predecessor transcript.
