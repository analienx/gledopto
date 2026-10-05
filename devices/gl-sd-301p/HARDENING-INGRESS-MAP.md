# GL-SD-301P — enabled ZCL ingress / ownership map

Scope: remediation of independent review R1–R8, reviewed candidate
`7184eec40d8141ccd92a005d2d6d5ef0c2bcb853`. SDK: public Telink
`telink_zigbee_sdk` V3.7.2.0 @ `d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e`.
SDK line numbers below are pre-patch at that commit. Endpoint 11 (0x0B).

G4 must turn every UNGUARDED row below into a guarded + harnessed one, or
record an explicit audit limit. No opaque-archive internals are claimed.

## Registration and dispatch roots (public C, compiled in)

- `zcl_rx_handler` → scheduled `zcl_cmdHandler` (`zigbee/zcl/zcl.c:740-872`):
  parses the ZCL header (manuf-specific needs asduLen>=5, else >=3), then
  foundation commands go to `zcl_foundationCmdHandler`, cluster commands to
  the registered `cmdHandlerFunc`. Short root headers are guarded; R7/R8
  live behind this root.
- App clusters (`glsd301p_telink_target.c:155-171`, in-clusters Basic,
  Identify, Groups, OnOff, Level): registered with
  `zcl_basic/identify/group/onOff/level_register`.
- OTA client (`ota_init`, `zigbee/ota/ota.c`, called at target `:540`):
  `zcl_register(simpleDesc->endpoint, ...)` puts `g_otaClusterList`
  (`zigbee/ota/otaEpCfg.c:83-85`) on endpoint 11 too. OTA ingress below is
  therefore reachable on EP11 even though OTA is an out-cluster in the
  simple descriptor.
- App callbacks: OnOff/Level → shared policy (`glsd301p_zcl_commands.c`,
  validated pre-dispatch at F8/F9); Identify → `glsd_identify_cb` (returns
  SUCCESS, ignores payload — follow-up question); Basic/Groups → NULL.

## Cluster ingress (client→server direction as wired)

| Cluster | Handler body | Payload reads | State/storage effects |
|---|---|---|---|
| Basic | none (no cluster cmds) | — | attrs only via foundation |
| Identify | `zcl_identify.c:134-168` | R6 UNGUARDED: Identify[0:2] `:144`, TriggerEffect bytes `:151-152` | app cb only (no-op SUCCESS) |
| Groups | `zcl_group.c:475-504` | R6 UNGUARDED: Add/View/Remove/AddIfIdentify id; GetMembership count+list | APS group-table add/delete/search (`aps_me_group_*`, archive); responses via `zcl_sendCmd` |
| OnOff | `zcl_onoff.c` (P2b patched) | guarded (F6); effect ids validated in app policy (F9) | shared control plane; OnTime/OffWaitTime mirrors |
| Level | `zcl_level.c` (P2a patched) | guarded (F6); options byte + Stop len-1 partially present (follow-up) | shared transition engine; CurrentLevel/RemainingTime mirrors |
| OTA client | `zcl_ota.c` serverCmdHandler (Notify, QueryNextImageRsp, ImageBlockRsp, UpgradeEndRsp, DeviceSpecificFileRsp) | G4 inventory pending: exact per-command bounds to be traced | image state, flash staging (`ota.c`), reboot on complete (`glsd_ota_event`) |

Opposite (server→client) direction frames reach the `serverCmdHandler`
side of each cluster parser (direction bit is wire-controlled):
Identify QueryRsp `:180` UNGUARDED (app cb non-NULL, so the read runs);
Groups responses guarded in practice by NULL app cb (reads sit inside
`if (pInMsg->clusterAppCb)`); OnOff/Level response sides are G4 inventory
items; OTA client→server requests parse on receipt (G4 item).

## Foundation ingress (`zcl_foundationCmdHandler`, all enabled clusters)

Enabled commands (from the dispatcher `case` list): Read, ReadRsp, Write,
WriteUndivided, WriteRsp, WriteNoRsp, ConfigReport, ConfigReportRsp,
ReadReportCfg, ReadReportCfgRsp, Report, DefaultRsp, DiscoverAttr(+Rsp),
DiscoverCmdsRcvd(+Rsp), DiscoverCmdsGen(+Rsp), DiscoverAttrExtd(+Rsp).

- Read (`zcl_readHandler`, ~`:1074-1137`): parse allocates
  `sizeof+dataLen`, numAttr=dataLen/2, reads bounded. Stores `attrCmd`
  (R8 owner). Honors ACCESS_CONTROL_READ; rejects without APS security.
- Write/WriteUndivided/WriteNoRsp (`zcl_parseInWriteCmd`, `:1266-1312`):
  R7 UNGUARDED — record loop reads type/value without a 3-byte header or
  remaining-length contract; `memcpy` of `zcl_getAttrSize` bytes. Write
  applies via `zcl_attrWrite` per record (validated against attr table
  access/type; read-only attrs rejected — but only AFTER the OOB parse).
- ConfigReport (`zcl_parseInCfgReportCmd`, `:1709-1782`): R7 UNGUARDED —
  unvalidated record walk; **u8 len** allocation-size truncation (`:1742`);
  discrete-record scan drift (`:1735`); applies via `zcl_configureReporting`
  into the report table (`zcl_reporting.c`, reportCfgInfo entries).
- Responses (ReadRsp/WriteRsp/ConfigReportRsp/ReadReportCfgRsp/
  DiscoverRsp/DefaultRsp/Report): stored/forwarded to `attrCmd`/hook; G4
  must bound each parser (truncated prefixes, max counts) and prove no
  attribute/report/transition mutation on invalid input.
- Root dispatch (`zcl.c:835-837`) normalizes most foundation failures to
  FAILURE, erasing specific statuses (R7 fix item).

## Datatype helpers (public C)

- `zcl_getDataTypeLen(u8)` — fixed widths; unknown/strings need caller care.
- `zcl_getAttrSize(u8 dataType, u8 *pData)` — reads length octets for
  variable types from `pData` WITHOUT a remaining-length parameter; every
  caller must bound first (R7 fix item).
- `zcl_analogDataType` / `zcl_analogDataBuild` — analog classification and
  reportable-change copy (used by the configure-report parser).

## Allocation and ownership

- Event pool: `ev_buf_allocate` / `ev_buf_free` (`proj/os/ev_buffer.c`,
  public C). Parsed-command buffers (`attrCmd`) are owned by the root
  dispatcher; R8: with `hookFn==NULL` (`zcl_init(NULL)` at target `:531`)
  they leak per foundation request. Response-building buffers are freed
  inline by each handler (G4 must prove per-path, incl. allocation
  failure and pool exhaustion).
- Group table: APS layer (`aps_me_group_*`, archive) — mutated by Groups
  commands; membership responses built on-stack (`groupListBuf`, capped by
  `APS_GROUP_TABLE_SIZE`).
- Report table: `zcl_reporting.c` entries mutated by ConfigReport; G4 must
  prove invalid input cannot change reporting state.
- NV/report persistence: `zcl_nv.c` compiled in; health path provably avoids
  `nv_*`/`reportAttr` (build gate); G4 must state which ingress paths can
  reach NV writes (report-table persistence) and bound them.

## Audit limits (explicit, carried into G4)

1. Opaque archives (`libzb_ed.a`, MAC/APS internals, `aps_me_group_*`
   bodies, reassembly/fragmentation limits, APS security processing): NOT
   audited; G4 may only bound what public C hands them and state the
   handoff contract.
2. 64-bit host vs 32-bit TC32 widths: G4 needs target-ABI probes (struct
   sizes, `u8 len` wrap thresholds) alongside host sanitizer runs.
3. Physical OTA transfer, flash staging behavior and reboot path: no
   device execution in this task; quarantine holds.
4. `zcl_ota.c` per-command bounds and OTA attribute writes: traced in G4,
   not pre-judged here.

---

## M0 reconciliation for R9–R16 (2026-10-04; rows above are history @ 7184eec)

Current candidate: `bc7196f`. Disposition of every UNGUARDED/pending
claim above (see `HARDENING-FINDINGS.md` R6/R7/R8 for fix commits):

- Identify short reads (`zcl_identify.c:144,151-152,180`): GUARDED by
  P3 (exact-2/exact-0) + harnessed. R13 opens the *effect* gap
  (IdentifyTime never applied), not the parse.
- Groups id/count reads: GUARDED by P4 + harnessed. R10 opens the
  *grammar* gap (GroupName string form unvalidated; Add If
  Identifying over-strict; membership-length policy vs ledger).
- OnOff/Level: guarded (F6/P2a/P2b) + harnessed. R9/R16 open
  *semantic* gaps (With On/Off direction invention; Step duration
  after clipping), not parse.
- OTA per-command bounds: GUARDED by P6 (all 10 parsers) + harnessed.
  OTA core/flash/reboot stays an opaque/physical limit (no device
  execution); parser-only evidence, inert stubs.
- Write/report/read-rsp/cfg/read-cfg-rsp: GUARDED by P5 validators +
  harnessed. R11 opens residual *grammar* gaps (read-cfg whole
  records, discover-rsp suffixes, write/cfg-rsp status-dependent
  short forms).
- `u8 len` wraps (`zcl.c:1742,1859,1982`): widened to u16 by P5 +
  wrap battery. Root status normalization removed (truthful
  statuses); R8 cleanup hook-independent + allocation accounting.
- `zcl_getAttrSize` unbounded reads: every caller now pre-validated
  (P5 validators replicate its size logic with bounds); direct
  unguarded call sites eliminated on enabled paths.
- NV writes reachable only via report-table persistence
  (`zcl_reportingTab_save` on successful ConfigReport); invalid input
  rejected before table change (state-guarded regression).
- Opposite-direction + response-side parsers: harnessed (identify
  QueryRsp, groups responses via NULL-cb, OnOff/Level response sides
  unreachable-by-dispatch, OTA requests both directions).
- Group table / report table / pool ownership: as mapped above; pool
  steady-state 26/26 + exhaustion fail-closed regressions hold.
- 64-bit host vs 32-bit target: `-m32` pool-exact harness + TC32
  target rebuild; exact per-type TC32 ABI probes remain a bounded
  M3 audit question, not a claimed proof.
- Remaining audit limits carried forward: opaque archives
  (`libzb_ed.a`, MAC/APS internals, fragmentation, APS security),
  physical power-stage/flash/live-network proof, public OTA core
  beyond parsers, residual Level options/rate semantics. Each must
  resolve with evidence or stay a documented limit at M5.

## M3 fix reconciliation (R10-R13 closed @ 7c0cf10)

- R10 grammar gap closed by P4v2: Add/AddIf require the exact
  uint16+counted-string form (namelen <= 15, exact 3+namelen, no
  0xFF/trailing); GetMembership requires exact 1+2*count. The
  membership-length policy question above resolves to EXACT, matching
  the R6 ledger claim (previously minimum-only); Add If Identifying is
  no longer over-strict (validates the same serializer shape, then
  applies the identifying guard).
- R11 residual grammar gaps closed by P5v2: read-cfg request
  validator (whole 3-byte records, defined directions, non-empty);
  discover rsp/ext whole 1+3n/1+4n records; write/cfg-rsp 1-byte
  success-only short forms; empty write/report/read-rsp/read-cfg-rsp
  frames malformed. Discover complete-only stays the sole legal
  record-less response.
- R12 status leak closed by P5v2: the read-cfg response builder
  initializes status per record.
- R13 effect gap closed by the `glsd301p_identify` adapter + control
  effect overlay (see findings): shared-store countdown (command +
  write paths), honest Query, Blink/Breathe programs with restore,
  reserved ids/variants rejected. Plain Stop during an OFF-phase is
  correctly gated by execute-if-off (SDK Options semantics); the
  emit-less StopWithOnOff path restores explicitly.
- M3 audit answers: countdown/effects add no timers (household 1 ms +
  owned Level timer); trigger-effect validation precedes state
  change; no new SDK surface beyond validated commands. Opaque
  archives, physical proof, and compound-type nesting beyond the
  harnessed flat struct/string stay documented limits (see M5).

## R17–R23 atoms (M2–M4 @ 0102715/9688233; seal commit for runs)

- R17 replaces the R13 effect ingress: Trigger Effect (any id/variant)
  is rejected with INVALID_FIELD by the shared
  `glsd301p_identify_cluster_command()` before any state change; no
  effect program, restore, or preemption exists. The old
  Blink/Breathe/restore ingress description above is superseded
  history. No physical identification exists (single load, no
  separate identify output); commissioners observe IdentifyTime.
- R18 adds the accepted-write ingress: the P5v3 SDK call sites report
  every successful `zcl_attrWrite` to `glsd301p_sdk_write_observer()`,
  which restarts IdentifyTime writes (incl. same-value) at receipt
  time; rejected/wrong-type/wrong-endpoint writes never notify.
- R19 keeps the countdown ingress O(1): whole-second arithmetic with
  residual phase and saturation; no catch-up work when disabled.
- R20 keeps the Move to Level ingress duration: accepted/clamped
  finite tenths run whole; proportional timing is Step-dispatch-only.
- R21 adds TARGET admission onset: accepted upward With On/Off applies
  ON before dispatch returns (guarded path, like MOVE).
- R22 hardens the typed ingress (P5v4): full-width STRUCT counts
  (high byte rejected), flat scalar elements only, nested/unknown/
  reserved compounds rejected, No Data as the sole zero-length type,
  reserved reporting directions rejected incl. per-record cfg-rsp
  long forms. Validator and SDK parser stay synchronized on every
  accepted shape.
- R23/A23 adds the target ABI ingress proof: TC32-compiled foundation
  record sizes/offsets in both TU contexts, pool geometry, and the
  255-cap u16/fail-closed allocation thresholds, plus a gate keeping
  parsed commands opaque to app code; the hosted layout test pins the
  -m32 deltas with explicit guards.
- A24 identity: GLSD-ED-004 / APP_BUILD 05 / FILE_VERSION 0x7F050001 /
  DATE 20261005 (fresh, verified unused).

## R24–R26 atoms (M2 @ cb9726a; seal commit for runs)

- R24 completes the status-dependent response ingress (P5v5):
  long Write/CfgRsp records must be failures (any SUCCESS record
  is MALFORMED); ReadCfgRsp directions are validated before the
  status branch, so failure records with reserved directions are
  rejected too. No new ingress: the same three response commands,
  narrower acceptance.
- R24 changes one egress shape: a refused Undivided write answers
  failures only (all-success stays the one-byte response). No new
  surface; accepted-write observation and refused-write no-restart
  preserved.
- R25 removes the dead `tick_steps_max` self-report; the O(1)
  countdown ingress is unchanged and its work bound is proven
  externally by the gcov oracle (no new code path, no timer).
- R26 adds no ingress: same four flows (Write/Report/Configure/
  ReadCfg) executed at exact allocation edges in unpacked and
  packed binaries, with recorded-request asserts. The target ABI
  header is compiler-verified metadata, not a runtime surface.
- Q1 defers with evidence: admission ordering (ON before timer
  registration, discarded emission result) is unreachable-as-failure
  per the cancel-first + fail-only-on-NULL + ready-excludes-fault
  chain; no ingress or behavior change.
- G27 identity: GLSD-ED-005 / APP_BUILD 06 / FILE_VERSION 0x7F060001 /
  DATE 20261005 (fresh, verified unused across repo, history,
  issues, PRs).
