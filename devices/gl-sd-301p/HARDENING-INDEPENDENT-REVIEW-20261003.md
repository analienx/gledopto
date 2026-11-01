# Independent hardening review — 2026-10-03

## Decision and reviewed inputs

**REQUEST CHANGES. Independent acceptance has not passed.**

Reviewed candidate: `7184eec40d8141ccd92a005d2d6d5ef0c2bcb853`, draft [PR #8](https://github.com/analienx/gledopto/pull/8), branch `codex/glsd301p-client-hardening`, clean checkout `C:\Workspace\repos\gledopto-hardening`. Frozen reviewed baseline/PR #6 remains `760c141925f29e517831c92afd61bd9c15e1b7b5`.

Inputs: user-supplied Muse completion transcript, candidate source and tests, findings/checkpoint documents, issue #1's superseding completion comment, authenticated canonical policies, build/patch scripts, hosted workflow definitions and actual run logs. The pinned public Telink SDK is `d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e` (V3.7.2.0). SDK line references below refer to that exact upstream commit, before application patches. Only ev_timer.c, zcl_level.c and zcl_onoff.c receive candidate SDK patches; the other cited bodies are unchanged and compiled into this target.

Review covered the changed runtime/control/transport/timer/rejoin/ZCL/health code and headers; target wiring, readiness/boot/fault paths; patch provenance and target build gates; hosted harnesses, workflow checkout semantics, decoder and development identity; reachable SDK parser and allocation ownership paths. Source inspection and hosted log inspection were performed locally. **No firmware build, test, lint, packaging or validation was executed locally.** Findings below have deterministic source traces; new failing hosted regressions still need to be authored and run. Opaque stack/driver archives and physical hardware behavior cannot be fully audited from this source review.

## Findings summary

| ID | Priority | Finding | Required remediation goal |
|---|---|---|---|
| R1 | P1 | Expired queued ON can be transmitted in the same step that raises a fault | G1 UART safety |
| R2 | P1 | Pending traffic bypasses the accepted DMA transfer's deadline | G1 UART safety |
| R3 | P1 | A successful rejoin does not clear ownership after an earlier successful join | G2 recovery |
| R4 | P2 | Small level changes finish long transitions far too early | G3 timing |
| R5 | P2 | Host regression workflow checks the PR merge commit rather than the recorded candidate head | G5 evidence |
| R6 | P1 | Enabled Identify and Groups parsers still read beyond malformed payloads | G4 protocol safety |
| R7 | P1 | Foundation parsers still read beyond malformed records; configure-report allocation size can wrap | G4 protocol safety |
| R8 | P1 | NULL foundation hook leaks parsed-command buffers on ordinary attribute requests | G4 protocol safety |

P1 means a required fix before acceptance. P2 also remains in this remediation scope. The SDK and transition defects include inherited behavior; migration into a shared module or a passing narrow harness does not close them.

## R1 — Reject queued ON before any transmission when its deadline expires [P1]

Location: `src/glsd301p_uart_service.c:82–112`; integration at `src/glsd301p_control.c:444–449`.

At queue age >=32 ms, service sets `fault_latched` and `event.fault_raised`, then continues to peek/send the queued frame if the link is idle. Runtime fault latching and replacement with confirmed OFF happen only after service returns. No check of `is_off` or the service fault prevents the normal frame from starting.

Deterministic trigger: ready runtime, queue an ON at t=0; hardware rejects DMA starts while idle, or stays busy until t=31; at t=32 it becomes idle and accepts a start. The service both raises the deadline fault and sends ON. OFF is queued afterward. A newly faulted device can therefore energize before its next service step.

Fix: detect deadlines before any normal hardware start; discard/quarantine stale normal traffic and latch/cancel output state before transmission can proceed. Continue bounded confirmed-OFF recovery without waiting indefinitely. Test through the complete control/service/transport path and assert the actual captured UART frame sequence, rather than only the fault flag. Include rejection-to-acceptance and busy-to-idle boundary changes, age31/32, wrap, boot failure and a running transition.

## R2 — Check accepted DMA ownership independently of pending traffic [P1]

Location: `src/glsd301p_uart_service.c:82–135`.

The in-flight deadline is checked only after the pending-queue branch. That branch always returns, including when hardware stays busy. The deadline of an already accepted DMA transfer is consequently invisible whenever another frame is pending.

Deterministic trigger: send ON at t=0, leave UART busy, queue a fresh frame at t=31, service at t=32. Queue age is only1 ms, so no fault occurs, despite the original transfer being busy for32 ms. At minimum this extends the deadline. Queue replacement/coalescing can further change which stamp the branch observes.

Fix: retire/check the owned in-flight transfer before pending-queue handling on every step; do not overwrite its start stamp until that ownership is retired. Keep queue deadlines separately. Test pending normal and OFF traffic, coalescing, delayed callbacks and uint32 wrap; prove the first accepted transfer faults at its own deadline regardless of newer pending work.

## R3 — Reconcile authoritative joined state independently of counter edges [P1]

Location: `src/glsd301p_rejoin.c:30–37,95–109`; target callbacks `firmware/glsd301p-ed/glsd301p_telink_target.c:379–428`.

`note_joined(true)` clears state only on `true && !last_joined`. Parent-loss handling increments a counter but never clears `last_joined`. Target commissioning callbacks do not call `note_joined(false)` on parent loss. The unit test supplies that missing false observation directly, so it does not match actual wiring.

Deterministic trigger: initial successful join sets `last_joined=true`; parent loss starts an accepted attempt, setting SDK_ACTIVE; subsequent successful rejoin calls `note_joined(true)` with the cached flag still true. It returns false and leaves SDK_ACTIVE. A later parent loss cannot start recovery. If the start had been rejected, successful external recovery similarly leaves RETRY_PENDING and the owned retry event is not stopped.

Fix: invalidate joined-edge bookkeeping on observed loss and always reconcile successful authoritative joined evidence to IDLE, stopping any application retry. Keep success counter deduplication separate. Preserve SDK backoff for accepted attempts. Test the actual target callback adapters with two complete join/loss/recovery cycles, accepted and rejected starts, duplicate SUCCESS, startup failures, factory-new behavior, and success before an owned retry fires.

## R4 — Honor requested transition duration for small deltas [P2]

Location: `src/glsd301p_control.c:322–365`.

The target transition takes `ceil(diff / remaining_time)` level units on every100 ms callback, with a minimum step of1. Integer quantization forces progress each callback even when the requested duration requires multiple callbacks per level change. A one-level change with transitionTime1000 (100 seconds) finishes in the first100 ms; a2→254 change with that duration finishes in25.2 seconds. RemainingTime is callback-count based, so scheduling gaps also distort duration.

Fix: store transition origin/target/start time/duration and use bounded integer interpolation or equivalent fractional accumulation based on elapsed time. Preserve zero-duration behavior and explicit reserved/default duration policy, saturation, OFF/PUSH takeover, interruption/replacement and no overshoot. Tests must cover one-level and full-range moves with long durations, delayed callbacks, wrap and exact completion/RemainingTime semantics. Early finalization must not turn a requested slow transition into an immediate change.

## R5 — Bind host regressions to the reviewed head [P2]

Location: `.github/workflows/cleanroom-guard.yml:18,252–253`.

Both implementation checkouts omit `ref`. In pull_request runs, Actions checks out the synthetic merge commit. Run37127418335 explicitly logs merge commit `2edf21c` merging7184eec into629a2c5 for boundary-and-tests. This is useful integration evidence, but it does not establish host regression results for the exact recorded source head. The readiness workflow correctly checks out7184eec explicitly and supplies separate target reproducibility evidence.

Fix: add an exact-head host validation lane or explicitly pin these implementation checkouts to `github.event.pull_request.head.sha || github.sha`; retain merge testing separately if useful. Assert/log actual HEAD and record it beside host harness results and patch provenance. Evidence must distinguish candidate tests from integration tests.

## R6 — Cover enabled Identify and Groups ingress before SDK reads [P1]

Target compiles `zigbee/zcl/general/zcl_identify.c` and `zcl_group.c` (`tools/build_glsd301p_ed_tc32.sh:93–94`) and registers SDK ZCL ingress directly (`glsd301p_telink_target.c:531–537`). The existing cluster harness stages only Level/OnOff bodies.

Pinned SDK references:

- [Identify handler](https://github.com/telink-semi/telink_zigbee_sdk/blob/d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e/tl_zigbee_sdk/zigbee/zcl/general/zcl_identify.c#L134): Identify reads payload[0:2] at144 without a length guard; TriggerEffect reads both bytes at151–152. QueryResponse similarly reads2 bytes at180.
- [Groups handler](https://github.com/telink-semi/telink_zigbee_sdk/blob/d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e/tl_zigbee_sdk/zigbee/zcl/general/zcl_group.c#L195): Add/View/Remove read a group ID without minimum lengths. GetMembership reads count at275 and count×2 bytes at284 without checking available payload. `{count=1}` alone reaches an out-of-payload ID read; malformed add/remove data can reach group table operations before validation.

Fix: validate fixed and counted/string payloads before reads or any state/storage mutation, using narrow deterministic SDK patches or a complete pre-dispatch validation boundary. Include both directions actually reachable on this endpoint. Add real SDK Identify/Groups bodies to the hosted sanitizer harness with all truncated prefixes, oversized counts, string-length mismatches and valid commands. Assert no callback/group mutation on malformed input and truthful response status.

The apparent Groups response-buffer offset was checked: its response builder intentionally writes capacity/count into the reserved first word. It is **not** reported as a defect.

## R7 — Bound foundation records and allocation arithmetic [P1]

Location: pinned SDK [zcl.c](https://github.com/telink-semi/telink_zigbee_sdk/blob/d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e/tl_zigbee_sdk/zigbee/zcl/zcl.c#L1266), compiled by `tools/build_glsd301p_ed_tc32.sh:89`.

Write parser1266–1311 loops while any byte remains, skips2 bytes and reads a type without requiring a3-byte record header; `zcl_getAttrSize` and `memcpy` then consume a value without a bounded remaining-length contract. A one-byte Write payload already reads outside the payload. Truncated variable-length values can over-read during size discovery/copy.

Configure-report parser1709–1781 likewise consumes direction, IDs, type, intervals and reportable changes without complete record validation. Its allocation prefix uses **u8 len** at1742 for `sizeof(command)+numAttr*sizeof(record)`; sufficiently large record counts overflow this byte, while the second pass still writes numAttr records. The discrete-type scan also advances an extra byte at1735 absent from the second-pass format, so a valid multi-record stream can be miscounted. These are distinct hazards within the same parser remediation goal; hosted target-ABI evidence must establish exact overflow thresholds and compare them with actual ingress/reassembly size limits.

Fix: inventory every enabled foundation request/response parser; use bounded cursors, complete record validation, checked allocation arithmetic with target widths, capacity limits and single ownership paths. Reject malformed input before attribute/report-table changes. Preserve specific malformed/invalid statuses through the root dispatcher; its835–837 failure normalization currently erases foundation error detail. Exercise actual zcl.c dispatch with sanitizers, exact-sized payload allocations, target ABI probes, variable-length strings, discrete and analog configure-report records, allocation failure and maximum count limits. Include ordinary valid read/write/report/discovery interactions and enabled OTA ingress in the inventory rather than claiming completeness from Level/OnOff alone.

## R8 — Release parsed foundation commands when the hook is NULL [P1]

Location: target `glsd301p_telink_target.c:531`; pinned SDK [zcl_cmdHandler cleanup](https://github.com/telink-semi/telink_zigbee_sdk/blob/d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e/tl_zigbee_sdk/zigbee/zcl/zcl.c#L866).

Target initializes `zcl_init(NULL)`. SDK sets that as `zcl_vars.hookFn`. Read handler allocates a parsed read command and stores it in `pCmd->attrCmd` at1086–1090; Write does so at1325–1329. The root dispatcher frees attrCmd only inside `if (hookFn && toAppFlg && attrCmd)`. With a NULL hook, ordinary requests leak their parsed-command event buffers. Only the incoming APS command and temporary response buffers are freed. Repeated Basic reads, including health polling, can exhaust the shared event-buffer pool and disrupt network/OTA operation.

Fix: make cleanup independent of optional notification, or deliberately install a documented no-op hook if that completely satisfies ownership across all reachable paths. Avoid double frees and do not mask error-path leaks. Hosted allocation-accounting tests must run actual root dispatch with target hook configuration through repeated reads/writes and relevant responses, no-response forms and allocation failures. Allocations must return to baseline after each settled command.

## Follow-up investigations required by the protocol goal

These are bounded review questions, not additional confirmed findings:

- Partially present Level options (base payload plus one option byte; Stop length1) currently pass minimum-only guards. Establish the applicable ZCL compatibility rule from authoritative sources, then reject malformed forms before callbacks and preserve a running transition; add both accepted and rejected forms to coverage.
- Identify callback currently returns SUCCESS without using IdentifyTime/command payload. Document supported behavior and provide truthful status/attribute semantics for advertised functionality.
- Health network state is intentionally advisory: all non-success BDB events map JOINING, including possible terminal failure. Make this limitation explicit or derive an accurate disconnected/joining distinction from actual recovery ownership.
- Inventory enabled OTA and foundation response dispatch, short ZCL headers, datatype size calculation, report table/storage side effects, and pool-exhaustion exits. Do not infer archive-internal safety from reviewed public C bodies.

## Evidence independently checked

All four PR checks were successful. Two separate GitHub-hosted readiness runs explicitly checked out the final candidate head:

- [Run37127418330](https://github.com/analienx/gledopto/actions/runs/37127418330)
- [Run37127593399](https://github.com/analienx/gledopto/actions/runs/37127593399)

Their logs agree on final binary SHA256 `c080ee63c52212d486571a26d02e0fc497cb0158d72d4cfda225b17516d7baeb` and wrapped OTA SHA256 `0b0caf53218ba273f25fe06715fc37e5a8329c0d59dbdf2c842718cdd9e8b0e8`. Reported sizes135716 and135782 bytes respectively; identity GLSD-ED-002 / APP_BUILD03 / FILE_VERSION0x7F030001. Host/integration evidence: [run37127418335](https://github.com/analienx/gledopto/actions/runs/37127418335), subject to R5.

Positive findings: owned static timer design removes application reliance on pool scheduling; patching checks exact original/patched hashes; boot DMA work is bounded and uses static storage; readiness gates and confirmed OFF vector remain explicit; diagnostics remain RAM-only/read-only; build/slot/role/quarantine gates and two matching clean builds provide useful provenance. These improvements should be preserved.

Evidence is hosted source/build/log evidence, not independent physical output verification or a full audit of opaque libraries. No artifact download/byte inspection or device execution was performed for this review. Candidate7184eec and its identity remain quarantined. Further firmware changes require a fresh development identity and two clean builds of the new sealed SHA. Deployment, merge and release remain outside this task.
