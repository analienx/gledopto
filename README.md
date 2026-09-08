# analienx/gledopto — GL-SD-301P interoperability firmware

Independent firmware work for the GLEDOPTO **GL-SD-301P** Zigbee AC dimmer.
The objective is to keep the device mains-powered and always listening while
changing its Zigbee role from Router to an RX-on-when-idle **End Device/leaf**.

Target architecture:

```text
ZB_ED_ROLE=1
ZB_ROUTER_ROLE=0
RX_ON_WHEN_IDLE=1
PM_ENABLE=0
powerSource=MAINS
```

The project preserves hardware interoperability (On/Off, dimming, physical
control, reporting, direct binding and reliable electrical OFF) through an
independently written implementation.

## Interoperability/reference boundary

Read `CLEAN_ROOM.md` and `THIRD_PARTY_FIRMWARE.md` before contributing.

The repository deliberately has three separated zones:

```text
third-party vendor reference / RE evidence
                 |
                 v
       interoperability specification
                 |
                 v
       independently written firmware
```

Lawfully obtained vendor firmware may be retained under
`devices/*/vendor-firmware/` with provenance and hashes. Those binaries remain
third-party copyrighted material and are **not relicensed by this repository**.
They are useful reference/backup evidence and may be analysed for the project's
interoperability purpose.

The independent implementation consumes documented interface facts from:

`devices/gl-sd-301p/interoperability/INTERFACE.md`

CI enforces the directory/provenance boundary with `tools/check_cleanroom.py`.

## Current engineering result

The power-stage path is classified **`SECOND_MCU_UART` with high software
confidence**. The TLSR8258 application initializes a 9600-baud 8N1 UART with
TX=PB1 and RX=PA0 and emits six-byte controller messages from the lighting path.

The principal level-control frame is now structurally known:

```text
A5 5A 01 LL 04 AA
```

The standard level-update path sends `LL=0` while output is disabled and otherwise
uses the Zigbee `currentLevel`, subject to the product's minimum-level policy.
A second operation family uses:

```text
A5 5A 02 MM 04 AA
```

Its full mode map is still being recovered. The remaining work is therefore
protocol semantics/startup synchronization rather than discovering the basic
hardware architecture.

## Safety

The installed production unit is not the first canary. Flashing and live
electrical validation require the separate sacrificial-spare/Supervisor gate.
Static analysis, host tests and public-source comparison do not authorize live
mains work.

## Layout

- `CLEAN_ROOM.md` — interoperability/reference/implementation boundary.
- `THIRD_PARTY_FIRMWARE.md` — third-party firmware provenance/licensing policy.
- `AGENTS.md` — mandatory agent bootstrap and hard boundaries.
- `.supervisor/project.yaml` — project manifest.
- `devices/gl-sd-301p/vendor-firmware/` — original third-party references + curated RE evidence.
- `devices/gl-sd-301p/interoperability/` — implementation-facing interface specification.
- `src/` — independently written reusable implementation components.
- `evidence/` — sanitized project evidence not requiring proprietary binary material.
- `tools/check_cleanroom.py` — CI boundary/provenance guard.
