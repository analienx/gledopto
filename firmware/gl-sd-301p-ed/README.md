# GL-SD-301P RX-on-idle End Device firmware

This directory is the actual product-firmware track for the installed GL-SD-301P.
It is deliberately separate from `firmware/wireless-dump-stager/`, which remains
recovery/research tooling.

## Product objective

Build a normal Zigbee dimmable-light firmware for TLSR8258 with:

```text
Zigbee role          End Device (not Router)
RX while idle        enabled
power management     disabled
power source         mains
endpoint             11 / 0x0B
profile              Home Automation
server clusters      Basic, Groups, On/Off, Level Control
client cluster       OTA Upgrade
routing              disabled by End Device role
children             not accepted by End Device role
```

The pinned Telink SDK directly supports this architecture. In the End Device
build, `ZB_MAC_RX_ON_WHEN_IDLE=1` causes the stack to advertise an RFD with the
RX-on-when-idle MAC capability rather than router capability.

## Architecture

The firmware is intentionally split into three pieces:

1. `glsd_ed_core.*` — device-state semantics: On/Off, level, minimum level,
   previous level and physical-PUSH actions. No Telink dependency.
2. `glsd_telink_ed_app.c` — endpoint 11, standard ZCL attributes/callbacks,
   BDB/network restoration, reporting defaults and OTA client.
3. `glsd_power_stage.h` — the hardware boundary. The exact installed GL-SD
   power-stage interface (direct phase-control GPIO/timer versus a second MCU)
   is still being reverse-engineered. Product logic must not guess it.

That hardware boundary is the remaining engineering problem. It does not block
building and testing the Zigbee product application.

## Fixed product identity during development

Development firmware uses an unambiguous model/build identity instead of
pretending to be untouched stock:

```text
manufacturerName = GLEDOPTO
modelId          = GL-SD-301P-ED
endpoint         = 11
hwVersion        = 2
powerSource      = mains
```

The OTA manufacturer/image identifiers remain the known GL-SD lineage values
when an OTA container is eventually built:

```text
manufacturerCode = 0x124F
imageType        = 0x1416
```

## Current scope

The first implementation supports standard immediate On/Off and Level commands
through a small state core. Transition timing, Scenes persistence and the exact
physical PUSH GPIO are follow-up implementation items once the power-stage
adapter is identified.

No custom network discovery, credential extraction, coordinator modification or
other network-security functionality belongs in this firmware. Zigbee is used
only for the dimmer's normal application protocol, commissioning and OTA path.
