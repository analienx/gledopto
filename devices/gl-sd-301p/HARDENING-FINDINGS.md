# GL-SD-301P client hardening — findings register

Branch: `codex/glsd301p-client-hardening`. Baseline: `760c1419`.
SDK: Telink V3.7.2.0 @ `d5bc2f7b`; TC32 sha256 `33b854be…b430`.
Status values: CONFIRMED (source evidence or hosted repro) / FIXED (hosted
validation at stated SHA) / ACCEPTED (no change, rationale).

## F1 — Pooled IO/Level scheduling ignores NULL (High, FIXED @ 0f342c9)

Trigger: all 24 SDK pool events occupied when IO (1 ms) or Level (100 ms)
schedules.
Expected: output/input path survives pool exhaustion.
Actual: `TL_ZB_TIMER_SCHEDULE` returns NULL; target ignores it
(`glsd301p_telink_target.c:730,427,449`). IO death is silent; the main-loop
600 ms watchdog (`apps/common/main.c`) cannot detect it.
Fix (M3): owned static `ev_timer_event_t` for IO/Level/health/rejoin-retry via
direct `ev_on_timer`/`ev_unon_timer` + explicit registration flags. Pool
`used`-contract wrapper (`ev_timer_taskCancel` requires `used`, `ev_timer.c`)
is unsuitable for static events.
Validation: hosted harness with all 24 pool slots occupied; static IO/Level
keep working. No physical-OFF claim.

## F2 — Level early-exit leaves stale transition mode (High, FIXED @ 0f342c9)

Trigger: Level tick fires while mode==IDLE or runtime not ready.
Expected: no active transition remains.
Actual: `glsd_level_timer_cb` (`glsd301p_telink_target.c:328-333`) clears the
timer handle but leaves `g_level_transition.mode` at TARGET/MOVE.
Fix (M3): reset mode to IDLE on every exit path; start functions return
failure without arming when refused.
Validation: hosted cancel/restart + PUSH-cancellation + not-ready refusal tests.

## F3 — Rejoin start unchecked and unpaced (High, FIXED @ 7633e78)

Trigger: `BDB_COMMISSION_STA_PARENT_LOST` / `REJOIN_FAILURE`, or failed
`bdb_init` on a non-factory-new device.
Expected: bounded recovery under defined ownership.
Actual: `zb_rejoinReqWithBackOff` return (`u8`: RET_ILLEGAL_REQUEST or
zdo_status_t, `zigbee/zbapi/zb_api.h`) ignored; every callback restarts an
attempt (`glsd301p_telink_target.c:592-608`).
Fix (M4): IDLE / SDK_ACTIVE / RETRY_PENDING ownership; rejected start retries
no sooner than 5 s on the owned retry event; accepted starts keep SDK-owned
backoff; joined reconciliation via `zb_isDeviceJoinedNwk()`; success counted
once. Polling untouched.
Validation: hosted rejected/accepted/duplicate/startup/reconciliation tests.

## F4 — UART deadline counts callbacks, not time (Medium, FIXED @ 0f342c9)

Trigger: delayed/starved 1 ms IO service; in-flight frame with empty queue.
Expected: 32 ms deadline on actual elapsed time; in-flight tracked while busy.
Actual: `g_uart_pending_ms` increments per service call only while the queue is
non-empty (`glsd301p_telink_target.c:195-244`). A delayed IO tick stretches the
deadline; an accepted-but-never-completing DMA transfer is invisible.
Fix (M3): SDK `ev_timer_update` hook advances an app ms timebase (fractional
ticks stay in SDK `remSysTick`; wrap handled by unsigned clock delta in
`ev_timer_process`); oldest-queued age preserved across coalescing; in-flight
age tracked even with empty queue (fault only on busy persisting 32 ms; an
idle link retires the transfer, since faulting on an idle flag would brick
output on hardware whose TX DONE bit does not clear promptly on DMA start);
32 ms fault latches, cancels transitions, prioritizes OFF, retries OFF with
bounded O(1) work; ON rejected while gated.
OFF transfer attempt/completion is NOT acknowledged physical state.
Validation: hosted fractional/wrap/uint32-wrap, DMA-rejected/busy-forever,
empty-queue timeout, coalescing, OFF-priority, boot-failure tests.

## F5 — Boot UART path allocates and spins (Medium, FIXED @ 0f342c9)

Trigger: every boot (`glsd_uart_send_boot_off_blocking`).
Expected: bounded allocation-free boot OFF.
Actual: `drv_uart_tx_start` (`proj/drivers/drv_uart.c`) spins while not idle,
`ev_buf_allocate`s, and spins until the DMA start is accepted.
Fix (M3): aligned static boot DMA frame, single bounded `uart_dma_send`
attempt (no `uart_tx_is_busy` gate at boot: TX_DONE is clear after reset).
Output arms only after init + event registration + observed boot-OFF
completion (or 32 ms boot-timeout fault, locked OFF). Zigbee/OTA init proceeds
with output locked OFF on boot failure.
Validation: hosted boot accept/reject/completion/timeout tests.

## F6 — SDK ZCL parse reads past short frames (High, FIXED @ 7633e78)

Trigger: truncated Level MoveToLevel/Move/Step, OnOff OffWithEffect/
OnWithTimedOff payloads.
Expected: short frames rejected before reads.
Actual: `zcl_level_clientCmdHandler` and `zcl_onOff_clientCmdHandler`
(`zigbee/zcl/general/zcl_level.c`, `zcl_onoff.c`) read mandatory bytes without
a `dataLen` minimum; only optional fields are length-gated. Out-of-bounds read
precedes the application callback.
Fix (M4): narrow pinned-SDK patch with minimum-length guards returning a
malformed-command status. Anchored, hash-pinned, provenance-recorded.
Validation: hosted repro of the original OOB (ASan/UBSan where compatible)
plus patched-body regressions through real SDK dispatch.

## F7 — SDK ignores application callback status (Medium, FIXED @ 7633e78)

Trigger: any cluster command where the app returns non-success.
Expected: response status reflects the app verdict.
Actual: `pInMsg->clusterAppCb(...)` return is discarded in both handlers;
`status` stays SUCCESS into `zcl_rx_handler` default-response path
(`zigbee/zcl/zcl.c` dispatch: `status = pCluster->cmdHandlerFunc(&inMsg)`).
E.g. app UNSUP for OnWithTimedOff is reported as SUCCESS.
Fix (M4): same SDK patch propagates the app status.
Validation: hosted response-propagation tests.

## F8 — App cancels valid transitions before validating input (Medium, FIXED at PR head; see PR #8 milestone log)

Trigger: unknown OnOff command; Move with rate 0; reserved move/step modes.
Expected: malformed input never cancels a valid transition.
Actual: `glsd_onoff_cb` cancels before the command switch (`:462`);
`glsd_start_move` cancels before the rate-0 early return (`:431-435`);
moveMode/stepMode accept any value (`== LEVEL_MOVE_UP` else down).
Fix (M4): validate command/enum/rate/level/reserved/endpoint/bounds before any
state change; malformed input returns INVALID_FIELD with the transition
intact; reserved 0xFF still rejected; PUSH/OFF priority preserved.
OnWithTimedOff stays explicitly unsupported (now truthfully reported via F7).
Validation: hosted truncated/optional/enum/0xFF/endpoint/boundary/transition-
preservation tests through real dispatch.

## F9 — Reserved OffWithEffect ids accepted (Low, FIXED at PR head; see PR #8 milestone log)

Trigger: OffWithEffect with reserved effect id.
Expected: reserved enum rejected.
Actual: treated as plain OFF (`glsd_onoff_cb`), effect bytes ignored.
Fix (M4): reject reserved effect ids with INVALID_FIELD; keep plain-OFF
treatment for the two defined ids. No new power commands.
Validation: hosted enum tests.

## F10 — Not-ready commands latch permanent fault (High, FIXED @ 0f342c9)

Trigger: any energizing ZCL/local command while the runtime is not ready.
Expected: refused without latching; OFF already holds by construction.
Actual: `glsd_emit_runtime_result` maps every FORCED_OFF (including
NOT_READY) to `latch_fault`. Unreachable at baseline (synchronous boot
restore) but reachable once M3 defers arming past boot-OFF completion.
Fix (M3): not-ready/fault refusals return without latching, arming, or
touching transitions; OFF already holds by construction, so OFF reports
success while ON fails. Only genuine output-guard violations latch, and
fault clearing never restores ON.
Validation: hosted not-ready refusal tests for OnOff/Level/policy plus the
control-suite readiness/fault gates.

---

# Independent review R1-R8 (reviewed candidate 7184eec40d8141ccd92a005d2d6d5ef0c2bcb853)

Review: `HARDENING-INDEPENDENT-REVIEW-20261003.md` (verbatim copy, sha256
`7b783b25…bbe8b1`). Decision: **REQUEST CHANGES**. Remediation branch: same
`codex/glsd301p-client-hardening`, forward commits only; reviewed candidate
SHA and frozen baseline `760c1419` preserved in the ledger. Repro status
below means hosted reproduction of the original defect; each fix needs the
new regression green at the sealed SHA plus the G5/G6 evidence gates.

## R1 — Expired queued ON transmitted in the fault step (P1, CONFIRMED)

Source: `src/glsd301p_uart_service.c:82-112`,
`src/glsd301p_control.c:444-449` at 7184eec.
Trigger: queue ON at t=0; link rejects DMA starts or stays busy to t=31;
idle+accept at t=32. Service raises the deadline fault AND starts ON; OFF
is queued only after service returns. A newly faulted device energizes.
Original repro: hosted red run 37142138291 (`!ev.frame_sent` assert in
`test_r1_reject_to_accept_boundary_drops_stale_on` on pre-fix code).
Fix (G1 @ 6eb2bef): queue-deadline trip drops the expired normal slot
before any hardware start; no normal starts while latched; OFF never
dropped; IO fault path cancels the running transition at the fault step.
Validation: service + full-chain R1/R2 suites green at 6eb2bef
(boundary 37142423473, readiness 37142423428).

## R2 — Pending traffic hides the accepted DMA deadline (P1, CONFIRMED)

Source: `src/glsd301p_uart_service.c:82-135` at 7184eec.
Trigger: ON accepted at t=0, link stays busy, fresh frame queued at t=31,
serviced at t=32: queue age is 1 ms so no fault, although the original
transfer has been busy 32 ms. Coalescing can further move the observed
stamp.
Original repro: same G1 red run 37142138291 (suite aborts at the first
R1 assert; R2 variants traced to the same pre-fix service ordering).
Fix (G1 @ 6eb2bef): in-flight retire/check moved before pending-queue
handling on every step; stable start stamp; separate queue deadlines.
Validation: green at 6eb2bef (boundary 37142423473).

## R3 — Successful rejoin does not clear ownership (P1, CONFIRMED)

Source: `src/glsd301p_rejoin.c:30-37,95-109`,
`firmware/glsd301p-ed/glsd301p_telink_target.c:379-428` at 7184eec.
Trigger: initial join sets `last_joined=true`; parent loss starts an
accepted attempt (SDK_ACTIVE); the successful rejoin's `note_joined(true)`
sees the cached flag still true, returns false, leaves SDK_ACTIVE. A later
loss cannot start recovery. Rejected starts similarly strand RETRY_PENDING
(owned retry not stopped) on external success. The unit test feeds a
`false` the target never produces, so it misses this.
Original repro: hosted red run 37142619613 (`stop_retry == true`
assert in `test_adapter_two_accepted_cycles` on pre-fix code).
Fix (G2 @ 8bde993): shared hostable BDB adapter used by target and
harness; loss/failure observations invalidate the cached joined edge;
authoritative joined evidence always reconciles to IDLE + stops the app
retry; success counting stays edge-deduplicated; SDK backoff preserved;
single start site + ZDO_SUCCESS mapping + one-shot pacer unchanged; nm
gate now proves the target->adapter->rejoin chain.
Validation: green at 8bde993 (boundary 37142816643, readiness
37142816742).

## R4 — Small level changes finish long transitions early (P2, CONFIRMED)

Source: `src/glsd301p_control.c:322-365` at 7184eec.
Trigger: `ceil(diff / remaining_time)` with minimum step 1 per 100 ms
callback. One-level change with transitionTime 1000 (100 s) finishes in
the first 100 ms; 2->254 with that duration finishes in 25.2 s.
RemainingTime is callback-count based, so gaps distort duration too.
Original repro: hosted red run 37142931607 (`rc == 0` assert in
`test_r4_one_level_honors_duration` on pre-fix code).
Fix (G3 @ dd85a37): TARGET interpolates from origin/target/start/duration
on the ms timebase (floor, ceiling-tenths RemainingTime); MOVE integrates
levels/second over elapsed ms (milli-level accumulator, divide-first
u32-safe saturated steps — the confirmed Move rate distortion fixed too);
documented 0/0xFFFF-immediate policy; bounds, OFF/PUSH cancel,
replacement, no-overshoot preserved.
Validation: green at dd85a37 (boundary 37143088126, readiness
37143088132).

## R5 — Host regressions check the merge commit, not the head (P2, CONFIRMED)

Source: `.github/workflows/cleanroom-guard.yml:18,252-253` at 7184eec.
Run 37127418335 logs merge commit `2edf21c` (7184eec into 629a2c5) for
boundary-and-tests: integration evidence, not exact-head regression
evidence. (Readiness workflow already pins the head explicitly.)
Original repro: N/A (workflow semantics; run log is the evidence).
Proposed fix: pin implementation checkouts to
`github.event.pull_request.head.sha || github.sha` (same pattern as the
readiness workflow); assert/log actual HEAD; keep merge testing separate
if useful.
Fix (G5 @ 384ebb2): both `cleanroom-guard.yml` implementation checkouts
(boundary + TC32 jobs) pinned to the head pattern with an
assert-equal/log step; no separate merge-test job (branch is the sole
change source, nothing to integrate). All R1-R4/R6-R8 validation runs
above remain merge-commit evidence; this run re-establishes the full
suite as exact-head evidence.
Validation: green at 384ebb2 (boundary 37149777232, readiness
37149777209): SOURCE_HEAD == EXPECTED_HEAD == 384ebb2 in both jobs.

## R6 — Identify/Groups parsers read past malformed payloads (P1, CONFIRMED)

Source: pinned SDK `d5bc2f7b` `zigbee/zcl/general/zcl_identify.c:144,151-152,180`
and `zcl_group.c` Add/View/Remove/GetMembership/AddIfIdentify paths,
compiled into the target (`tools/build_glsd301p_ed_tc32.sh:93-94`).
Identify reads payload[0:2] unguarded; Groups reads group id unguarded and
`count` + `count*2` bytes unguarded (`{count=1}` alone over-reads), reaching
group-table ops before validation. (At finding time the harness staged
Level/OnOff only; G4 stages all six cluster bodies + both foundation
translation units.)
(Groups response-builder offset checked: intentional, not a defect.)
Original repro: hosted red run 37147368621 (ASan SEGV in
`zcl_identify_clientCmdHandler` via `test_r6_identify_truncated` on
pre-fix code); persistent 24-case pristine-body repro keeps failing on
original bodies (identify0/1, triggereffect1, queryrsp0, groupadd1,
groupview1, groupmember1 all ASan-fatal).
Fix (G4-B @ d3060dd, green head 20208cc): P3 identify exact-2/exact-0
guards; P4 group guards — add minimum-2 (trailing name defined content,
never parsed), remove/view/add-if-identifying exact-2, remove-all
exact-0, membership count-cap (INSUFFICIENT_SPACE past table size) +
exact 1+count*2 need — all before reads, callbacks, or table mutation,
both directions, MALFORMED_COMMAND truthfulness.
Validation: dispatch R6 suites + 24/24 pristine repro green at 20208cc
(boundary 37149493049, readiness 37149493059).

## R7 — Foundation parsers over-read; configure-report length wraps (P1, CONFIRMED)

Source: pinned SDK `d5bc2f7b` `zigbee/zcl/zcl.c` write parser 1266-1311,
configure-report parser 1709-1781, root dispatch 835-837 (status
normalization), compiled by `tools/build_glsd301p_ed_tc32.sh:89`.
Write loop skips 2 bytes and reads a type without a 3-byte record header;
`zcl_getAttrSize`+`memcpy` consume values without a bounded remaining
contract (1-byte payload already over-reads). Configure-report consumes
records unvalidated; allocation prefix uses **u8 len** (1742) for
`sizeof(command)+numAttr*sizeof(record)` (wraps at large counts while the
second pass writes numAttr records); discrete scan advances an extra byte
(1735) the second pass omits (miscount). Root dispatch erases foundation
error detail (835-837). (At finding time only Level/OnOff callbacks were
harnessed; G4 harnesses every foundation command + all six clusters.)
Original repro: hosted red run 37147368621 plus persistent pristine-body
cases (write1/cfgtrunc ASan-fatal, cfgwrap40 SEGV on the u8 wrap,
report4/readrsp4/discrsp0/dflt0/disc1-4/readodd/writersp2/cfgrsp3
pristine-accepted); P6 OTA cases otaqueryrsp2/otablockshort ASan-fatal
on original OTA bodies.
Fix (G4-B @ d3060dd, green head 20208cc): P5 bounded pre-parse
validators replicating each build-pass layout (write/report/read-rsp/
cfg/read-cfg-rsp, 255-record caps), read evenness, write/cfg-rsp exact
shapes (1 or 3k/4k), default-rsp exact-2, discover exact-3 / non-empty
responses (closes the dataLen==0 underflow), cfg scan/build desync
removal, all three u8 allocation lengths to u16, dispatch
normalization removal (specific statuses reach the default response);
P2a/P2b exact level/onoff lengths + callback-status propagation; P6
exact lengths for all 10 OTA request/response parsers (fc-bit options,
notify ladder, block dataSize). Division-based parsers (read-req
quotient, write/cfg-rsp quotients) audited: truncation only
under-reads, no overread; u16->u8 numAttr truncation likewise safe.
Fix (G4-B) also covers the read/read-rsp/write-rsp/cfg-rsp siblings the
original finding text did not enumerate (same file, same class).
Validation: dispatch R7 + follow-up suites (incl. 40-record wrap
battery with canary, pool-exhaustion fail-closed, OTA bounds/requests)
green at 20208cc (boundary 37149493049, readiness 37149493059);
TC32 target rebuild green on the same head.

## R8 — NULL foundation hook leaks parsed-command buffers (P1, CONFIRMED)

Source: target `glsd301p_telink_target.c:531` (`zcl_init(NULL)`); pinned SDK
`zigbee/zcl/zcl.c:866` (cleanup inside `if (hookFn && toAppFlg && attrCmd)`).
Read/Write allocate parsed commands into `pCmd->attrCmd` (1086-1090,
1325-1329); with a NULL hook ordinary requests never free them. Repeated
Basic reads (incl. health polling) can exhaust the shared event-buffer pool.
Original repro: hosted red run 37147368621 (R8 documented pre-fix);
persistent r8leak pristine case drains the pool to 16 free after 10
reads (10 parsed-command buffers leaked).
Fix (G4-B @ d3060dd, green head 20208cc): P5 splits the zcl.c:866
cleanup from the optional hook — hook still notified when present,
`attrCmd` freed and nulled whenever set (hookFn never frees, so no
double-free); the R8 dispatch case now asserts return-to-baseline with
the target's NULL hook (was leak documentation pre-fix).
Validation: R8 suites green at 20208cc (boundary 37149493049):
10 NULL-hook reads hold 26/26 free; pristine r8leak still reproduces.

# Independent review R9-R16 (reviewed candidate bc7196f028d466a12ea992c2f2c3aa9cc2f6c764)

Full text: `HARDENING-INDEPENDENT-REVIEW-20261004.md`.
Acceptance oracle per row is A9–A16/AP in the goal brief; no row passes
on compilation, grep, or prior-suite expectations alone.

## R9 — Repeated minimum-level With On/Off command can re-energize output (P1, CONFIRMED)

Source: `src/glsd301p_control.c:222,258,375` at bc7196f.
Trigger: ready + ON above minimum; immediate Move to Level With On/Off
to minimum emits OFF; repeating the identical command at minimum+OFF
takes the `target == current_level` equality path (`>=`), sets
`direction_up=1`, and emits ON. Descending transitions also derive
direction from rounded samples (unchanged sample reads as upward), and
`level > min_level` forces ON even when the semantic command decreases
an already-OFF level.
Original repro: TBD (M1 hosted adverse-behavior repro through real SDK
Level dispatch → production control → UART capture).
Proposed fix: base On/Off effects on the command's actual direction,
retained through interpolation; equality never invents an increase;
OFF preserved for decreasing commands from OFF; ON applied at onset of
a real increase, OFF on reaching minimum during a decrease; keep
fault/readiness/PUSH/OFF preemption intact.
Fix (M2 @ f0a2845, green 5937ddf): `target_dir` (+1/-1/0) retained
from dispatch through interpolation; With On/Off follows it only
(up: ON at onset; down: preserve until min, then OFF; equal:
preserve, never invent). MOVE-down sibling fixed via the same
policy. Documented choice: equal-at-minimum from ON preserves ON
(not a decrease, so no OFF mandate). One old unit expectation
corrected with justification (it asserted ON for a downward
WithOnOff from OFF — the defect itself).
Validation: 7 R9 matrix cases green @ 5937ddf (boundary
37221176908); AP suites green.

## R10 — Groups accepts malformed Add, rejects valid Add If Identifying (P2, CONFIRMED)

Source: `tools/apply_glsd301p_sdk_patches.py:294,335,372` (P4) and
`tests/test_glsd301p_zcl_dispatch.c:1169,1210,1260` at bc7196f.
P4 Add guards only two bytes, so a missing/truncated GroupName reaches
`aps_add_group_req` membership mutation; Add If Identifying demands
exactly two bytes although the pinned SDK serializer always emits the
string prefix (three-byte minimum, empty name included). Tests lock in
two-byte validity for both. Get Membership checks `< 1+2*count`
(minimum) while the R6 ledger claims exact shape.
Original repro: TBD (M1 SDK-serializer fixtures, truncated prefixes,
count/name mismatch, capacity/identifying boundaries, APS + response
observations).
Proposed fix: validate the complete uint16 ID + bounded string form
before mutation; accept valid empty/named inputs (unsupported names
ignorable only after validation); reconcile the membership-length
policy with exact-record validation.
M1 repro: adverse-grammar matrix red at b9a1025 (boundary 37220219614).
Fix (M3 @ 9ac2c85, green 7c0cf10): P4v2 requires the exact
uint16+counted-string form (namelen <= 15, exact 3+namelen length, no
0xFF/trailing) for Add and AddIf before any mutation; GetMembership
requires exact 1+2*count. Two-byte Add/AddIf now MALFORMED; old R6
valid-shape tests moved to the serializer shape with justification.
Validation: r10_add_names + r10_addif_member green @ 7c0cf10
(boundary 37223816991); AP suites green.

## R11 — Foundation validators still accept incomplete record shapes (P2, CONFIRMED)

Source: P5 in `tools/apply_glsd301p_sdk_patches.py:806,819,845,858`;
pinned `zcl.c` `zcl_parseInReadReportCfgCmd`,
`zcl_parseInDiscAttrsRspCmd`, `zcl_parseInDiscAttrsExtRspCmd`,
`zcl_parseInWriteRspCmd`, `zcl_parseInCfgReportRspCmd` at bc7196f.
Read Reporting Configuration divides `dataLen/3` without requiring
whole records (trailing suffix silently discarded). Discover responses
check only the leading byte (count truncates incomplete suffixes).
Write/Configure-Reporting response guards accept any one-byte status
although failures need record fields (success-only short form not
status-checked). Accepted-malformed-input defects (no new OOB claim).
Original repro: TBD (M1 real root/foundation grammar matrix).
Proposed fix: command-specific complete-record validation incl.
status-dependent forms, reserved directions/types, empty-payload
legality, supported compound types; truthful rejection; no uniform
fixed length on variable records.
M1 repro: adverse-grammar matrix red at b9a1025 (boundary 37220219614).
Fix (M3 @ 7c0cf10): P5v2 adds a read-cfg request validator (whole
3-byte records, defined directions, non-empty) + hook; discover
rsp/ext require whole 1+3n/1+4n records; write/cfg-rsp 1-byte form is
success-only; write/report/read-rsp/read-cfg-rsp reject empty frames.
Validation: 5 R11 matrix cases green @ 7c0cf10 (boundary
37223816991); AP suites green.

## R12 — Read Reporting Configuration response status leaks between records (P2, CONFIRMED)

Source: pinned `zcl.c:1968-2020`; P5 only widens the allocation
(`apply_glsd301p_sdk_patches.py:664`) at bc7196f. `status` starts as
SUCCESS outside the response loop; an unknown attribute sets
UNSUPPORTED_ATTRIBUTE and a later configured attribute fills its
fields without resetting status, so both records carry the first
failure status. Existing tests miss mixed success/failure ordering.
Original repro: TBD (M1 mixed orderings through production root
dispatch with decoded wire statuses).
Proposed fix: independently initialize/derive each record's status;
keep cleanup ownership correct.
M1 repro: mixed-ordering matrix red at b9a1025 (boundary 37220219614).
Fix (M3 @ 7c0cf10): P5v2 initializes the builder status to SUCCESS at
the top of each response-record iteration (send-result reuse after the
loop unchanged).
Validation: r12_status + r12_alloc green @ 7c0cf10 (boundary
37223816991); AP suites green.

## R13 — Identify acknowledges success without implementing its effect (P2, CONFIRMED)

Source: `firmware/glsd301p-ed/glsd301p_telink_target.c:243` and
`tests/test_glsd301p_zcl_dispatch.c:168,1810` at bc7196f. The
registered callback ignores payloads and returns SUCCESS; the SDK
passes IdentifyTime to the app instead of applying it. Identify(5)
leaves IdentifyTime at zero, Query emits no identifying response, Add
Group If Identifying cannot be enabled by the accepted command. Tests
lock in no-effect behavior and hand-set time for Groups coverage.
Original repro: TBD (M1 production-adapter Identify→Query→AddIf→
expiry/stop/restart chain, output unchanged).
Proposed fix: target-shared bounded RAM IdentifyTime with owned
countdown + Query/Groups integration incl. the attribute-write path;
power output unaffected; physical-identification limits documented;
unsupported Trigger Effect semantics rejected truthfully (no
fabricated success, no power-stage blink).
M1 repro: adapter chain red at b9a1025 (boundary 37220219614).
Fix (M3 @ 264c7cb..9b25107, green 7c0cf10): new `glsd301p_identify`
adapter (shared-store countdown on the household tick, command +
write paths with ceiling adopt, Query honest via store); Blink/Breathe
one-shots through the guarded emit path with restore, abort on any
remote/local/fault preemption; reserved ids/variants INVALID_FIELD.
M3 correction: the M1 "accepted-without-blink" contract was the
defect itself (a dimmer can modulate; SUCCESS with no visible program
lies to the commissioner), so defined effects run real bounded
programs; the no-op-locking followup was corrected with justification.
Validation: 3 R13 matrix cases green @ 7c0cf10 (boundary 37223816991);
AP suites green.

## R14 — New firmware reuses the previous development image identity (P2, CONFIRMED)

Source: `firmware/glsd301p-ed/version_cfg.h:7,14` + identity
assertions at bc7196f. Still APP_BUILD `0x03`, FILE_VERSION
`0x7F030001`, GLSD-ED-002 despite the prior goal's fresh-identity
requirement; different bits share the earlier quarantined identity.
Original repro: N/A (source/identity comparison).
Proposed fix: allocate a fresh unused development identity from
repository/issue history; update source, assertions, wrapper
metadata, docs consistently; preserve quarantine (no publication or
eligibility change).
Fix commit: TBD. Hosted proof: TBD.

## R15 — Handoff ledger and reproducibility evidence are incomplete (P2, CONFIRMED)

Source: PR #8 body; `HARDENING-CHECKPOINT.md`,
`HARDENING-INGRESS-MAP.md`, `HARDENING-FINDINGS.md`; both hosted
workflows at bc7196f. PR body still describes `7184eec` + old hashes;
checkpoint has contradictory complete/pending rows; ingress map keeps
older unguarded/pending claims; R6 claims exact Membership length
while code checks a minimum; only one wrapped OTA build evidenced
(boundary job wraps no OTA), so two matching wrapped artifacts are
unproved.
Original repro: N/A (document/evidence comparison).
Proposed fix: single current acceptance ledger (finding→fix→
regression→raw evidence), history preserved; after sealing, two
independent clean hosted TC32 + quarantine-wrapper runs at one final
SHA with full provenance and matching ELF/raw/final/MAP/OTA hashes;
PR body + issue result updated; remaining limits stated.
Fix commit: TBD. Hosted proof: TBD.

## R16 — Clamped Step retains the full unclamped transition time (P2, CONFIRMED)

Source: `src/glsd301p_zcl_commands.c:106-127` at bc7196f. Step clamps
its target to min/max but forwards unchanged `transitionTime`, so a
4-unit clipped move (e.g. 250→254 of a 40-unit/10 s Step Up) takes
the full 10 s instead of ~1 s proportional.
Original repro: TBD (M1 real SDK Step dispatch, both bounds,
unclamped controls, zero/reserved times, gaps/wrap, RemainingTime +
captured frames; negative control at bc7196f).
Proposed fix: proportionally reduce finite Step duration on clipping
with bounded arithmetic + explicit rounding; preserve
immediate/reserved conventions and zero-step semantics.
Fix (M2 @ f0a2845, green 5937ddf): shared
`glsd301p_control_proportional_time` (ceil, bounded u32) applied to
Step spans in `zcl_commands.c` and to below-min MoveToLevel spans in
`start_target` (same class); fully-clipped targets still
short-circuit as immediate.
Validation: 3 R16 matrix cases green @ 5937ddf (boundary
37221176908); AP suites green.

# Independent review R17-R23 (reviewed candidate 69831aa5c9c230bb8c3c8074abd9c618eb936ed5)

Full text: `HARDENING-INDEPENDENT-REVIEW-20261005.md`.
Acceptance oracle per row is A17–A24 in
`HARDENING-MUSE-REMEDIATION-TASKS-20261005.md`; no row passes on
compilation, grep, or prior-suite expectations alone. OUTPUT AUTHORITY:
Identify/Trigger Effect stay output-neutral (RAM commissioning state
only); no load-identification mechanism replaces the removed overlay.

## R17 — Trigger Effect energizes/modulates the load (P1, CONFIRMED)

Source: `src/glsd301p_identify.c:78`–`:160`,
`src/glsd301p_control.c:233`–`:305`, `:493`–`:525`,
`firmware/glsd301p-ed/glsd301p_telink_target.c:270`,
`tests/test_glsd301p_zcl_dispatch.c:3045` at 69831aa.
Trigger: Trigger Effect Blink/Breathe from OFF through real SDK
dispatch. Blink emits ON at maximum, Breathe forces ON; the
saved-output restore can re-energize later; control preemption paths
entangle effects with transitions. The authorized contract requires
RAM IdentifyTime/countdown with power output unchanged.
Original repro: M1 red at b28f454 (boundary 37273719102):
r17_trigger FAILs on SUCCESS-vs-INVALID_FIELD; r17_cmd PASS-NOW.
Proposed fix: delete the power-effect overlay, saved-state restore
and effect preemption; reject unsupported optional effects with a
truthful status before any state/timer/transition/UART mutation; keep
bounded RAM commissioning semantics; document the lack of physical
identification.
Fix commit: TBD. Hosted proof: TBD.

## R18 — IdentifyTime writes inherit the old timer phase (P2, CONFIRMED)

Source: `src/glsd301p_identify.c:49`–`:70` at 69831aa; target keeps
`zcl_init(NULL)`.
Trigger: second mark 0, write IdentifyTime=1 at 999 ms → adopted at
1000 ms and immediately decremented (≈1 ms of identification). Equal
writes never restart; post-gap writes are charged pre-write elapsed
time. Detection is a store-vs-shadow comparison at an old boundary,
not a receipt-time event.
Original repro: M1 red at b28f454 (boundary 37273719102):
r18_write FAILs (write-1 at t=999 already 0 at t=1500).
Proposed fix: bounded production observer/shared adapter for accepted
IdentifyTime writes (receipt time + value incl. same-value writes);
start/restart at the write's time; failed/wrong-type/wrong-endpoint
writes never restart; NULL-hook cleanup and per-record semantics
preserved.
Fix commit: TBD. Hosted proof: TBD.

## R19 — Identify catch-up loops per elapsed second (P2, CONFIRMED)

Source: `src/glsd301p_identify.c:49`,
`src/glsd301p_control.c:659` at 69831aa.
Trigger: a 24 h service gap iterates 86400 times (near-wrap gaps up
to ~4.29M) before PUSH sampling/UART service, even with the countdown
at zero. Operation count is source-apparent; no on-target duration
claimed.
Original repro: M1 red at b28f454 (boundary 37273719102):
r19_bound FAILs (200 catch-up steps in one 200 s IO step).
Proposed fix: O(1) elapsed whole-second arithmetic with residual
phase and saturating decrement; no catch-up work for a disabled
countdown; unsigned wrap conventions preserved; no silent-loss caps
or timebase-unit changes.
Fix commit: TBD. Hosted proof: TBD.

## R20 — Move to Level shortened by Step clipping policy (P2, CONFIRMED)

Source: `src/glsd301p_control.c:376`–`:393` at 69831aa (Step
counterpart `src/glsd301p_zcl_commands.c:123`–`:141`).
Trigger: CurrentLevel=10, MinLevel=2, accepted Move to Level(0,
transitionTime=100) schedules the 8-unit clamped move for 80 tenths
instead of the requested 100. Proportional reduction belongs to
clipped Steps only.
Original repro: M1 red at b28f454 (boundary 37273719102):
r20_duration FAILs (remaining 80, expected 100).
Proposed fix: preserve the accepted/clamped Move to Level policy and
requested finite duration; keep proportional timing at Step dispatch
only with its bounded arithmetic/rounding; no acceptance-policy
change to dodge the regression.
Fix commit: TBD. Hosted proof: TBD.

## R21 — Upward TARGET ON delayed to the first 100 ms tick (P2, CONFIRMED)

Source: `src/glsd301p_control.c:401`–`:420`, `:570`–`:574`,
`src/glsd301p_timer_events.h:37`,
`tests/test_glsd301p_zcl_dispatch.c:2228` at 69831aa.
Trigger: OFF at level 16, upward Move to Level With On/Off with
nonzero duration returns SUCCESS but leaves mirror/runtime OFF until
the first 100 ms callback (STEP shares the path; MOVE already applies
at onset). The existing regression observes after 150 ms, proving
early-in-transition behavior, not command-onset effect.
Original repro: M1 red at b28f454 (boundary 37273719102):
r21_onset FAILs (runtime OFF immediately after admission).
Proposed fix: apply a real accepted increase at admission, before
dispatch returns; keep equality/downward/fault/readiness/OFF
invariants; rejected/failed admissions change nothing; emit through
the guarded transport without claiming instant physical output.
Fix commit: TBD. Hosted proof: TBD.

## R22 — Prevalidators accept malformed typed records (P2, CONFIRMED)

Source: `tools/apply_glsd301p_sdk_patches.py:420`–`:475`, `:542`–`:579`,
`:585`–`:631`, `:928` + pinned `zcl.c` parsers at 69831aa.
Trigger (each accepted today): Report `00 00 4c 00 01` (STRUCT count
high byte ignored); Report `00 00 4c 01 00 4c` (missing nested body
read as zero-length scalar); Report `00 00 ff` (reserved datatype,
zero size); Configure Reporting / read-cfg-response with reserved
direction 2 (any nonzero treated as receive form); Configure
Reporting Response long forms validated for length only.
Accepted-malformed-input defects (no new overwrite claim).
Original repro: M1 red at b28f454 (boundary 37273719102):
r22_struct + r22_direction FAIL (malformed shapes accepted).
Proposed fix: explicit supported datatype grammar; full wire count
width/sentinels; truthful rejection of unsupported compound forms
before unsafe SDK size logic (no unbounded recursion); valid flat
structures/strings stay compatible with bounded nesting/count policy;
zero-length vs unknown types distinguished; defined reporting
directions + status-dependent long records validated.
Fix commit: TBD. Hosted proof: TBD.

## R23 — Target foundation ABI proof + current checkpoint missing (P2, CONFIRMED)

Source: `tools/build_glsd301p_ed_tc32.sh:276`–`:308`,
`devices/gl-sd-301p/HARDENING-CHECKPOINT.md:156`, `:245`,
Identify target callback vs
`tests/test_glsd301p_zcl_dispatch.c:168` at 69831aa.
Trigger: the ABI probe covers cluster-registration/timer structures
only; foundation record sizes/allocation thresholds are a residual
limit, not executed target evidence; the harness Identify callback
is a mirror, not proof of production write-observer wiring; the
checkpoint keeps pending M4/M5 rows + a stale build next action
against a declared-complete PR/issue.
Original repro: N/A (evidence/ledger comparison; M1 records the gap).
Proposed fix: target-compiled probes for foundation layouts/pool
thresholds in both flag contexts; tested Identify command/write
adapter tied to production code; host-harness layout comparison with
explicit difference guards; one current matrix with labeled history;
coherent checkpoint/ingress/PR/issue records.
Fix commit: TBD. Hosted proof: TBD.