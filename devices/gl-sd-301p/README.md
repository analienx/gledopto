# GL-SD-301P — device ledger

GLEDOPTO GL-SD-301P Zigbee triac AC dimmer, mains powered.

## Production unit

```text
friendly_name     LivingRoomCircleLightDimmer
IEEE              0xa4c13850cfcdb3a4
model             GL-SD-301P
manufacturer      GLEDOPTO
softwareBuildID   20651203
dateCode          20240704
stock role        Router
endpoint          11 (0x0B)
track             PRODUCTION_ONLY_NO_SPARE
```

## Established architecture

| Fact | State |
|---|---|
| Zigbee MCU family | Telink TC32/B85, TLSR8258 family |
| Flash class | 512 KiB |
| POWER_STAGE_CONTROL | **SECOND_MCU_UART** |
| Power-stage UART | **9600 8N1, PB1 TX / PA0 RX** |
| Normal control frame | **A5 5A 01 VV 04 AA** |
| Confirmed electrical OFF | **A5 5A 01 00 04 AA** |
| Physical PUSH input | **PC2** |
| Auxiliary compatibility input | **PB4** |
| Flash safe-voltage/VBAT ADC pin | **PB3** |
| OTA client | Present/reachable on stock lineage |
| Endpoint | 11 |
| Existing stock binds | genOnOff + genLevelCtrl → coordinator-side target recorded in evidence |

## Independent End Device candidate

The current safety-reviewed candidate is PR #6 at exact head:

```text
760c141925f29e517831c92afd61bd9c15e1b7b5
```

Target architecture:

```text
STACK_ARCHIVE=libzb_ed.a
ZB_ED_ROLE=1
ZB_ROUTER_ROLE=0
ZB_MAC_RX_ON_WHEN_IDLE=1
PM_ENABLE=0
ENDPOINT=11
POWER_SOURCE=MAINS
```

Key safety properties verified on that frozen software head include:

- consistent pinned-TC32 SDK/application ABI with compile-time layout gates;
- physical PUSH takeover cancels stale remote Level transitions/moves;
- reserved Zigbee Level `0xFF` is rejected before normalization;
- runtime UART transport is bounded/nonblocking with strict OFF priority;
- startup/fault paths fail closed to confirmed electrical OFF;
- host/runtime/target contract tests and exact-head TC32 build are green.

Same-head CI:

```text
Interoperability boundary and host tests  34276666901  SUCCESS
GL-SD-301P final no-spare readiness       34276666846  SUCCESS
```

Frozen final binary SHA-256:

```text
d6f41fb720002390817b56cc925dae08a657ca9f77b94c72c646af49a7426551
```

Frozen quarantined OTA wrapper SHA-256:

```text
43e6996fb55dd5569f450117ac430b8dd11d35f23a878b4680439f84b65c899b
```

## Current gate

```text
SOFTWARE_GATE            = GREEN
SAFETY_SWEEP_BLOCKERS    = CLOSED
CANARY_DECISION_READY    = YES
LIVE_DEVICE_VALIDATED    = NO
GENERAL_RELEASE          = NO
```

The remaining proof is hardware/runtime validation on the exact production
device under the latest bounded authorization in issue #1. Green CI is not
equivalent to a successful live conversion.

Canonical clean-room interface and safety documentation live alongside the
candidate branch/PR. Status history: `STATUS.md`. Evidence: repo `evidence/`
plus raw private captures retained only in their authorized runtime location.
