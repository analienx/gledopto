# GL-SD-301P interoperability interface

Status: **engineering interface specification**

Purpose: define only the externally relevant facts required for an independently written GL-SD-301P Zigbee End Device/client firmware to interoperate with the existing dimmer hardware.

No vendor firmware, disassembly, decompiled code or translated implementation belongs in this file.

## Confidence vocabulary

- `CONFIRMED` — supported by multiple consistent software/interface observations or a public authoritative hardware/SDK definition.
- `INFERRED` — best explanation of confirmed evidence, but not yet independently measured at the electrical boundary.
- `UNKNOWN` — implementation must not guess.

## Architecture decision

| Property | State | Confidence |
|---|---|---|
| Main Zigbee MCU family | Telink TLSR8258/B85, 512 KiB class | CONFIRMED |
| Exact candidate | TLSR8258F512ET32 | HIGH |
| Power-stage control classification | `SECOND_MCU_UART` | HIGH / software-confirmed |
| Physical PCB destination of UART | secondary dimmer/power-stage controller | INFERRED; spare trace pending |
| TLSR directly generates phase-cut brightness timing | no supporting evidence found | NOT ESTABLISHED |

### Basis for `SECOND_MCU_UART`

The product application has a live lighting/control path which transmits fixed-length control messages through the Telink UART driver. The same firmware configures a dedicated 9600-baud UART and invokes its transmit path from level/transition-related application operations.

By contrast, TLSR8258 PWM references found in the same application are limited to channel enable/disable control. No evidence of the PWM compare/cycle writes that would establish a continuously variable duty value was identified in the relevant application. This is not proof that PWM hardware is never used; it is sufficient to avoid designing the new firmware around an unproven direct phase-cut implementation.

**Implementation consequence:** the first independent firmware must treat the attached serial controller as the existing power-stage abstraction. Do not independently invent mains phase-cut timing.

## UART electrical/digital interface

| Parameter | Value | State |
|---|---:|---|
| UART instance | TLSR8258 UART | CONFIRMED |
| Baud | 9600 | CONFIRMED |
| Data bits | 8 | CONFIRMED |
| Parity | none | CONFIRMED |
| Stop bits | 1 | CONFIRMED |
| TX pin | PB1 | CONFIRMED/HIGH |
| RX pin | PA0 | CONFIRMED/HIGH |
| Vendor transport implementation | DMA TX + RX configured | CONFIRMED but not a compatibility requirement |
| Application RX handling | no meaningful receive callback observed in the analyzed path | CONFIRMED for analyzed firmware; do not assume protocol is permanently TX-only |

The vendor-side SDK driver internally creates a four-byte DMA length prefix before the application payload. That prefix is **driver metadata, not part of the six bytes transmitted on the UART wire**.

An independent implementation may use DMA or non-DMA UART as convenient, provided the wire behaviour is equivalent.

## Wire transaction size

`CONTROL_PAYLOAD_BYTES = 6` — **CONFIRMED**.

Multiple live lighting/control transmit paths send exactly six application bytes.

Do not append the SDK's internal four-byte DMA buffer prefix to the wire message.

## Six-byte frame knowledge

Byte numbering is zero-based.

| Byte | Known behaviour | State |
|---:|---|---|
| 0 | not yet semantically resolved | UNKNOWN |
| 1 | not yet semantically resolved | UNKNOWN |
| 2 | command-family discriminator in control paths | CONFIRMED |
| 3 | dynamic command value associated with family in byte 2 | CONFIRMED |
| 4 | not yet semantically resolved | UNKNOWN |
| 5 | not yet semantically resolved | UNKNOWN |

### Family `0x01`

Observed in a level-related control path:

```text
byte[2] = 0x01
byte[3] = dynamic level/state-derived value
```

`byte[3]` is demonstrably derived from the application's changing level/state, but the exact canonical mapping from Zigbee `currentLevel` (1..254) to wire value is **UNKNOWN**. At least two control paths transform/select the value differently, so a single shift/clamp formula must not be assumed from static analysis alone.

### Family `0x02`

Observed in transition/mode control paths:

```text
byte[2] = 0x02
byte[3] = dynamic operation/mode value
```

Values observed in the application path include `0`, `1`, `2`, `3`, `4`, and `15`. Their complete behavioural mapping is **UNKNOWN**. They must not yet be named `on`, `off`, `leading-edge`, `trailing-edge`, etc. without an independent black-box correlation.

### Configuration/status frame

A separate six-byte transmission path populates all six bytes from device configuration/state. Its field semantics are **UNKNOWN**. It proves that six bytes is a general controller transaction width, not that every transaction uses the same two-field layout.

## What is deliberately not specified yet

The following are blockers for a production power-stage encoder and are intentionally `UNKNOWN`:

- bytes 0, 1, 4 and 5 for each command family;
- checksum/CRC presence or absence;
- sequence/counter semantics;
- exact level-to-wire mapping;
- exact `0x02` operation mapping;
- whether configuration frames require acknowledgement;
- receiver response/telemetry semantics;
- controller startup handshake, if any;
- minimum-brightness translation at the serial boundary;
- electrical OFF command sequence;
- power-on synchronization sequence.

**No flashable implementation may manufacture values for these fields.**

## Next engineering experiment

The shortest remaining path is black-box capture on the sacrificial spare:

1. identify PB1/PA0 on the low-voltage module/controller boundary by continuity with the unit unpowered;
2. capture UART traffic with appropriately isolated instrumentation while exercising only known normal controls;
3. record exactly one variable at a time: OFF, ON, then stable levels (1, 10, 25, 50, 75, 100%), then one controlled transition;
4. correlate six on-wire bytes with the commanded/read-back Zigbee state;
5. repeat each state to distinguish constants from counters/checksums;
6. capture controller-to-TLSR traffic separately on PA0;
7. publish only the resulting interface table/test vectors, not raw proprietary firmware material.

This experiment should resolve the encoder without requiring reconstruction of the vendor's internal control algorithm.

## Firmware boundary

The independent firmware should expose a small abstraction such as:

```text
glsd_power_stage_init()
glsd_power_stage_set_onoff(on)
glsd_power_stage_set_level(level, transition_ds)
glsd_power_stage_stop()
glsd_power_stage_sync()
```

The Zigbee/ZCL implementation owns Zigbee semantics. The power-stage adapter owns only the confirmed serial interoperability protocol. This keeps the Zigbee End Device role conversion independent from proprietary dimmer-controller details.

Until the unresolved frame fields are measured, `glsd_power_stage_*` must remain behind a compile/test gate and must not produce a flashable canary image.
