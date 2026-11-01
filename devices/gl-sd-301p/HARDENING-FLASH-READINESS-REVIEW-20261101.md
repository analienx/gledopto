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
  power-on-behavior or effect controls). Load-tested against
  zigbee-herdsman-converters 26.125.0 (`findByDevice` resolves it).
- `tools/glsd301p_release_plan.py`: `--recovery-url`; the plan is never
  decision-ready without a recovery request. The recovery request uses the
  same `{"id","url"}` shape proven by the BSEED targeted runner and is marked
  `isDowngrade` / `requiresZ2mDowngradeVerificationBeforeCanary`: whether the
  installed Zigbee2MQTT serves a lower version for an explicit URL must be
  verified read-only before the canary.

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
  final image 141 540 bytes, `TELINK_FINAL_IMAGE=PASS fileVersion=0x7f070001`;
  new symbols present in the linked ELF; OTA wrapper packaging PASS.
- Not run locally (no 32-bit multilib): the `-m32` ZCL dispatch, mutant and
  original-body legs. They must pass in hosted CI.

## Still required before any canary

1. Hosted CI green on the new head (both workflows).
2. Fresh independent acceptance review of the new head.
3. Read-only verification that the installed Zigbee2MQTT serves the vendor
   downgrade for an explicit URL; install the external converter first.
4. Existing preflight gates (live tuple, shadow probes, explicit risk
   acceptance) and explicit human authorization.
