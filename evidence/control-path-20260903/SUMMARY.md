# Live control-path mapping — 2026-09-03 (order 5525122232, read-only)

Data: `zigbee2mqtt/bridge/devices` + `bridge/groups` + `configuration.yaml`
groups block + recent logs. No bind/group/config mutation. Raw:
`raw/control-path-mapping.json`.

## Resolved devices

```text
0xa4c13843a9d40f85 = LivingRoomMainDimmer   (EC-GL86ZPCS31, Router)
                     wall dimmer panel; EP1-EP3 button/bind endpoints,
                     EP4/EP5/EP6 dimmer output channels
0x00124b002d12b1fd = NOT PRESENT in the current Z2M device list
                     (stale/historical bind target — likely a previous
                     coordinator IEEE; no current role)
0xfdb1122d004b1200 = Coordinator (universal bind target used by Z2M to
                     ingest device commands)
0xa4c13850cfcdb3a4 = LivingRoomCircleLightDimmer (GL-SD-301P, Router)
```

## Bind topology (live)

- Wall panel EP6 (Circle channel): single bind `genOnOff -> Coordinator EP1`.
  **No bind to the GL-SD-301P.**
- Wall panel EP1/EP2/EP3: OnOff/Level/Multistate -> Coordinator, plus internal
  binds to the panel's own outputs (EP1->EP4, EP2->EP5).
- GL-SD-301P EP11: binds only `genOnOff` + `genLevelCtrl` -> Coordinator EP1
  (ingest/reporting pattern). **No direct bind from any wall endpoint.**
- Group 110 "LR Circle" members: wall panel **EP6** + GL-SD-301P **EP11**.
- The panel is also a member of groups 8 ("Lights All"), 23, 24, 30 on
  EP4/EP5/EP6 — group-scene usage across the installation.

## Return block

```text
DEVICE_0xa4c13843a9d40f85 = LivingRoomMainDimmer (EC-GL86ZPCS31 wall dimmer
  panel, Router; EP1-3 buttons/bind endpoints, EP4-6 dimmer outputs;
  EP6 = Circle channel)
DEVICE_0x00124b002d12b1fd = ABSENT from current device list (stale/historical
  bind target, likely a previous coordinator; not part of the live path)
DEVICE_0xfdb1122d004b1200 = Coordinator (Z2M command-ingestion bind target)

WALL_CONTROL_ENDPOINTS = LivingRoomMainDimmer EP1-EP3 (buttons) / EP6 (the
  Circle-group output channel)
WALL_CONTROL_BINDS = EP6: genOnOff -> Coordinator EP1 only; EP1-3: OnOff/
  Level/Multistate -> Coordinator + internal binds to own EP4/EP5.
  NO wall->GL-SD-301P bind exists.
WALL_CONTROL_GROUPS = Group 110 "LR Circle" (panel EP6 + GL-SD-301P EP11);
  panel additionally in groups 8/23/24/30 (EP4/5/6)

LIVE_CONTROL_PATH = DIRECT_GROUP_BIND — Circle control reaches the dimmer as
  group-addressed OnOff/Level to group 110. Wall button presses are ingested
  via the panel's Coordinator bind (HA/MQTT relay probable for the final
  command; Z2M-side logs at info level do not record group hops), so the full
  user path is best described as HYBRID (group RX at the dimmer is mandatory
  in every variant).
GROUP_110_ROLE = paired output group "LR Circle": one command drives BOTH the
  wall panel's own dimmer channel (EP6) AND the GL-SD-301P (EP11). It is an
  output/scene group, not a wall-control signalling group.
DIRECT_BIND_PRESERVABLE = yes (trivially — no wall->dimmer direct bind exists;
  what must be preserved is group-110 membership and the two Coordinator binds)
GROUP_RX_REQUIRED_FOR_CUSTOM_ED = yes — the dimmer is commanded via
  group-addressed OnOff/Level; the custom EndDevice MUST implement inbound
  group-addressed command reception (matches supervisor design correction
  5525791743: route group frames through the same handlers as unicast)
EVIDENCE = raw/control-path-mapping.json (bridge/devices + bridge/groups +
  configuration.yaml + recent logs, 2026-09-03T18:35:33+02:00)
```

## Limitation

Passive per-press proof was not obtainable at Z2M info log level (group
command hops are debug-level; log lines for the dimmer contained only
announce/state republications). The bind/group topology above is
authoritative from the live coordinator state; enabling debug logging for one
press would require a temporary config mutation and can be done on request.

## Firmware-design consequence

The custom TLSR8258 EndDevice build must support: unicast OnOff/Level,
group-addressed OnOff/Level (group 110), Groups-cluster membership, and the
two Coordinator binds — with routing disabled and RX-on-when-idle.