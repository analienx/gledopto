# GL-SD-301P — device ledger

GLEDOPTO GL-SD-301P, Zigbee triac AC dimmer (phase-cut), mains powered.

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

## Class A / architecture facts

| Fact | State |
|---|---|
| MCU_EXACT | UNKNOWN |
| POWER_STAGE_CONTROL | UNKNOWN (SECOND_MCU_UART / TELINK_DIRECT unresolved) |
| OTA client liveness | ALIVE — responds to imageNotify with well-formed queryNextImageRequest |
| OTA identity tuple | manufacturerCode 0x124F, imageType 0x1416, fileVersion 0x26013001, fieldControl 0 (live-captured 2026-09-03) |
| Public stock binary | NONE found |
| Foundation ZCL discovery (0x0C/0x11/0x13/0x15) | NO RESPONSE on EP11 (all four clusters probed) |
| Supported Basic attrs | 0x0000–0x0007, 0x0012, 0x4000; UNSUPPORTED: 0x0008–0x000E, 0x0011 |
| Level Control supported reads | currentLevel 0x0000, startUpCurrentLevel 0x4000 (=255); 0x0002/0x0003/0x000F/0x0010–0x0014 UNSUPPORTED |
| OnOff reads | onOff 0x0000, startUpOnOff 0x4003 (=1) |
| Existing binds | genOnOff + genLevelCtrl → 0xfdb1122d004b1200 EP1 (do not mutate) |
| Reporting | onOff (0/65000/1), currentLevel (5/65000/1) |

Status history: see `STATUS.md`. Evidence: `evidence/` in repo root.
