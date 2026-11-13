# Profile-driven ED bench diagnostics (ED-008 and later)

`tools/glsd301p_bench.py` records targeted MQTT diagnostics in a private work
directory. It does not flash, reset, change joining or open SWire. It requires
Python 3, `paho-mqtt` 2.x, PyYAML, and access to the existing MQTT broker.
The local profile and MQTT config must be outside this repository. Do not
commit them or the generated logs.

Before any read, configure the target device's `filtered_cache` option to
include `bench_read_.*`. Zigbee2MQTT wraps these patterns in full-name anchors;
`^bench_read_` alone does not match the UUID suffix. Do not put this pattern in
`filtered_attributes`: fresh responses must remain visible to the runner.
The runner refuses to read unless the live bridge configuration confirms this
setting. This prevents old diagnostics from being repeated in normal state.
Existing cached diagnostics disappear after the next state updates; historical
frontend activity entries are not retroactively removed.

The profile is JSON with keys `ieee`, `build`, `endpoint`, `role`,
`file_version` and `mqtt_config`. The latter points to a private Zigbee2MQTT
YAML configuration containing its `mqtt` section; the runner reads the broker
credentials without printing them. Pin the actual approved target image in
each profile: installed ED-008 uses build `GLSD-ED-008`/version `0x7F090001`;
the ED-009 candidate uses `GLSD-ED-009`/`0x7F0A0001`. JSON file_version is an
integer (convert the hex value); endpoint is 11 and role is EndDevice.
The target IEEE remains pinned by the tool. Future builds require an explicit
profile build/version pair; live reads must match it. A workdir is bound to
that profile, so use a new directory for a new image. Preserve old journals;
do not relabel ED-008 evidence as ED-009 evidence.

```powershell
python tools/glsd301p_bench.py --profile C:\private\bench-profile.json --workdir C:\private\bench-results --phase guided
```

`guided` runs the ordered phases and prompts for each manual cycle. Resume with
the same command and workdir: completed cycles are not repeated. Enter `DONE`
only after physically completing the requested cycle; an empty response pauses.
Add `--skip-soak` when a 20-minute soak is not wanted; it remains unverified.
Individual phases are `baseline`, `attributes`, `ota`, `power-cycle`, `soak`
and `report`, plus the `post-ota` and `startup` gates below.
Use only one active runner for the target. Current versions use a host-local
lock, but a runner launched from an older version may not hold that lock.
Use the same workdir to append observations across invocations; `report` reads
the existing journal. `ota` runs up to five transaction-matched availability
checks with a 90 second window, and never requests a firmware transfer. Its
responses do not prove OTA download or boot. `soak` waits five minutes between
its four probes. Ctrl+C preserves completed events.

Generic successful read logs omit the target and transaction identity. The
runner requests a unique `state_property` for each read, and accepts only that
property in a non-retained message on the exact friendly-name topic resolved
from the pinned IEEE in bridge/devices. The cache exclusion above prevents
these diagnostics being retained in device state. OTA checks use transaction IDs.

Before each manually controlled power cycle run `--phase power-cycle --cycle N
--stage before`; proceed only on PASS. Disconnect SWS at the adapter, turn off
the isolated target supply until the rail is discharged, wait ten seconds and
restore the supply. Then run the same command with `--stage after`. The runner
checks joining is closed, a restarted and advancing uptime, retained network
membership, fresh build and OTA availability. Stop if the stage fails. A later
cycle requires a passing previous cycle. Use `--phase report` to resume by
reviewing the journal; never repeat a power cycle merely because the console
was interrupted.

The installed Zigbee2MQTT spelling is `upgradeServerId` (lowercase `d`), not
`upgradeServerID`. The attributes phase compares both spellings and numeric
attribute zero. Keep a failed old-spelling request separate from a firmware
defect.

Repository tests, builds and acceptance must run on GitHub-hosted Actions at
the exact candidate SHA. Local bench access is only for authorized live-device
diagnostics. Exact-SHA CI and review evidence are recorded in issue #11;
never reuse an older commit's green result for a new candidate.

## Post-OTA metadata gate (only after separate owner authorization)

```powershell
python tools/glsd301p_bench.py --profile C:\private\ed009.json --workdir C:\private\ed009-results --phase post-ota
```

Requires fresh Basic build and OTA file-version reads, matching bridge metadata,
EndDevice/endpoint 11 and the four-choice power_on_behavior expose. A stale
ED-008 build or legacy definition fails. The tool does not trigger another
OTA or re-interview: use the qualified targeted metadata refresh/interview
procedure after the approved OTA, then rerun this check. Exit 1 means that the
requested phase failed or remains unverified.

## Startup persistence acceptance

Use a separately approved safe bench/power-cycle setup. Select the policy and
initial state manually first. Cases: `on` (policy on, initially OFF), `off`
(policy off, initially ON), `toggle` (toggle, initially ON), `previous-on`, and
`previous-off`. Select a nonzero brightness first, even for an OFF case.

```powershell
python tools/glsd301p_bench.py --profile C:\private\ed009.json --workdir C:\private\ed009-results --phase startup --startup-case previous-on --stage before
```

This reads fresh identity/state/health, waits three seconds, checks the state
is still unchanged, and records the settled snapshot. Proceed with the manual
power cycle only after PASS; keep joining closed and do not reset/re-pair.
Then run the same command with `--stage after`. It requires restarted uptime,
joined healthy communication, retained policy, expected OnOff and brightness.
Repeat for each case with its proper operator-selected initial state. All of
these cases remain UNVERIFIED in the ED-009 report until actually performed.
These read-only commands do not select settings, cut a relay or change firmware.

For household automation using `previous`, allow at least three quiet seconds
between the latest light/level change and cutting RIGHT. Direct wall/bound relay
control cannot enforce that delay through HA; `on` is the intended policy when
RIGHT is the normal lamp power switch. Do not claim last-second persistence.
