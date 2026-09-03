# Power-on evidence — 2026-09-03 (mains restored)

Read-only collection after the GL-SD-301P was powered back on. No config
changes, no extension, no OTA traffic — only retained MQTT state, the
documented `get` read mechanism, and passive log analysis.

## Facts observed

- **Identity fully retained after power-up**: GL-SD-301P, swBuildID 20651203,
  dateCode 20240704, GLEDOPTO, type Router, `networkAddress 52572` unchanged
  → no leave/rejoin occurred; the device kept its network session.
- **Availability**: `online` (both friendly-name and IEEE topics); bridge online.
- **Live reads respond** (09:30 and 09:32 local): `state OFF, brightness 0,
  power_on_behavior "on", LQI 120–144`. Consistent with the Phase 1 raw reads
  (`startUpOnOff=1`, `startUpCurrentLevel=255`, `power_on_behavior: on`) and
  with Z2M `state_action: true` re-applying the cached OFF state after the
  power-up announce — the observed OFF is therefore likely Z2M-imposed, not
  necessarily the device's native power-up output state.
- **Timeline** (all of today's Z2M logs since 2026-09-02 08:12): zero
  `offline`/leave events for this device; exactly one `device_announce` at
  08:14:18 (typical router power-up announce); all other `online` lines are
  Z2M restart republications.
- **Repeatable quirk**: each `get`-triggered read was followed ~immediately by
  an unsolicited `{"action":"off"}` event published by Z2M for this device
  (09:30:38 and 09:32:56). Raw log lines preserved in `raw/poweron-snapshot.json`
  (`recent_target_lines`). Worth re-examining during spare bring-up; do not
  over-interpret — captured verbatim only.

## Files

- `raw/poweron-evidence.json` — availability, descriptor, state, fresh reads.
- `raw/poweron-snapshot.json` — 09:32 snapshot incl. raw recent log lines.
- `raw/poweron-timeline.log` — full availability/announce/offline timeline.
