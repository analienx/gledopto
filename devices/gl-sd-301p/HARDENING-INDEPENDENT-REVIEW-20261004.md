# Independent review — GL-SD-301P, 2026-10-04

## Decision and scope

**Changes requested; independent acceptance remains pending.** Reviewed candidate
`bc7196f028d466a12ea992c2f2c3aa9cc2f6c764`, draft
[PR #8](https://github.com/analienx/gledopto/pull/8), branch
`codex/glsd301p-client-hardening`, clean implementation checkout
`C:/Workspace/repos/gledopto-hardening`. The frozen PR #6 candidate remains
`760c141925f29e517831c92afd61bd9c15e1b7b5`.

This is a source and hosted-evidence review, not physical validation. Inspected
runtime/control/transport/rejoin integration, target callbacks, all SDK patch
definitions, dispatch regressions, enabled cluster/foundation/OTA ingress,
allocation ownership, build/check workflows, identity and handoff documents.
Reference SDK: `telink-semi/telink_zigbee_sdk` at
`d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e`. Vendor reference files were read
separately; no vendor code is copied into this report or implementation.
No local firmware builds, tests or lint were run. New failure cases below are
confirmed by source tracing; their executed reproductions are assigned to Muse
on GitHub-hosted Actions.

## Prior findings

| Prior ID | Review of bc7196f | Remaining boundary |
|---|---|---|
| R1 | Expired/faulted queued normal frames are dropped before starting DMA; service cancels transitions on fault. | Preserve captured-frame regressions. Physical OFF remains unverified. |
| R2 | Accepted-transfer deadline is checked independently of pending traffic. | Preserve age/wrap/recovery coverage. |
| R3 | Loss invalidates the joined edge; authoritative joined state clears attempt ownership and retry. Production uses the shared BDB adapter. | SDK callback mapping was source-inspected; opaque stack behavior remains a limit. |
| R4 | Target interpolation and Move integrate elapsed time; new small-delta, long-duration and wrap tests address the reported quantization defect. | Additional command semantics remain: R9 and R16. |
| R5 | Hosted workflows select and assert the PR head instead of treating a merge commit as the candidate. | Repeat required checks at the next sealed SHA. |
| R6 | Identify and Groups short reads now have guards. | Full Groups wire grammar is still wrong: R10. Identify effects are missing: R13. |
| R7 | Variable-record prechecks and three widened allocation lengths address important bounds defects. | Acceptance grammar and per-record response status remain: R11/R12. |
| R8 | Parsed attribute command cleanup occurs outside the optional hook; the NULL-hook leak regression is meaningful. | Preserve allocation/failure accounting. |

These closures apply to the reported failure sequences, not to whole-subsystem
acceptance. Muse did useful implementation work; green checks do not close the
following omissions and regressions.

## Findings

### R9 — P1: repeated minimum-level command can re-energize output

Sources: `src/glsd301p_control.c:222`, `:258`, `:375`.
`glsd301p_control_apply_level` selects ON when `direction_up` is true;
the immediate path treats `target == current_level` as upward using `>=`.

Trigger: ready and ON above minimum; send immediate Move to Level With On/Off
to minimum. It emits OFF. Repeat the identical command while current level is
minimum and OFF. The equality path sets `direction_up=1` and emits ON at minimum.
A descending transition also derives direction from rounded samples and treats
an unchanged sample as upward; `level > min_level` independently forces ON even
when the semantic command decreases an already OFF level.

Fix scope: base On/Off effects on the command's actual direction, retaining it
through interpolation; equality must not invent an increase. Preserve OFF for
decreasing commands from OFF. Apply ON when a real increasing command begins,
and OFF upon reaching minimum during a decrease. Keep fault/readiness/PUSH/OFF
preemption intact. Regression: real SDK Level dispatch → production control →
UART capture for repeated minimum, equal level, zero Step, upward/downward
transitions, quantized no-change ticks, OFF initial state, replacement and fault.
Check state AND every emitted frame, including when transmission is delayed.

### R10 — P2: Groups accepts malformed Add and rejects valid Add If Identifying

Sources: `tools/apply_glsd301p_sdk_patches.py:294`, `:335`, `:372`;
`tests/test_glsd301p_zcl_dispatch.c:1169`, `:1210`, `:1260`.
P4 Add guards only two bytes, allowing a missing or truncated GroupName before
`aps_add_group_req` mutates membership. Add If Identifying requires exactly two
bytes, rejecting its mandatory string field. The pinned SDK's own serializer
always emits the string prefix, including an empty name: a three-byte payload.
The tests instead treat two-byte Add/Add If Identifying as valid.

Fix scope: validate the complete uint16 ID plus bounded character-string form
before mutation; accept valid empty/named inputs while unsupported names can be
ignored after validation. Check sentinel, maximum length and trailing bytes
against the chosen protocol contract. Get Membership currently permits surplus
bytes (`< 1+2*count`), despite the ledger claiming exact shape. Reconcile that
policy with exact-record validation. Regression: SDK-serialized valid frames,
every truncated prefix, count/name mismatch, capacity errors and active/inactive
identifying state, checking APS calls and response frames.

### R11 — P2: foundation validators still accept incomplete record shapes

Sources: P5 in `tools/apply_glsd301p_sdk_patches.py:806`, `:819`, `:845`, `:858`;
pinned `zcl.c` functions `zcl_parseInReadReportCfgCmd`,
`zcl_parseInDiscAttrsRspCmd`, `zcl_parseInDiscAttrsExtRspCmd`,
`zcl_parseInWriteRspCmd` and `zcl_parseInCfgReportRspCmd`.

Read Reporting Configuration still computes `dataLen / 3` without requiring
whole records. One complete record followed by a byte processes the first and
silently discards the suffix. Discover response checks only the leading byte;
its record count truncates incomplete suffixes. Write and Configure Reporting
response guards accept ANY one-byte status, although a failure needs its record
fields; their success-only short form is not status-checked. Count/direction/type
policies are not consistently tied to record grammar. These are accepted
malformed-input defects, not a new demonstrated out-of-bounds read claim.

Fix scope: command-specific complete-record validation, including status-dependent
forms, reserved directions/types, empty-payload legality and supported compound
types. Reject unsupported shapes truthfully. Do not impose a uniform fixed
length on variable records. Regression: real root/foundation entry point,
all truncated prefixes and extra suffixes of valid frames, mixed-record forms,
actual status/hook/no-mutation/allocation effects, and valid interoperability.

### R12 — P2: Read Reporting Configuration response status leaks between records

Sources: pinned `zcl.c:1968`–`:2020`; P5 only widens its response allocation at
`tools/apply_glsd301p_sdk_patches.py:664`.
`status` starts as SUCCESS outside the response loop. An unknown attribute sets
UNSUPPORTED_ATTRIBUTE; a subsequent existing reportable/configured attribute
fills its fields but never resets status. Both response records then have the
first failure status. Existing large-record tests exercise unsupported attributes
and miss the mixed success/failure ordering.

Fix scope: independently initialize and derive each record's status. Regression:
configure a real reportable attribute, then read `[unknown, configured]` and the
reverse order through production root dispatch. Decode captured wire statuses,
IDs, datatype, intervals/change; include unreportable, missing configuration,
multiple successes and allocation failures. Keep cleanup ownership correct.

### R13 — P2: Identify acknowledges success without implementing its effect

Sources: `firmware/glsd301p-ed/glsd301p_telink_target.c:243`;
`tests/test_glsd301p_zcl_dispatch.c:168`, `:1810`.
The registered callback ignores all payloads and returns SUCCESS. The pinned
Identify handler passes IdentifyTime to the app instead of applying it. Therefore
Identify(5) leaves IdentifyTime at zero, Query emits no identifying response, and
Add Group If Identifying cannot be enabled by the accepted command. Tests lock in
this no-effect behavior and manually set time for Groups coverage.

Fix scope: provide truthful bounded commissioning semantics. At minimum, implement
RAM IdentifyTime with owned countdown and Query/Groups integration, including the
attribute-write path; keep power output unaffected. Document any physical
identification limit explicitly. Unsupported Trigger Effect IDs/variants must
not report a fabricated successful effect. Do not introduce a power-stage blink
or device action. Regression: command → state/countdown → Query → valid Add If
Identifying, stop/restart/write/expiry, wrong endpoint and unsupported effect,
using the actual production adapter rather than a test-only state setter.

### R14 — P2: new firmware reuses the previous development image identity

Sources: `firmware/glsd301p-ed/version_cfg.h:7`, `:14` and build/identity assertions.
The new implementation still uses APP_BUILD `0x03`, FILE_VERSION `0x7F030001`,
and GLSD-ED-002, despite the previous remediation goal requiring a fresh identity.
Different firmware bits now share the earlier quarantined candidate identity,
weakening provenance and any future version-based selection.

Fix scope: allocate a fresh unused development identity from repository/issue
history; update all source, assertions, wrapper metadata and documentation
consistently. Preserve manufacturer/model separation and quarantine. No OTA
publication or eligibility change is authorized. Regression/evidence: final
hosted manifest and wrapped metadata agree on the new identity and exact SHA.

### R15 — P2: handoff ledger and reproducibility evidence are incomplete

Sources: PR #8 body; `HARDENING-CHECKPOINT.md`, `HARDENING-INGRESS-MAP.md`,
`HARDENING-FINDINGS.md`; the two hosted workflows.
PR body still describes `7184eec` and old artifact hashes. Checkpoint contains
contradictory complete/pending rows. Ingress map contains older unguarded/pending
claims. R6 claims exact Membership length although code only checks a minimum.
Two independent jobs build matching final TC32 outputs, but the boundary job
does not wrap an OTA; the inspected final-SHA evidence provides only one wrapped
OTA build, leaving the requested two matching wrapped artifacts unproved.

Fix scope: one current acceptance ledger with finding→fix→regression→raw evidence;
preserve historical review entries as history. After sealing all code and identity,
obtain two independent clean GitHub-hosted TC32 AND quarantine-wrapper executions
at the same final SHA, recording toolchain/SDK/patch/ABI provenance and matching
ELF/raw/final/MAP/OTA identities. Keep no-publication policy; logs/metadata suffice.
Update PR body and concise issue result to that SHA and state remaining limits.

### R16 — P2: a clamped Step retains the full unclamped transition time

Source: `src/glsd301p_zcl_commands.c:106`–`:127`.
Step computes and clamps its target, then forwards unchanged `transitionTime`.
For current level 250, max 254, Step Up 40 over 10 seconds, the four-unit move
therefore takes ten seconds rather than the proportional one second. The elapsed
interpolation fix makes this slow boundary behavior precise; it does not fix
the command's duration calculation.

Fix scope: proportionally reduce finite Step duration when hitting min/max, using
bounded arithmetic and an explicit rounding policy; preserve immediate/reserved
duration and zero-step semantics. Regression: both bounds, unclamped control,
small and full step, zero and reserved duration, callback gaps and wrap through
actual SDK dispatch and target-wired control. Check RemainingTime and wire state.

## Current hosted evidence

Inspected successful GitHub-hosted runs at `bc7196f`:

- [Boundary PR run 37150021744](https://github.com/analienx/gledopto/actions/runs/37150021744)
- [Readiness PR run 37150021753](https://github.com/analienx/gledopto/actions/runs/37150021753)
- [Boundary dispatch 37150235095](https://github.com/analienx/gledopto/actions/runs/37150235095)

Raw logs report 24 controlled pristine-body reproductions and passing patched
dispatch regressions. Exact-source assertions are present. Two clean TC32 jobs
match these hashes:

| Output | SHA-256 |
|---|---|
| ELF | `7f250345a1779896df66acbb45c10b8191a6d19129fe96e67cef7820e7607114` |
| Raw BIN | `6feb1a42726f5a8640705f0676a86d7b7e0020fabd8a1c6c29dce4eb889e71af` |
| FINAL | `4f4b7acddb42671bbd658e6c7acd0c2630ecd54cb49f5b89fd545f57c960f4d9` |
| MAP | `1e5dde582df9e7dc35e41cf251ec45c96f12dcdc4cefb66eccaa180d61d130ba` |

FINAL size: 138052 bytes; raw size: 138040; TEXT VMA: `0x1560`; BSS: 13196.
The readiness job reports one wrapped OTA hash
`ae8417427521c254ad79e5362ae33494629633af7a88b8b1a899bcacdb9dd000`.
No downloadable deployment artifacts were published by these runs. Matching
hashes prove repeatability of those outputs, not closure of R9–R16.

## Bounded follow-up audit questions

These are investigations, not confirmed findings or permission to add features:

- Root header checks before reading the first ASDU byte, manufacturer headers,
  direction/command handling and Default Response behavior for malformed replies.
- Enabled compound datatypes and count/allocation limits under actual TC32 ABI:
  derive foundation record sizes and pool thresholds in hosted probes. Host
  sanitizer staging is useful but does not prove every target ABI property.
- Public OTA core behavior versus parser-only tests and opaque archives: document
  actual callback/storage ownership; use inert stubs only, never flash operations.
- RemainingTime, optional Level options, reserved rate/default-duration behavior
  and fault/readiness interactions after R9/R16, preserving existing contracts.

## Protocol references and review limits

The pinned public SDK's Groups serializer independently establishes that a name
prefix is emitted even for an empty name:
[zcl_group.c](https://github.com/telink-semi/telink_zigbee_sdk/blob/d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e/tl_zigbee_sdk/zigbee/zcl/general/zcl_group.c).
The [CSA ZCL revision 6 specification](https://csa-iot.org/wp-content/uploads/2019/12/07-5123-06-zigbee-cluster-library-specification.pdf)
describes Groups ID/name fields (§3.6.2.3.2/7), IdentifyTime assignment/countdown
(§3.5.2.2.1/3.1), proportional clamped Step timing (§3.10.2.4.3), and With On/Off
effects tied to increasing/decreasing level (§3.10.2.4.5). Confirm the applicable
SDK/profile contract while implementing, rather than deriving expected behavior
from the current test fixture.

Review cannot prove physical power-stage OFF, flash safety on hardware, live
Zigbee recovery or opaque archive internals. No deployment, merge, release, live
device, binding or coordinator changes are authorized. Muse's next result must
state **independent acceptance review pending**.
