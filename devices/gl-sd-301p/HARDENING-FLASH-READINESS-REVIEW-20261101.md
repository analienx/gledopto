# GL-SD-301P flash-readiness review and remediation — 2026-11-01

Scope: can the PR #8 candidate (`codex/glsd301p-client-hardening` @ `109b6b1`)
be flashed to the single production unit `0xa4c13850cfcdb3a4` over Zigbee OTA
and still be recovered wirelessly. Reference implementation for comparison:
`tuya-zigbee-switch` (BSEED mains End Device, same TLSR8258 / Telink SDK
V3.7.2.0 lineage).

This document records findings and the code that addresses them. It grants no
deployment, device or publication authority.

## Confirmed correct before this change

| Check | Evidence |
|---|---|
| Stock OTA client accepts the candidate identity | Stock and vendor OTA are `0x124F` / `0x1416` (Telink sample family); candidate uses the same pair with a higher file version than the running stock `0x26013001`. |
| Images fit the 512 KiB dual-bank slot | Candidate final image < `0x34000`; vendor recovery inner image 208 884 bytes < `0x34000`. |
| Custom OTA client accepts the vendor downgrade | WWAH disabled, so the pinned SDK's lower-version rejection is compiled out (`tools/check_telink_ota_recovery_contract.py`). |
| Role/pin/boot-OFF invariants | Compile-time firewall, SDK pin asserts, first UART frame `A5 5A 01 00 04 AA`. |

## Findings and remediation

### F1 — Factory-new device could never commission again (blocker)

Nothing called `bdb_networkSteerStart()`; touchlink is disabled and the pinned
SDK's `bdb_init()` never steers on its own (the SDK samples schedule steering
explicitly). `glsd301p_rejoin` deliberately ignored factory-new devices. Any
path to factory-new — stock network NV the custom stack cannot restore, a
coordinator Leave ("remove"), a future reset — left the device permanently
off-network, which also made the vendor OTA recovery undeliverable.

Fix:

- `src/glsd301p_rejoin.{h,c}`: STEERING / STEER_WAIT states owned by the same
  state field as rejoin (never overlapping). Level-triggered steering from a
  1 s tick once BDB init has reported; backoff 5 s doubling to 300 s; 180 s
  watchdog for attempts that never report a terminal status; joined
  observations reconcile even if a commissioning callback is missed; network
  state appearing during the wait hands back to rejoin.
- `src/glsd301p_bdb_adapter.{h,c}`: `GLSD301P_BDB_STEER_FAILURE` event,
  BDB-ready marking on every init event, `glsd301p_bdb_tick()`.
- Target: all terminal non-success commissioning statuses map to
  `STEER_FAILURE`; `IN_PROGRESS` / `FORMATION_DONE` are ignored; the 1 s owned
  health event now also runs `glsd_maintenance_tick()` (no pooled timers).
- Tests: nine new cases in `tests/test_glsd301p_rejoin.c` (init gating, never
  over preserved state or an active rejoin, success, backoff sequence and cap,
  refused start, SUCCESS-without-join, watchdog, missed-callback
  reconciliation, operator restart, rejoin-trigger isolation).

Policy note: `WIRELESS_EXTRACTION_TRANSFER.md` §7.3 forbids *initializing* as
factory new because the parser rejects stock NV. This change keeps that: it
never erases or overwrites state the stack restored; it only steers when the
stack itself already reports factory new, where staying off-network would
forfeit recovery.

### F2 — No physical reset path and no reboot on Leave (high)

Fix: ten short PUSH activations (≤ 1000 ms apart) request a network reset
(`glsd301p_control_note_push_event` / `..._take_network_reset_request`, fed
from the IO step via `runtime_core.last_push_event`). The target performs
`zb_factoryReset()` and reboots within 10 s; on a factory-new device it just
restarts steering immediately. A successful own-Leave confirm
(`glsd_leave_cnf_cb`) reboots after 2 s like the SDK samples. Tests: four new
cases in `tests/test_glsd301p_control.c` (ten presses through the real PC2
decoder, pause restart, hold/NONE/gap-boundary/wrap rules, hold path).

### F3 — Stock-NV compatibility unproven and ungated (high, residual)

Cannot be proven offline (the stock stack-release byte `0x30` is identical
across Telink 3.x samples). With F1/F2 fixed, an incompatible NV now degrades
to "re-pair with permit-join" instead of "stranded". The build manifest
declares `COMMISSIONING=…`; `make_glsd301p_ed_ota.py` requires it and records
`architecture.factoryNewCommissioning`; the final preflight blocks with
`FACTORY_NEW_COMMISSIONING` if it is absent. The residual-risk text now states
the NV uncertainty explicitly.

### F4 — Recovery delivery not planned; custom identity unknown to Zigbee2MQTT (medium)

- `devices/gl-sd-301p/zigbee2mqtt/glsd301p_ed.js`: external converter for
  `GL-SD-301P-ED` / `analienx` with `ota: true` (light + identify, no
  power-on-behavior or effect controls). Gated in CI by
  `tools/check_z2m_converter.js` (`z2m-converter.yml`, pinned
  zigbee-herdsman-converters 26.125.0 and latest), which also checks it
  against the Basic identity parsed from the firmware source.
- `tools/glsd301p_release_plan.py`: `--recovery-url`; the plan is never
  decision-ready without a recovery request.
- Zigbee2MQTT semantics, verified from the published sources of
  zigbee2mqtt 2.1.3-2.14.3 and zigbee-herdsman 11.2.0 (not assumed):
  the request `url` is honoured from 2.9.0; a URL ending in `.json` is read
  as an OTA index, anything else as the image itself; a direct image is
  served only if newer, or older via the `ota_update/update/downgrade` topic;
  direct images are not hash-checked and bypass the definition's `ota` flag.
  The first plan draft used the plain update topic for recovery, which
  Zigbee2MQTT would have answered with "no image available". The plan now
  uses the `/downgrade` topic for recovery, rejects `.json` URLs, records
  the minimum version, and carries the expected SHA-512 of the served bytes
  for operator verification.

### F5 — StartUp attributes accepted writes that were ignored (low)

`StartUpOnOff` and `StartUpCurrentLevel` are now read-only and report the real
fixed behavior (OFF, `0xFE`). The hosted ZCL table mirror was updated.

### F6 — OTA client never queried (low)

`ota_queryStart(43200)` after every join, as in the SDK samples and
`tuya-zigbee-switch`.

### F7 — Over-the-air identity contradicted the build manifest (low)

The manifest declared `GLSD-ED-005` / `20261005`; Basic reported
`GLSD-ED-002` / `20261003` / appVersion 1. Identity is now `GLSD-ED-006`,
APP_BUILD `0x07`, FILE_VERSION `0x7F070001`, date `20261101`, and the TC32
build script fails if the Basic strings drift from the declared identity.

## Local verification (2026-11-01)

- 64-bit legs of `cleanroom-guard.yml` executed verbatim in WSL against the
  pinned SDK `d5bc2f7b`: all host harnesses (ASan/UBSan) including the new
  rejoin/steering and gesture tests, health decoder fixtures and the
  architecture firewall (with the new commissioning gates) PASS.
  `INTEROP_BOUNDARY_GUARD=PASS`; `tools/tests` 22/22 PASS.
- Real TC32 build with the hash-pinned toolchain
  (`33b854be…b430`): compile/link/finalize PASS, no warnings,
  `TELINK_FINAL_IMAGE=PASS fileVersion=0x7f070001`; new symbols present in
  the linked ELF; OTA wrapper packaging PASS.
- Not run locally (no 32-bit multilib): the `-m32` ZCL dispatch, mutant and
  original-body legs; covered by hosted CI below.

## Hosted CI and reproducibility (head `048d2f1`)

- `cleanroom-guard.yml`: all legs PASS, including `-m32` ZCL dispatch,
  R25/R26 mutants KILLED and the original-body reproduction.
- `glsd301p-final-readiness.yml`: `NO_SPARE_FINAL_PREFLIGHT_POLICY=PASS_AUTH_FALSE`,
  `EXACT_IEEE_INERT_RELEASE_PLAN=PASS_AUTH_FALSE_RECOVERY_PLANNED`.
- `z2m-converter.yml`: PASS against zigbee-herdsman-converters 26.125.0 and
  latest.
- Fixed a latent CI flake on the way: `tc32-elf-nm | grep -q` under
  `pipefail` could report a present symbol as missing (SIGPIPE).
- Byte-identical across PR run 41289086530, dispatch rebuild 41289174940 and
  the local WSL build: `glsd301p-ed.final.bin` 141 572 bytes, SHA-256
  `adb252bcb6db81c0d2352f167b8daaa2a05a3085059ab2e151c2dc3ed2fe6492`.

## Independent acceptance review of `109b6b1..cedf0d3`

A separate reviewer, with no part in writing the code, reviewed the
remediation against the pinned SDK source. Verdict: wireless recovery to the
vendor image was **not yet guaranteed**. Every finding was re-checked against
the SDK source before fixing (`defad51`). Identity bumped to `GLSD-ED-007` /
APP_BUILD `0x08` / `0x7F080001` so the reviewed `0x7F070001` bytes and the
fixed bytes can never be confused.

| # | Finding | Disposition |
|---|---|---|
| A1 | The leave-confirm reboot also fired on the Leave BDB sends itself after a failed TC link-key exchange (`bdb.c` `bdb_retrieveTcLinkKeyTimeout`), so the RAM backoff was lost and the device could loop associate → leave → reboot. | Fixed. The Leave origin is now told apart: gesture → reboot; during STEERING → no reboot; otherwise → coordinator removal. |
| A2 | The tick treated `joined` as success during the TCLK window, so the later `TCLK_EX_FAILURE` was ignored and the backoff never grew. | Fixed. An accepted attempt ends only on a terminal status or the watchdog. New test `test_steer_tclk_window_failure_backs_off`. |
| A3 | Network state that was neither joined nor factory new, with nothing outstanding, was never driven back onto a network. | Fixed. A rejoin attempt is started every 60 s. New test `test_stranded_network_state_rejoin_kick`. |
| A4 | When the device was not joined, the gesture relied on library-only `zb_factoryReset()` behaviour, so it could reboot still non-factory-new. | Fixed. When not joined, the gesture reboots immediately. Before any gesture reboot, `nv_resetToFactoryNew()` (SDK source, the same routine as `tl_bdbReset2FN`) runs if the stack still reports network state. |
| A5 | A failed or aborted OTA transfer never re-armed the periodic query. | Fixed, as in the SDK sample (`ota_queryStart` on a non-success `OTA_EVT_COMPLETE`). |
| A6 | The 1 s event now carries recovery behaviour, but its registration result was discarded. | Fixed. Registration is retried from every BDB callback. |
| A7 | The missed-callback reconciliation did not run the join side effects (OTA query). | Fixed. The tick returns `JOINED` → `stop_retry` + `glsd_note_joined()`. |
| A8 | The release plan copied the vendor SHA-512 and fileVersion unchecked, so it could plan recovery with a null hash. | Fixed. A recovery request needs the pinned vendor SHA-512 and fileVersion and a strict downgrade. Six negative test variants. |
| A9 | After a coordinator removal the device steered forever and re-joined on the next unrelated permit-join. | **Withdrawn in round 3**: dormant removed (see the third review). |
| A10 | The steering counters and state were not observable on the device. | Fixed. Basic:0xFF10 byte 47 (formerly reserved, zero) carries the state and steering failures (bit 3 reserved); the decoder and fixtures were updated. |

`zb_factoryReset()` and the exact moment the stack becomes factory new are
inside the binary-only `libzb_ed.a` and cannot be checked offline. A4 is
designed so the outcome does not depend on them.

## Second independent review of the fixes (`cedf0d3..8b05a11`)

A second separate reviewer checked the A1–A10 fixes. Every finding was
re-checked against the SDK source, and the fixes below came after the
`8b05a11` build.

| # | Finding | Disposition |
|---|---|---|
| B1 | Any non-gesture own Leave outside steering made the device dormant, and dormant blocked rejoin. The Leave confirm carries no rejoin flag, so a Leave with rejoin=1 could strand a device that kept its network state. | Superseded in round 3: dormant removed. |
| B2 | A real removal during STEERING (joined, before the success callback) was taken for BDB's TCLK Leave, so the device re-steered. | Superseded in round 3: dormant removed, no Leave classification. |
| B3 | When the abort came from inside an `otaTimer` callback (`ota_ieeeAddrRspWait`), the OTA re-query did nothing: `ev_timer_exist` was still true, and the timer was unregistered right after. | Fixed. The re-query is deferred to the next 1 s tick (`g_ota_requery_pending`). |
| B4 | When `ota_mcuReboot()` returned without switching (invalid image or failed flag write), the device was left with stale RAM transfer state. | Fixed. It reboots onto the current valid image (`glsd_arm_reboot(1u)`). |
| B5 | SDK_ACTIVE had no exit if the stack's backoff stalled. | Fixed. A 900 s watchdog moves it to IDLE, and the stranded kick starts a fresh attempt. |
| B6 | A rejoin could start during the gesture's reboot countdown. | Fixed. `glsd_rejoin_start_attempt` is gated on `g_reset_to_fn`. |
| B7 | The release plan's downgrade check trusted the unbound sidecar `fileVersion`. | Fixed. The version is parsed from the candidate OTA header, and both the sidecar and the preflight must match it. |
| B8 | A join reconciled by the tick left the health network state at JOINING. | Fixed. |
| B9 | The docs implied the device retries an aborted recovery by itself, but Zigbee2MQTT only offers images on explicit requests. | Fixed: SAFETY.md and the comments corrected. The plan now records `abortedTransferRecovery = RESEND_SAME_REQUEST` and `deviceSelfRetriesRecovery = false`. |
| B10 | STATUS.md still gave the old identity. | Fixed. |

## Third independent review (`8b05a11..403df5a`)

A third separate reviewer checked the round-2 fixes. The main result was a
design conclusion. The "dormant after removal" behaviour (A9, then B1/B2)
depended on telling Leave origins apart through event order and timing
inside the closed `libzb_ed.a`. Each variant still had a path to a
factory-new device that never steers again, and that conflicts with the
project's primary goal of always being wirelessly recoverable. **Dormant is
removed, and A9 is withdrawn.** A removed unit re-joins on the next
permit-join, like most Zigbee devices; use the Zigbee2MQTT `blocklist` to
keep it out. A CI gate now forbids any dormant/suppressed connectivity
state.

| # | Finding | Disposition |
|---|---|---|
| C1 | A BDB Leave confirm that arrived after `TCLK_EX_FAILURE` was taken for a removal, leaving the device permanently dormant. | Resolved by removing dormant. Only the gesture's Leave reboots; nothing else reacts to a Leave. |
| C2 | Timing-window classification instead of `g_bdbCtx.leaveDoing`. | Moot: no classification is needed anymore. |
| C3 | A real mid-commissioning removal could be "explained" by a later TCLK failure. | Moot: removal is not special-cased. |
| C4 | The SDK can stay in DOWNLOAD_IN_PROGRESS without a completion event (`ota.c` buffer allocation failure), refusing every Image Notify. | Confirmed in source. The first fix (a 300 s no-progress watchdog) was replaced in round 4 by SDK patch P7 at the source. |
| C5 | The 900 s SDK_ACTIVE watchdog may restart a stack backoff that is still running. | Kept on purpose. Whether the stack backoff ever ends is decided inside the closed library. The worst case of the watchdog is a refused start retried every 5 s, which is harmless; the worst case without it is a stranded device. |
| C6 | RETRY_PENDING/SDK_ACTIVE never yield to steering if the device became factory new. | Fixed: both states hand over to steering on a factory-new device. Tests added. |
| C7 | Use `TL_SCHEDULE_TASK` instead of the re-query flag. | Not adopted: the repo avoids pooled SDK scheduling; the flag runs on the owned 1 s event. |
| C8 | Redundant `CUSTOM_FILE_VERSION` check in the release plan (`validate_ota` already pins it). | Removed. |
| C9 | The plan reported `candidate.fileVersion` from the sidecar, not from the header. | Fixed: it reports the header value. |

## Fourth independent review (`403df5a..6b24ca9`)

The reviewer found no reboot loop, no flash-wear loop and no permanently
off-network path, and confirmed that removing dormant was safe. The
findings concerned the round-3 OTA stall watchdog:

| # | Finding | Disposition |
|---|---|---|
| D1 | `ota_upgradeAbort()` leaves `otaTimer` armed, so the same-tick re-query did nothing and the periodic query could be lost; stale block-request chains could continue. | Resolved: the watchdog is removed. |
| D2 | Server-paced transfers (WAIT_FOR_DATA) legitimately hold the offset still for more than 300 s, so a healthy recovery could be aborted. | Resolved: the watchdog is removed. |
| D3 | The SDK_ACTIVE → steering handover does not stop a stack backoff that may still be running, so steering can be refused for a while; the handover also counted as a failure. | Accepted residual: there is no public API to stop it (`zb_api.h` has only `zb_rejoinReqWithBackOff`). A refused steer backs off to at most 300 s and retries, so it always converges. The counter is made consistent (a handover is not a failure). |
| D4 | The stall abort sends no Upgrade End (ABORT) to the server. | Resolved: the watchdog is removed. |
| D5 | The stall counter carried over between back-to-back transfers. | Resolved: the watchdog is removed. |
| D6 | Fix the root cause in the SDK instead of using a heuristic. | **Adopted.** Pinned SDK patch **P7-ota-client-no-stuck-download** (`zigbee/ota/ota.c`, original `8e1a92ec…5848`, patched `36b18b57…fa0b`) calls `ota_upgradeComplete(ZCL_STA_ABORT)` on both early returns of the Query Next Image Response handler. That restores NORMAL, frees the state and reports completion, so the app re-queries. A CI gate requires P7 and forbids application abort heuristics. |
| D7 | Removing `note_own_leave` also removed the only place that cleared the join edge on a non-gesture Leave, so the next join was not counted in the health success counter. | Fixed: any successful non-gesture own Leave records not-joined. |
| D8 | The watchdog had no behavioural test. | Resolved: the watchdog is removed. P7 is hash-pinned and anchor-checked. |

### Current candidate: hosted CI and reproducibility (head `2765e36`)

- All three workflows PASS on `2765e36`: `GLSD301P_SDK_PATCHES_APPLIED=8`,
  with P7 at the pinned original and patched hashes in both the harness and
  the TC32 build; `TARGET_OTA_STUCK_DOWNLOAD_FIXED_AT_SDK_SOURCE_P7=PASS`;
  `TARGET_LEAVE_ONLY_GESTURE_REBOOTS_NO_DORMANT=PASS`;
  `GLSD301P_REJOIN=PASS`; the preflight and release-plan policies are PASS
  with authorization false.
- The PR run 41316758236 and the dispatch rebuild 41316819468 are
  byte-identical: **`glsd301p-ed.final.bin` is 142 212 bytes,
  `fileVersion=0x7f080001`, SHA-256
  `6553198eff28abffa728d0748cc38334a02bde473b157b7a822a78ab5279fec9`,
  SHA-512 `40a058aa…f84a120b5`.** Any canary must bind to these bytes.

### Hosted CI and reproducibility for the round-3 changes (head `6b24ca9`, superseded)

- All three workflows PASS on `6b24ca9`, including
  `TARGET_LEAVE_ONLY_GESTURE_REBOOTS_NO_DORMANT=PASS` and
  `TARGET_OTA_STALL_WATCHDOG=PASS`. The PR run 41315955219 and the dispatch
  rebuild 41316013968 are byte-identical: `glsd301p-ed.final.bin` is
  142 340 bytes, `fileVersion=0x7f080001`, SHA-256
  `1175959e8692d23006d59b8a95e39695aa7ec0ddcd3949403353dd63aec4254b`,
  SHA-512 `b10f704c…6a5be31e`.

### Hosted CI and reproducibility for the round-2 fixes (head `403df5a`, superseded)

- All three workflows PASS on `403df5a`. The PR run 41314781628 and the
  dispatch rebuild 41314899611 are byte-identical:
  `glsd301p-ed.final.bin` is 142 340 bytes, `fileVersion=0x7f080001`,
  SHA-256 `b4dd0c91d89b47c94a782008e2fad1aadf742de4e8e4c681cde0e4d7b7fc1e83`,
  SHA-512 `ab15e663…d35c1889`.

### Hosted CI and reproducibility for the round-1 fixes (head `8b05a11`, superseded)

- All three workflows PASS, including `GLSD301P_REJOIN=PASS` with the new
  cases, `TARGET_LEAVE_ORIGIN_AND_DORMANT=PASS`,
  `TARGET_OTA_QUERY_REARM_AFTER_FAILURE=PASS`, R25/R26 mutants KILLED,
  `NO_SPARE_FINAL_PREFLIGHT_POLICY=PASS_AUTH_FALSE` and
  `EXACT_IEEE_INERT_RELEASE_PLAN=PASS_AUTH_FALSE_RECOVERY_PLANNED`.
- The PR run 41312768731 and the dispatch rebuild 41312905477 are
  byte-identical: `glsd301p-ed.final.bin` is 142 084 bytes,
  `fileVersion=0x7f080001`, SHA-256
  `a7df748a5f9bd4fdd30a02f5d83f24c526cb98ff08444b0804879aa4bdc80ed5`,
  SHA-512 `9171d2ad…5d12128`.

## Live environment, read only (2026-11-01)

Only read-only checks were made, through the canonical `home-assistant-readonly`
access path. No device or coordinator state was changed.

- Zigbee2MQTT add-on `2.14.2-1` (latest `2.14.3-1`): meets the >= 2.9.0
  needed for URL requests.
- The live record of `0xa4c13850cfcdb3a4` matches the preflight tuple:
  `GL-SD-301P` / `GLEDOPTO`, swBuildId `20651203`, dateCode `20240704`,
  hwVersion 2, appVersion 1, stackVersion 2, interview complete, mains.
  Endpoint 11 has 2 binds; endpoint 242 has Green Power.
- **The stock unit is a Router; the candidate is an End Device.** The network
  has 59 routers and 47 End Devices (most of them battery). A read-only raw
  network map shows eight routers listing the dimmer as a neighbour, and one
  (`TS011F_plug_1_2`) lists it as its child (that plug is its parent).
  The stock firmware does not answer Mgmt_Lqi_req; this failed on two
  separate scans (`failed: ['lqi']`). Checked from the other side: 36 of
  the 47 End Devices are claimed as children by routers that answered. The
  other 11 (E2201 ×3, ZG-204ZL ×2, TH01 ×2, 929002398602, TS0201, ZP01,
  ZG-106Z) belong to one of the 8 non-answering routers, and the dimmer is
  one of them. So **up to 11 End Devices may currently use the dimmer as
  parent**; none is confirmed. When the dimmer becomes an End Device, such
  children lose their parent and rejoin through another router. That is
  standard Zigbee behaviour, but sleepy sensors only notice on their next
  poll, so expect a short gap in their reports.

## Still required before any canary

1. Hosted CI green on the new head, and a fresh independent review of the
   A1–A10 fixes.
2. Accept the router-to-End-Device role change: up to 11 End Devices (listed
   above) may have to re-parent. Their parent cannot be read, because the
   stock firmware does not answer LQI requests. After the canary, check that
   those 11 report again.
3. Install the external converter; verify the served bytes against the
   plan's SHA-512 values immediately before each request.
4. Existing preflight gates (live tuple, shadow probes, explicit risk
   acceptance) and explicit human authorization.
