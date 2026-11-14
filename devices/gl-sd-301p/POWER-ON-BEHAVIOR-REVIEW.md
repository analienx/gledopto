# Configurable startup behavior: review gate

Tracking issue: <https://github.com/analienx/gledopto/issues/11>.

**No flashing or OTA initiation is authorized.** The owner requires another
agent's review followed by explicit owner green light. CI success, a published
branch, or a review comment alone does not authorize a device transfer.

## Installed behavior and requested change

The owner reports ED-008 running, paired, and controllable after wired recovery.
Current source is based on `228f3ee`, with build `GLSD-ED-008` and file version
`0x7F090001`. This observation does not close the broader incident in issue #9.

ED-008 deliberately boots OFF at level 254. StartUpOnOff and
StartUpCurrentLevel are read-only; converter `powerOnBehavior` is disabled.
This prevents the lamp from lighting after its upstream RIGHT relay restores
power. The End Device role does not require this limitation.

Implement standard writable StartUpOnOff values: 0 OFF, 1 ON, 2 toggle previous,
255 previous. Expose the matching `power_on_behavior` control in Zigbee2MQTT.
Persist the selected policy and previous accepted application state. Preserve
brightness on previous-state restoration, with separately defined and truthful
StartUpCurrentLevel behavior. ON is the intended setting for this installation;
do not silently change an existing user's chosen policy during migration.

## Required implementation and review checks

- Preserve the first confirmed electrical OFF frame, UART completion, essential
  timer registration, output guard and fault latch before any requested ON.
- Define default/migration behavior for ED-008 with no application record;
  reject invalid enum values before an attribute write is acknowledged.
- Persist using an application-owned, versioned, integrity-checked NV item.
  Validate state and level bounds. Missing/corrupt/torn records fail safely.
  Never change the stack-NV epoch marker or trigger network erasure/re-pairing.
- Define when a state becomes durable, immediate-power-loss limitations,
  write failure behavior, flash wear/coalescing and UART/timer constraints.
  Avoid a periodic write storm or flash activity during active output DMA.
- Include Zigbee, physical PUSH, level-move, min-level and OFF paths in the
  saved-state design. Fault-forced OFF must not be persisted as an ordinary
  user choice unless the semantics explicitly call for that.
- Prove all startup policies, previous ON/OFF and brightness, malformed writes,
  missing/corrupt records, interrupted storage, and faulted/unready output.
- Bump Basic build, OTA version and date together for the reviewed candidate.
  Preserve role, manufacturer/image type, endpoint 11 and recovery reachability.
- Run host regressions, converter checks and real TC32 build on GitHub-hosted
  Actions from the exact candidate SHA. No local public-repository tests/builds.
- Review exact candidate and CI before proposing a bounded OTA. After explicit
  owner approval: transfer, fresh installed build/role, power-cycle behavior,
  physical dimming and button/relay operation are separate live acceptance cases.

## Implemented candidate: ED-009

The candidate is `GLSD-ED-009`, app version `0x0A`, OTA `0x7F0A0001`, date
`20261113`. ED-008 remains installed. No OTA has been initiated.

- `src/glsd301p_startup.{c,h}` owns a versioned 8-byte record with CRC16,
  startup selection, last normal OnOff and brightness. Invalid/missing records
  migrate to OFF/254. Values 0/1/2/255 mean Off/On/Toggle/Previous.
- Standard OnOff `StartUpOnOff` (0x4003) is writable. The pinned SDK patch
  rejects reserved enum values before ordinary writes and atomic validation.
  Level `StartUpCurrentLevel` remains read-only 255, restoring saved brightness.
- Target item `NV_MODULE_ZCL:0xE6` (0x3A000–0x3BFFF) uses SDK journal write plus
  exact readback. Routine saves never rotate the APP module holding epoch
  item `0xE5`. Stack epoch and its marker remain unchanged. An ordinary OTA
  within this firmware line does not erase network records or startup settings.
- Persistence requires ready output, idle level engine, no pending/inflight
  DMA and two matching maintenance samples separated by 1 s. Normal durability
  takes 1–2 s after settling. A cut sooner restores the prior committed state.
  State writes are coalesced, absent when unchanged, and failed writes retry
  no more than once per 30 s. Fault-forced OFF never replaces normal state.
- Control restores selected OnOff only after initial OFF completion and owned
  timer readiness. Defaults remain OFF, so after a future approved installation
  explicitly select `on` for the RIGHT-relay topology, or `previous` if desired.
- The converter exposes the four options for the ED-009-and-later build family;
  ED-008/unknown development builds keep their older light/OTA definition.
  Do not mistake green converter/build checks for actual installed support.

Regression sources: `tests/test_glsd301p_startup.c` (all policy/state pairs,
round trip, every single record-bit corruption, power cut, failed write retry,
wear/coalescing, unsafe state, zero-level OFF and wrap); control harness
(configured ON stays OFF until completion, invalid restore, boot timeout);
SDK dispatch harness (valid/reserved policies, atomic writes and no-response
writes); converter resolution/exposes; packaging startup contract.

Limits to review: durability is deferred rather than synchronously guaranteed
by the ZCL write response. A persistent flash failure retains the previous
committed record and retries. Startup policy applies to watchdog/software
resets too. SDK journal behavior and flash timing are not replaced by a new
storage driver; actual powered-device persistence and lamp behavior remain
unverified until a separately authorized OTA and controlled power-cycle test.

## Review corrections and evidence limits

The initial candidate at `0fdf8aa` incorrectly placed routine saves in APP with
the epoch marker and left application voltage detection disabled. That
candidate is withdrawn from hardware consideration. Keeping the marker's bytes
unchanged was insufficient: sector rotation could lose it and boot recovery
would then erase the network. No part of that candidate was flashed here.

The corrected target checks a fresh PB3 ADC sample before calling NV and
refuses the save below 2200 mV. It reduces brownout exposure; it cannot
guarantee a flash operation finishes if supply collapses after the check.

An intermediate revision (`e35b4ff`..`7b0ef11`) also set
`VOLTAGE_DETECT_ENABLE=1`. In the pinned SDK that adds a boot-time
`voltage_detect(1)` in `platform_init`, before `user_init`: unless five PB3
samples exceed 2.2 V within one second it calls `SYSTEM_RESET()`, on every
boot. No custom build had ever read PB3 on this unit, and over the air a unit
stuck in that loop cannot be recovered (Zigbee never starts). The flag is back
to 0, so `platform_init` is the same as ED-008's, which runs on this unit. The
target configures the PB3 VBAT reading itself in `user_init` with the SDK's own
calls (`drv_adc_init`, `drv_adc_mode_pin_set(DRV_ADC_VBAT_MODE, PB3)`,
`drv_adc_enable`). It never waits on or resets for the reading. A wrong reading
can only refuse startup saves, which the bench `startup` cases detect and a
later OTA can fix. The build fails if `voltage_detect` is linked. Stack, OTA and
SDK NV writes keep ED-008's behaviour; on Zbit flash parts the SDK's own chip
level PB3 check still applies to them.

Basic:0xFF11 is a read-only, RAM-only, 10-byte supply snapshot: version 1,
sample count, first reading after boot, latest reading, lowest reading
(all mV, little-endian) and the count of saves refused for low voltage. The
boot sample gives a real PB3 value from the first ED-009 boot. Isolation ensures damaged ZCL
storage cannot, by itself, invalidate the APP epoch marker. ZCL reporting setup
may need repair after a ZCL storage failure. The unsafe APP:E6 record is not
read or migrated; the withdrawn candidate was never deployed.

Executable hosted wiring tests compile the actual target supply sampler,
writer, maintenance and `user_init` bodies. They exercise low voltage, a boot
with a hopeless 1000 mV reading (init must still reach `bdb_init`), the PB3
VBAT configuration, the 0xFF11 encoding and saturation, every save-deferral
input, NV errors, corrupt and valid-but-different readback, and boot-load
ordering. Mutation cases must fail if the module, voltage check, comparison,
DMA gate, boot ordering, boot sample or ADC setup changes, or if boot waits on
the reading.
The build manifest receives observed module/item/address/guard facts from this
test report, along with its SHA-256. These are tested source-wiring facts, not
binary-derived proof or hardware acceptance.

A second harness executes the pinned SDK NV driver and CRC code on simulated
NOR flash. It forces repeated sector rotations, cuts in write/erase callbacks
and invalid ZCL sector headers. All bytes outside ZCL, and the actual epoch
clean predicate, must remain unchanged. The simulation is not a physical
brownout test.

The profile-driven bench runner supports ED-008, ED-009 and future explicitly
pinned builds/versions. `post-ota` requires fresh identity/version responses and
updated Z2M metadata with the startup expose. `startup` supplies read-only
before/after cases with a three-second settling gate and real restarted-uptime
check; see BENCH-ED008.md. No interview, firmware transfer or relay action is
performed automatically.

Household policy: select `on` when RIGHT is the normal hard-power control. If an
automation uses `previous` and sends a light/brightness command before cutting
RIGHT, require at least three seconds with no newer state/level change before
the cut. A direct bound or local wall-button cut cannot be delayed by HA; do not
promise last-second persistence for that path. The HA topology/agent handoff
records this rule; live automation rollout remains a separate qualified action.

## Recent-activity defect and correction

32 `bench_read_<uuid>` fields from the private diagnostic runner accumulated in
Zigbee2MQTT state. Normal MQTT messages repeated about 7.6 KB, including old
health snapshots, even when their values were unchanged. The firmware never
emits these property names; its Basic 0xFF10 snapshot is read-only and not
reported. Reflashing would not remove Zigbee2MQTT's cached fields.

Target-only `filtered_cache: ["bench_read_.*"]` keeps each fresh diagnostic
response observable once while excluding it from subsequent cached state.
The full-name regex semantics matter: `^bench_read_` alone does not match the
UUID suffix. Keep the pattern out of `filtered_attributes`, which would hide
the response needed for correlation. Existing cache was refreshed through
ordinary reads. Fresh outgoing messages were checked with zero bench fields.
Historical Recent activity entries can remain in the frontend's history.

The runner now refuses reads unless live bridge configuration confirms the
cache exclusion and visible diagnostic responses. Its regression suite covers
the prefix-only mistake and cache-filter absence. Hosted CI remains the
authority for this public repository.

## Button and image observations

Live repair tooling belongs in the private `analienx/home-assistant-stack`
repository. The wall RIGHT short press already operates its local RIGHT relay;
the SONOFF EP3 OnOff binding to that relay was retained. Wall EP3 now binds
only Level Control to Gledopto EP11. The owner confirmed dimming changed and
stopped on release. Two stale Sunricher bindings were removed using classic
per-cluster Unbind with confirmation, then the host cache was reconciled and
the transient repair extension removed. The newer Clear All Bindings command
was unsupported and is not retried. The official original GL-SD-301P image URL
is saved as the target's icon option; visual frontend confirmation is separate.

Remaining HA work includes removing the legacy SONOFF light-toggle branch
that can duplicate its direct relay toggle, and reviewing old Sunricher
reconciliation/voice behavior against the restored relay-powered topology.
