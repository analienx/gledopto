# ED-008 bench diagnostics (work in progress)

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
credentials without printing them. For the current target use IEEE
`0xa4c13850cfcdb3a4`, build `GLSD-ED-008`, endpoint `11`, role `EndDevice`,
and decimal file version `2131296257`.

```powershell
python tools/glsd301p_bench.py --profile C:\private\bench-profile.json --workdir C:\private\bench-results --phase guided
```

`guided` runs the ordered phases and prompts for each manual cycle. Resume with
the same command and workdir: completed cycles are not repeated. Enter `DONE`
only after physically completing the requested cycle; an empty response pauses.
Add `--skip-soak` when a 20-minute soak is not wanted; it remains unverified.
Individual phases are `baseline`, `attributes`, `ota`, `power-cycle`, `soak`
and `report`.
Use only one active runner for the target. Current versions use a host-local
lock, but a runner launched from an older version may not hold that lock.
Use the same workdir to append observations across invocations; `report` reads
the existing journal. `ota` runs up to five transaction-matched availability
checks with a 90 second window, and never requests a firmware transfer. Its
responses do not prove OTA download or boot. `soak` waits five minutes between
its four probes. Ctrl+C preserves completed events.

Generic successful read logs omit the target and transaction identity. The
runner requests a unique `state_property` for each read, and accepts only that
property in a non-retained message on the target's exact topic. Zigbee2MQTT may
retain these diagnostic properties in its device-state cache; they are not
configuration changes. OTA checks use their own transaction IDs.

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
diagnostics. The current local changes have not passed hosted CI.
