# GL-SD-301P interoperability interface

Status: **engineering interface specification**

Purpose: define the externally relevant facts required for an independently written GL-SD-301P Zigbee End Device/client firmware to interoperate with the existing dimmer hardware.

Detailed vendor-machine-code notes belong under `../vendor-firmware/reference-analysis/`; implementation-facing code should use this functional specification plus public Telink/Zigbee documentation.

## Confidence vocabulary

- `CONFIRMED` — directly established from consistent interface/application behavior or authoritative public definitions.
- `HIGH` — strongly established; hardware measurement would be useful corroboration rather than the primary basis.
- `INFERRED` — best explanation of confirmed evidence, but not yet independently measured at the physical boundary.
- `UNKNOWN` — implementation must not guess.

## Architecture decision

| Property | State | Confidence |
|---|---|---|
| Main Zigbee MCU family | Telink TLSR8258/B85, 512 KiB class | CONFIRMED |
| Exact candidate | TLSR8258F512ET32 | HIGH |
| Power-stage control classification | `SECOND_MCU_UART` | HIGH / software-confirmed |
| Physical PCB destination of UART | attached dimmer/power-stage controller | INFERRED; board trace would corroborate |
| TLSR directly generates variable phase-cut level | no supporting variable-duty path established | NOT ESTABLISHED |

The application sends lighting state through the Telink UART driver. The variable lamp level is carried in that serial protocol; identified TLSR PWM accesses are limited to enable/disable-style control and are not the normal variable-level path.

**Implementation consequence:** preserve the serial power-controller boundary. Do not invent a new mains phase-cut algorithm.

## UART interface

| Parameter | Value | State |
|---|---:|---|
| UART instance | TLSR8258 UART | CONFIRMED |
| Baud | 9600 | CONFIRMED |
| Data bits | 8 | CONFIRMED |
| Parity | none | CONFIRMED |
| Stop bits | 1 | CONFIRMED |
| TX pin | PB1 | HIGH |
| RX pin | PA0 | HIGH |
| Stock transport | DMA TX + RX configured | CONFIRMED, not a wire requirement |
| Control payload bytes | 6 | CONFIRMED |
| RX required for core On/Off/Level | no evidence of such requirement | HIGH |

The public Telink TLSR8258 SDK defines PB1 as a valid UART TX pin and PA0 as a valid UART RX pin. The stock application configures those encoded pins before UART initialization.

The Telink driver prepends a four-byte length field only inside its DMA buffer. That prefix is **not part of the six application bytes transmitted on the UART wire**.

The receive callback supplied by the analyzed application resolves to a no-op return stub. RX may be retained in an independent implementation for diagnostics/future compatibility, but controller responses are not presently a blocker for ordinary lamp On/Off/Level operation.

## Confirmed control frame

```text
A5 5A CC VV 04 AA
```

| Byte | Value / meaning | State |
|---:|---|---|
| 0 | `0xA5` fixed prefix | CONFIRMED |
| 1 | `0x5A` fixed prefix | CONFIRMED |
| 2 | `CC` command family | CONFIRMED |
| 3 | `VV` family-specific value | CONFIRMED |
| 4 | `0x04` fixed field | CONFIRMED; semantic label unknown |
| 5 | `0xAA` fixed suffix | CONFIRMED |

The control buffer is initialized with these fixed framing bytes and identified live control senders change only bytes 2 and 3 before transmitting exactly six bytes. There is therefore no varying checksum byte in this control-frame form.

## Family `0x01` — normal lamp output

```text
A5 5A 01 LL 04 AA
```

This is now the **confirmed normal Zigbee On/Off + Level power-stage path**.

### OFF

Normal Zigbee OFF updates the application's standard On/Off state to false and then immediately executes the same serial output refresh used by Level Control. The resulting frame is:

```text
A5 5A 01 00 04 AA
```

**State: CONFIRMED.** No family-`0x02` command is used by the normal stock OFF path.

### ON

Normal Zigbee ON sets the standard On/Off state true and invokes the same output refresh. The transmitted value is the current Zigbee Level Control `currentLevel`, after the product's configured minimum-output handling:

```text
A5 5A 01 LL 04 AA
```

where ordinarily:

```text
LL = max(currentLevel, configuredMinimumOutput)
```

The analyzed configuration's default minimum-output threshold is `0x02`. The application also has an explicit runtime state which can temporarily permit a level below that threshold; because the meaning of that state is not yet named, the independent policy exposes it explicitly rather than guessing its purpose.

### Level Control

The vendor level-transition/update arithmetic closely matches Telink's public sample-light `light_applyUpdate()` state machine. Each relevant level refresh calls the family-`0x01` sender described above.

Therefore ordinary Zigbee `MoveToLevel`, `Move`, `Step` and transition progression can keep using the public/independently implemented ZCL state machine and emit the current resulting level through family `0x01`.

**Core encoder state: READY FOR HOST/BUILD INTEGRATION.**

### Special family-`0x01` path

A separate application path sends:

```text
A5 5A 01 (currentLevel >> 1) 04 AA
```

That path is not the ordinary Zigbee On/Off/Level refresh. Its trigger sits in a separate local state machine and remains under investigation. The normal output encoder must **not** globally apply this shift.

## Stock startup synchronization

The stock initialization path initializes the UART and later restores/reads the standard lamp state through the same application state machinery. That path invokes the normal On/Off refresh, which in turn emits family `0x01` with either zero or the restored current level.

No separate controller handshake is emitted directly by UART initialization, and the separately observed six-byte configuration transmission is in a command-handler path rather than adjacent to UART startup.

**Implementation consequence:** current evidence supports startup synchronization by emitting the normal family-`0x01` frame from restored On/Off + currentLevel state. A special mandatory startup frame has not been established.

## Family `0x02` — auxiliary operation/pattern control

```text
A5 5A 02 MM 04 AA
```

Observed values include:

```text
00 01 02 03 04 0F
```

The family is used by a separate timed/stateful engine with recurring 200/400/500 ms-style intervals and by configuration/private-control paths. It is **not used by the normal Zigbee On/Off/Level output path**.

Current classification: **auxiliary operation/pattern/configuration control — HIGH**, with individual value names still `UNKNOWN`.

This means family `0x02` is not a blocker for implementing and testing basic Zigbee lamp control. It may still be necessary to reproduce all physical PUSH feedback, commissioning indications or vendor-specific behavior.

## Separate six-byte configuration/state message

A distinct command-handler path transmits six configuration/state bytes directly rather than using the `A5 5A .. 04 AA` control template. It occurs in an endpoint-11 command-processing path and is not evidence of a mandatory UART-startup handshake.

Its exact field semantics remain `UNKNOWN` and should be implemented only if required by a feature we choose to preserve.

## What is solved without UART wiring

The stock image plus public Telink source are now sufficient to establish offline:

- serial power-stage architecture;
- 9600 8N1 transport;
- likely PB1 TX / PA0 RX assignment;
- six-byte control framing;
- normal electrical OFF frame;
- normal ON/current-level frame;
- minimum-output clamp behavior;
- ordinary Level Control integration;
- startup re-synchronization through the same normal output path;
- absence of a meaningful RX callback in the analyzed control path;
- family `0x02` being outside normal On/Off/Level output.

A physical UART capture is therefore **corroboration and a tool for optional-feature completion**, not a prerequisite for basic client dimming.

## Remaining optional/full-parity questions

These no longer block the core On/Off/Level encoder, but they matter for complete stock-feature parity:

- exact semantics of each family-`0x02` value;
- exact purpose of the `currentLevel >> 1` path;
- physical PUSH short/hold state-machine mapping;
- vendor-specific configuration message field meanings;
- whether any optional controller telemetry should be surfaced;
- exact meaning of the runtime below-minimum bypass state.

## Independent firmware boundary

The implementation is intentionally split:

```text
Zigbee/ZCL state machine
        |
        v
glsd301p_normal_output_frame_encode()
        |
        v
A5 5A 01 LL 04 AA
        |
        v
9600 8N1 UART TX
        |
        v
existing secondary power controller
```

The raw framing and normal output-policy components can be host-tested independently from Telink hardware. Family `0x02` and special local-control behavior remain separate modules until their semantics are established.

The installed production unit remains outside the first-flash path; this specification does not authorize flashing or mains-side probing.
