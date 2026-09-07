# GL-SD-301P — device ledger

GLEDOPTO GL-SD-301P, Zigbee AC dimmer, mains powered.

## Live unit (production, not a canary)

```text
friendly_name     LivingRoomCircleLightDimmer
IEEE              0xa4c13850cfcdb3a4
model             GL-SD-301P
manufacturer      GLEDOPTO
softwareBuildID   20651203
dateCode          20240704
current role      Router (firmware behavior)
endpoint          11 (0x0B)
```

## Architecture facts

| Fact | State |
|---|---|
| MCU family | Telink TC32/B85 TLSR8258 family — HIGH |
| MCU exact candidate | TLSR8258F512ET32 — HIGH |
| Flash size class | 512 KiB |
| POWER_STAGE_CONTROL | **SECOND_MCU_UART — HIGH software confidence** |
| UART wire settings | **9600 baud, 8N1** |
| UART pins | **TX PB1 / RX PA0** |
| Power-controller transaction width | **6 application bytes** |
| TLSR direct variable PWM/phase-cut path | NOT ESTABLISHED; only PWM enable/disable evidence in analyzed application |
| Exact six-byte controller protocol | PARTIAL — see `interoperability/INTERFACE.md` |
| Production encoder ready | **NO** |
| Flashable canary allowed | **NO** — protocol blockers remain + spare gate |
| OTA client liveness | ALIVE — responds to imageNotify with well-formed queryNextImageRequest |
| OTA identity tuple | manufacturerCode 0x124F, imageType 0x1416, live fileVersion 0x26013001 |
| Foundation ZCL discovery (0x0C/0x11/0x13/0x15) | NO RESPONSE on EP11 in bounded probe |
| Supported Basic attrs | 0x0000–0x0007, 0x0012, 0x4000; UNSUPPORTED: 0x0008–0x000E, 0x0011 |
| Level Control supported reads | currentLevel 0x0000, startUpCurrentLevel 0x4000 (=255); 0x0002/0x0003/0x000F/0x0010–0x0014 UNSUPPORTED |
| OnOff reads | onOff 0x0000, startUpOnOff 0x4003 (=1) |
| Existing binds | genOnOff + genLevelCtrl → 0xfdb1122d004b1200 EP1 (do not mutate) |
| Reporting | onOff (0/65000/1), currentLevel (5/65000/1) |

## Implementation direction

The independent firmware should preserve the existing power-stage controller and
communicate with it through a cleanly implemented UART adapter. Do **not** invent
a new mains phase-cut algorithm unless future physical evidence proves that is
required.

The Zigbee side remains independently implementable with the public Telink SDK:
RX-on-when-idle End Device, mains powered, endpoint/cluster compatibility,
reporting and direct binding.

The controller encoder remains compile/release gated until every safety-critical
six-byte field used for ON/OFF/level/startup has been independently measured.

Canonical clean-room interface: `interoperability/INTERFACE.md`.
Status history: `STATUS.md`.

Raw/vendor firmware is not repository content. See root `CLEAN_ROOM.md`.
