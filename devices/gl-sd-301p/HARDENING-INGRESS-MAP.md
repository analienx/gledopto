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
