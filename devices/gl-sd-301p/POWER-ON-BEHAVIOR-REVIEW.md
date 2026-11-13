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

**Startup firmware implementation is pending.** This document is an explicit
reviewable requirement, not a claim that ED-008 already supports the setting or
that a replacement image is ready.

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
