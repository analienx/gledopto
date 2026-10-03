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