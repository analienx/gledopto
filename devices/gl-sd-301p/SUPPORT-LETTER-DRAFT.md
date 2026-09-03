# DRAFT — GLEDOPTO support inquiry (supervisor order 5522442315 §J)

> Executor note: draft only, to be sent by the project owner.

Subject: GL-SD-301P — exact Zigbee SoC / module identification for a specific firmware tuple

Hello GLEDOPTO support,

I own a GL-SD-301P Zigbee dimmer and need to identify the exact silicon in THIS hardware revision, because I understand (from your own guidance) that internal parts can change without the model name changing.

The device reports:

```text
softwareBuildID          20651203
dateCode                 20240704
hardwareVersion          2
OTA manufacturerCode     0x124F (4687)
OTA imageType            0x1416 (5142)
OTA fileVersion          0x26013001
OTA stack version        2 (Zigbee 3.0)
ZCL cluster revisions    1 (Basic / OnOff / Level / Color)
```

Questions:

1. Which Zigbee SoC or pre-built module is used in THIS revision of GL-SD-301P?
   Please provide the exact IC marking or Telink part number
   (e.g. TLSR8258 / TLSR8278 / other), not a generic "GL-SD-301P" answer.
2. Is there a second MCU controlling the triac/phase-cut stage, or is phase
   control performed directly by the Zigbee MCU?
3. Can you provide the stock firmware image matching the exact tuple above?
4. PCB revision identifier for my unit, if available.
5. If you are willing to disclose it: location/identification of the
   programming (SWS/debug) pads.

Thank you.