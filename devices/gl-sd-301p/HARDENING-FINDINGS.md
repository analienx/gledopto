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
Fix commit: TBD. Hosted proof: TBD.

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