# Independent hardening review — 2026-10-05

## Decision and reviewed inputs

**Changes requested. Independent acceptance remains pending.**

Reviewed `69831aa5c9c230bb8c3c8074abd9c618eb936ed5`, draft
[PR #8](https://github.com/analienx/gledopto/pull/8), clean implementation checkout
`C:/Workspace/repos/gledopto-hardening`, branch
`codex/glsd301p-client-hardening`. Frozen PR #6 remains
`760c141925f29e517831c92afd61bd9c15e1b7b5`.

Reviewed the full delta from `bc7196f`, runtime and target integration, new
Identify module, SDK patch definitions and matching pinned public SDK bodies,
adverse-input tests, production-entry harness, target build gates, source/evidence
ledgers and raw hosted logs. SDK pin is
`d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e` (Telink V3.7.2.0).
Canonical config main was authenticated at
`b0a91d01383ab663bc5e83d95a728fe9f7ab744c`.

No local firmware builds/tests/lint/packaging were run. New triggers below are
source-traced findings; executed adverse-behavior reproductions remain assigned
to Muse on GitHub-hosted Actions. Physical output, live-network and flash success
remain unverified. Reference SDK material stays separate from authored code.

## Findings summary

| ID | Priority | Finding |
|---|---|---|
| R17 | P1 | Trigger Effect now energizes/modulates the load, contrary to the explicit output-neutral scope. |
| R18 | P2 | IdentifyTime attribute writes inherit the old timer phase and identical writes cannot restart the countdown. |
| R19 | P2 | Identify catch-up does work proportional to elapsed seconds before servicing UART/input safety. |
| R20 | P2 | Step-specific proportional timing was incorrectly applied to Move to Level. |
| R21 | P2 | Non-immediate upward TARGET commands still postpone ON until the first timer callback. |
| R22 | P2 | Foundation prevalidators accept invalid compound counts/types and reserved reporting directions. |
| R23 | P2 | Required target foundation ABI evidence and current completion checkpoint are still missing. |

### R17 — Remove the out-of-scope power-stage identification effects

Sources: `src/glsd301p_identify.c:78`–`:160`,
`src/glsd301p_control.c:233`–`:305`, `:493`–`:525`,
`firmware/glsd301p-ed/glsd301p_telink_target.c:270`;
`tests/test_glsd301p_zcl_dispatch.c:3045`.

The accepted 2026-10-04 brief explicitly required RAM IdentifyTime/countdown,
power output unchanged, and no blinking power stage. Muse nevertheless added
Blink/Breathe programs, saved-output restoration and control preemption. Blink
from OFF emits ON at maximum; Breathe also forces ON. The new test expressly
asserts energization from OFF. The checkpoint rationalizes that change as a
correction to the contract, although no user authorization changed the boundary.

This is a candidate code/scope regression, not evidence that a live device was
changed. Remove the power-effect overlay and its restoration/preemption paths.
Keep bounded RAM commissioning semantics. Optional effects unsupported within
this authorized scope must return a truthful unsupported/rejection status before
any state, timer, transition or UART mutation. Do not replace this with another
load-identification mechanism or broaden authority to satisfy a conformance claim.

Oracle: SDK Identify/Trigger Effect dispatch with initially OFF and ON output,
including a running Level transition, local takeover, fault and delayed UART.
Verify all accepted frames, output/level state and timer ownership remain
unaffected by Identify operations. Unsupported effects must never restore an
older ON state later. Retain the original ordinary-command regressions.

### R18 — Attribute writes are not an observable timed event

Source: `src/glsd301p_identify.c:49`–`:70`; target still initializes ZCL with a
NULL foundation hook. Command Identify starts a phase at its receipt, but
attribute writes are detected only by comparing the store with `shadow` at an
old whole-second boundary.

Trigger: previous second mark 0, write IdentifyTime=1 at time 999 ms. At 1000 ms
the code adopts the value and immediately decrements it to zero, giving roughly
one millisecond of identification. A write equal to `shadow` is indistinguishable
from no write and never establishes a fresh phase. A write after a long service
gap is charged for elapsed time preceding that write. The hosted test writes
near a phase boundary chosen by fixture initialization and misses these cases.

Fix: connect accepted IdentifyTime writes to a bounded production observer/shared
adapter so their receipt time and value, including same-value writes, are known.
Start/restart at that write's time; unsuccessful/wrong-type/wrong-endpoint writes
must not restart. Preserve NULL-hook cleanup coverage and per-record semantics
for ordinary, no-response and supported undivided writes. Avoid deriving fresh
event timing from a store comparison.

Oracle: actual foundation dispatch at 1/999/1000 ms, repeated equal-value writes,
stop/restart, delayed service, mixed valid/invalid records and timebase wrap.
Immediately Query/Add If Identifying must see the written value; the first
decrement must reflect elapsed time since that accepted write. No power effects.

### R19 — The household tick has an elapsed-seconds catch-up loop

Sources: `src/glsd301p_identify.c:49`,
`src/glsd301p_control.c:659` (Identify runs before PUSH sampling/UART service).

The `while` advances one second per iteration even when the countdown is zero.
A 24-hour callback gap takes 86400 iterations; an unsigned near-wrap gap can
take about 4.29 million. This defeats the bounded/O(1) application-loop design
and delays the safety-relevant work following it. No on-target duration is
claimed; the operation count is apparent from source.

Fix: compute elapsed whole seconds and residual phase in constant bounded work,
use saturating decrement, and stop doing catch-up work for a disabled countdown.
Integrate this with R18's explicit write/command timestamp. Preserve unsigned
wrap conventions. Do not add a cap that silently loses elapsed time or changes
timebase units.

Oracle: actual shared tick and production IO sequencing across ordinary/delayed
ticks, a disabled countdown, maximum value and near-wrap elapsed time. Instrument
operation count or another deterministic bounded-work oracle on hosted runners;
an unrelated CI timeout is not a reproduction. Check UART fault/deadline and
input sampling still proceed without elapsed-second iteration.

### R20 — Move to Level duration is shortened by Step's clipping policy

Source: `src/glsd301p_control.c:376`–`:393`; counterpart Step calculation is in
`src/glsd301p_zcl_commands.c:123`–`:141`.

`start_target` now calculates requested versus clamped span and calls the
proportional-time helper for every target command. Example: CurrentLevel=10,
MinLevel=2, accepted Move to Level(0, transitionTime=100). Its eight-unit clamped
move is scheduled for 80 tenths instead of the requested 100. Proportional
reduction belongs to a Step clipped at a limit, not all Move to Level commands.
The ledger labels the extra change “same class” without an applicable contract.

Fix: preserve the existing accepted/clamped Move to Level policy and requested
finite duration. Keep proportional timing at Step dispatch only, along with its
bounded arithmetic/rounding. Do not silently change the endpoint's acceptance
policy to avoid this regression.

Oracle: SDK Move to Level and With On/Off dispatch below minimum with finite,
zero and reserved times, delayed callbacks/wrap and RemainingTime, alongside
clipped/unclipped Step controls. No early completion of an accepted finite move.

### R21 — TARGET upward ON is delayed until a 100 ms callback

Sources: `src/glsd301p_control.c:401`–`:420`, `:570`–`:574`;
`src/glsd301p_timer_events.h:37`;
`tests/test_glsd301p_zcl_dispatch.c:2228`.

With OFF at level 16, an upward Move to Level With On/Off with nonzero duration
returns SUCCESS and starts the timer without applying ON. Its mirror and
runtime stay OFF until the first 100 ms callback. STEP uses the same path.
MOVE already applies its increase effect at onset. The “upward onset” regression
waits 150 ms before observing output, so it proves early-in-transition behavior,
not the required command-onset effect.

Fix: apply the effect of a real accepted increase at admission, before returning
from dispatch; keep equality/downward preservation and OFF-at-minimum fixes.
Keep failure/readiness semantics truthful: a rejected or failed admission must
not manufacture output changes. Emit/queue through the existing guarded transport
and do not claim instantaneous physical output.

Oracle: inspect mirror, runtime and offered/accepted frames immediately after
SDK dispatch, before any timer advance; then exercise zero-progress samples,
delays, replacement, failure/fault and OFF priority. Preserve downward/equal tests.

### R22 — Bounded-size replication still accepts malformed typed records

Sources: `tools/apply_glsd301p_sdk_patches.py:420`–`:475`, `:542`–`:579`,
`:585`–`:631`, `:928`; pinned `zcl.c` datatype/size and reporting parsers.

Concrete cases that the current prevalidator accepts:

- Report record `00 00 4c 00 01`: STRUCT claims 256 elements but none follow.
  The validator reads only the low count byte (`itemNum=p[0]`) and consumes
  just the two-byte count, matching the SDK's defective truncation.
- Report record `00 00 4c 01 00 4c`: one STRUCT element is declared but its
  nested count is absent. `zcl_getDataTypeLen(STRUCT)` is zero, so the validator
  and parser treat the missing compound body as a zero-length scalar.
- Report record `00 00 ff`: an invalid/reserved datatype receives a zero size
  from the SDK default case and passes without a value.
- Configure Reporting/read-configuration-response validators treat any nonzero
  direction as the receive form. For example reserved direction 2 passes size
  validation rather than being rejected before parsing; Configure Reporting
  Response long forms also validate length without their direction/status grammar.

These are malformed acceptance/grammar defects, not a newly demonstrated memory
overwrite claim. Merely documenting compound nesting as unverified does not
make these incomplete public-C records valid.

Fix: define an explicit supported datatype grammar, handle full wire count
width/sentinels, and reject unsupported compound forms truthfully before unsafe
SDK size logic. Do not implement arbitrary recursive SDK features to satisfy
this audit. Keep valid supported flat structures and strings compatible, with
bounded nesting/count/allocation policy where supported. Distinguish genuine
zero-length datatypes from unknown/unsupported types. Validate defined reporting
directions and status-dependent long records, not only short success forms.

Oracle: real root/foundation dispatch for the examples and complete/truncated
controls, count high bytes, nesting, reserved types/directions/status combinations;
observe return/wire status, hook, allocations, table/NV effects and cleanup.
Update original-body negative controls to cover these residual authored guards.

### R23 — Mandatory ABI proof and the current checkpoint are incomplete

Sources: `tools/build_glsd301p_ed_tc32.sh:276`–`:308`,
`devices/gl-sd-301p/HARDENING-CHECKPOINT.md:156`, `:245`;
Identify target callback versus `tests/test_glsd301p_zcl_dispatch.c:168`.

The target ABI probe still covers cluster-registration and timer structures only.
Required foundation record sizes/allocation thresholds were left as a residual
limit rather than executed target evidence. The Identify callback is copied into
a harness thunk; the shared countdown is real, but a mirrored callback is not
proof of production write-observer wiring. The checkpoint still has M4/M5 pending
and a next action to run builds after the issue and PR declare completion.

Fix: use target-compiled probes for the foundation layouts/pool allocation
thresholds affected by P5, including flags/packing in both SDK/app contexts;
tie the tested Identify command/write adapter to production code. Compare actual
host harness layout assumptions, explicitly guarding any differences. Maintain
one current matrix, preserving historical rows as clearly labeled history.
Update checkpoint, ingress map and PR/issue final record coherently.

Oracle: exact-head hosted ABI assertions/extracted layout metadata and supported
boundary/exhaustion tests; production adapter linkage; final matrix contains
finding→fix→regression→raw run/job/step evidence, with no conflicting next action.

## Prior finding disposition

| Prior IDs | Current disposition |
|---|---|
| R1–R3, R5, R8 | Prior transport, recovery, exact-head and cleanup fixes preserved by source inspection and existing hosted coverage. |
| R4 | Elapsed interpolation remains; command-duration/onset defects remain in R20/R21. |
| R6/R10 | Add/Add If Identifying counted-name and exact Membership guards are materially corrected. |
| R7/R11 | Ragged read-cfg/discovery and one-byte failed-response guards corrected; typed/enum grammar remains R22. |
| R9 | Equality/decreasing/minimum direction fixes are substantive; onset requirement remains R21. |
| R12 | Per-record response status reset fixed; mixed-order regression reads real wire results. |
| R13 | RAM command/countdown/Query/Groups works in ordinary sequences; R17–R19 prevent acceptance. |
| R14 | Fresh GLSD-ED-003 / APP_BUILD 04 / FILE_VERSION 0x7F040001 allocated consistently. Further changed bits require another fresh identity. |
| R15 | Two matching clean target/OTA jobs now proved; current ledger/ABI gaps remain R23. |
| R16 | Proportional clipped-Step fix works; its unintended application to Move to Level is R20. |

## Independently inspected hosted evidence

- [Boundary run 37224141144](https://github.com/analienx/gledopto/actions/runs/37224141144):
  exact candidate SHA, AP dispatcher PASS, 22-case matrix reports `failed=0`.
- [Readiness build 37224141202](https://github.com/analienx/gledopto/actions/runs/37224141202)
  and [independent rebuild 37224256709](https://github.com/analienx/gledopto/actions/runs/37224256709):
  both successful, exact `69831aa`, matching printed artifact hashes.

| Artifact | Matching SHA-256 in both clean jobs |
|---|---|
| ELF | `b93cfab786ecef64725f7fbec53378f1061e559ba27e34df28c456fe73dc5605` |
| Raw BIN | `76070685a88d8fccb648f2c7f1fafa8ff9b41d4dcbd311cd661f82a7433e5d5a` |
| Final BIN | `3c59119658cdf2cd273e4a097530f6c7218e182b5f1c649ce3dcb4bb7ee9cf20` |
| MAP | `7ead64a877f97a7f3949a2268552edf1621e868e25030c878e905fd4ac7b81ea` |
| Quarantined OTA | `5af42998cfb8222f4a40d00e485158e8347d569cb41e0963a3b8a4c2ee477009` |

Source/body/log inspection is useful evidence; no downloadable deployment
artifacts or physical execution were used. Green tests currently include the
out-of-scope Blink/Breathe behavior and do not close R17–R23.

## Protocol reference and limits

[CSA ZCL revision 6](https://csa-iot.org/wp-content/uploads/2019/12/07-5123-06-zigbee-cluster-library-specification.pdf)
defines finite Move to Level duration (§3.10.2.4.1), proportional clipping for
Step (§3.10.2.4.3), increasing-command On/Off onset (§3.10.2.4.5), and two-byte
compound counts and recursive values (§2.5.2.1). These support the wire/timing
expectations; this review does not claim complete Zigbee conformance.

Opaque libraries, live power/flash/network behavior and unsupported SDK features
remain explicit limits. They do not excuse confirmed defects in public authored
code. No deployment, merge, release, OTA publication or device action is authorized.

## Session handoff decision

Predecessor UUID `01a1017d-681f-7303-af97-8caf85983806` has a completed native goal
and no reported outstanding validation jobs. Its `session.jsonl` is 85771604
bytes; top-level session files total 94140130 bytes. These are transcript/storage
measurements, not active model context or billed usage. Under the user's explicit
fresh-session preference, transfer sole candidate-writing ownership to a new
visible session with a concise, source-verified context packet. Preserve the
predecessor, its receipts and history; do not reset/delete it or replay its full
transcript. The next implementation stops with **independent acceptance review
pending**.
