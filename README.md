# analienx/gledopto — GL-SD-301P clean-room firmware

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

## Clean-room rule

This repository is the **implementation side** of the interoperability process.
Read `CLEAN_ROOM.md` before contributing.

Vendor firmware, flash dumps, disassembly/decompiler output, reconstructed
source and encoded firmware chunks do **not** belong here. Reverse-engineering
work occurs in a private analysis workspace; only the minimum sanitized
interface facts necessary for interoperability cross into this repository.

The canonical hardware interface contract is:

`devices/gl-sd-301p/interoperability/INTERFACE.md`

CI enforces the boundary with `tools/check_cleanroom.py`.

## Current engineering result

The power-stage path is now classified **`SECOND_MCU_UART` with high software
confidence**. The TLSR8258 application uses a 9600-baud 8N1 UART on TX=PB1,
RX=PA0 and emits six-byte messages from live lighting/transition paths.

The exact six-byte protocol is not fully decoded yet. Unknown/reserved fields,
level mapping, electrical-OFF semantics and startup synchronization remain
fail-closed, so a flashable canary is intentionally blocked until black-box UART
capture on a sacrificial spare resolves them.

## Safety

The installed production unit is not the first canary. Flashing and live
electrical validation require the separate sacrificial-spare/Supervisor gate.

## Layout

- `CLEAN_ROOM.md` — mandatory interoperability/independent-implementation policy.
- `AGENTS.md` — mandatory agent bootstrap and hard boundaries.
- `.supervisor/project.yaml` — project manifest.
- `devices/gl-sd-301p/` — device ledger and status.
- `devices/gl-sd-301p/interoperability/` — sanitized interface specification.
- `evidence/` — sanitized historical/project evidence only; never proprietary firmware.
- `tools/check_cleanroom.py` — CI/repository boundary guard.
