# GL-SD-301P interoperability interface

Status: **engineering interface specification**

Purpose: define only the externally relevant facts required for an independently written GL-SD-301P Zigbee End Device/client firmware to interoperate with the existing dimmer hardware.

No vendor firmware, disassembly, decompiled code or translated implementation belongs in this file.

## Confidence vocabulary

- `CONFIRMED` — supported by multiple consistent software/interface observations or a public authoritative hardware/SDK definition.
- `HIGH` — strongly supported, with one final black-box/physical confirmation still useful.
- `INFERRED` — best explanation of confirmed evidence, but not yet independently measured at the electrical boundary.
- `UNKNOWN` — implementation must not guess.

## Architecture decision

| Property | State | Confidence |
|---|---|---|
| Main Zigbee MCU family | Telink TLSR8258/B85, 512 KiB class | CONFIRMED |
| Exact candidate | TLSR8258F512ET32 | HIGH |
| Power-stage control classification | `SECOND_MCU_UART` | HIGH / software-confirmed |
| Physical PCB destination of UART | secondary dimmer/power-stage controller | INFERRED; spare trace pending |
| TLSR directly generates phase-cut brightness timing | no supporting variable-duty path established | NOT ESTABLISHED |

### Basis for `SECOND_MCU_UART`

The product application has a live lighting/control path which transmits fixed-length control messages through the Telink UART driver. The same firmware configures a dedicated 9600-baud UART and invokes its transmit path from level/transition-related application operations.

By contrast, TLSR8258 PWM references found in the application are limited to channel enable/disable control. No corresponding PWM compare/cycle writes establishing the variable output level were identified in the relevant application path. This is not proof that PWM hardware is never used; it is sufficient to avoid designing the new firmware around an unproven direct phase-cut implementation.

**Implementation consequence:** preserve the serial power-controller boundary. Do not independently invent mains phase-cut timing.

## UART electrical/digital interface

| Parameter | Value | State |
|---|---:|---|
| UART instance | TLSR8258 UART | CONFIRMED |
| Baud | 9600 | CONFIRMED |
| Data bits | 8 | CONFIRMED |
| Parity | none | CONFIRMED |
| Stop bits | 1 | CONFIRMED |
| TX pin | PB1 | HIGH |
| RX pin | PA0 | HIGH |
| Vendor transport implementation | DMA TX + RX configured | CONFIRMED but not a compatibility requirement |
| Application RX handling | no meaningful receive callback observed in the analyzed control path | CONFIRMED for analyzed firmware; do not assume protocol is permanently TX-only |

The public Telink TLSR8258 SDK defines PB1 as a valid UART TX pin and PA0 as a valid UART RX pin. The application configures those encoded pin values immediately before UART initialization.

The Telink SDK driver internally creates a four-byte DMA length prefix before the application payload. That prefix is **driver metadata, not part of the bytes transmitted on the UART wire**.

An independent implementation may use DMA or non-DMA UART as convenient, provided the wire behaviour is equivalent.

## Control frame — confirmed wire structure

The shared power-control message is exactly six application bytes:

```text
A5 5A CC VV 04 AA
```

where:

| Byte | Value / meaning | State |
|---:|---|---|
| 0 | `0xA5` fixed prefix | CONFIRMED |
| 1 | `0x5A` fixed prefix | CONFIRMED |
| 2 | `CC` command family | CONFIRMED |
| 3 | `VV` family-specific value | CONFIRMED |
| 4 | `0x04` fixed control-frame field | CONFIRMED; semantic name UNKNOWN |
| 5 | `0xAA` fixed suffix | CONFIRMED |

The initialized control buffer contains these fixed framing bytes and every identified live sender using that buffer changes only bytes 2 and 3 before transmitting six bytes.

Consequently there is no varying checksum byte in this control-frame form: bytes 0, 1, 4 and 5 stay constant while command/value change. Do not infer what `0x04` *means* merely from its position.

## Command family `0x01` — output level

Canonical control-frame form:

```text
A5 5A 01 LL 04 AA
```

The main level-output path uses `LL` as follows:

- when the application's output-enabled/on state is false, `LL = 0`;
- otherwise `LL` is the current Zigbee Level Control `currentLevel` value;
- if that level is below the configured minimum output level, the application either resolves the exceptional state or clamps to the configured minimum before sending.

Identification of the level source is strengthened by a direct match between the application's level-update arithmetic and the public Telink `light_applyUpdate()` behaviour for `currentLevel` (1..254).

**State: HIGH.** This is sufficient to implement and host-test the raw family-`0x01` encoder, but the minimum-level policy and one special physical/control path still require black-box correlation before the high-level power-stage API is release-ready.

### Special family-`0x01` path

A separate normal application path intentionally sends a value derived as `currentLevel >> 1`. It is not yet proven whether this is a physical-control calibration/action, controller configuration operation, or another output mode. Therefore the independent implementation must not globally apply either `level` or `level >> 1` without knowing which high-level operation is being reproduced.

## Command family `0x02` — operation/transition control

Canonical frame form:

```text
A5 5A 02 MM 04 AA
```

Values confirmed in live application paths include:

```text
00 01 02 03 04 0F
```

The value participates in transition/operation state handling. The exact semantic map is still **UNKNOWN**. In particular, do not name these values `on`, `off`, `leading-edge`, `trailing-edge`, etc. until correlated against black-box UART traffic and externally observed behavior.

## Separate six-byte configuration/state transmission

A separate UART transmission path constructs another six-byte payload directly from configuration/state bytes rather than using the `A5 5A .. 04 AA` control buffer.

This proves that the UART transports more than one six-byte message form. Its field semantics and whether it is required during startup synchronization are **UNKNOWN**.

## Remaining blockers

The raw control-frame encoder itself is now structurally known. The blockers are higher-level protocol semantics and startup/safety behavior:

- exact semantic map for family `0x02` values;
- exact purpose/trigger for the special `currentLevel >> 1` family-`0x01` path;
- minimum-brightness policy expected by the secondary controller across all modes;
- controller startup/configuration six-byte message semantics;
- electrical OFF sequence and whether family `0x01` level 0 alone is sufficient;
- power-on synchronization order;
- controller-to-TLSR response/telemetry semantics, if operationally relevant.

**No flashable implementation may invent these values or sequences.**

## Next engineering experiment

The shortest remaining path is black-box capture on the sacrificial spare:

1. identify PB1/PA0 on the low-voltage module/controller boundary by continuity with the unit unpowered;
2. capture UART traffic with appropriately isolated instrumentation while exercising only known normal controls;
3. record exactly one variable at a time: OFF, ON, stable levels (1, 10, 25, 50, 75, 100%), then one controlled transition;
4. include physical PUSH short/hold operations to resolve the special `level >> 1` path;
5. repeat each state to distinguish control frames from startup/configuration frames;
6. capture PA0/controller-to-TLSR traffic separately;
7. publish only sanitized input/output vectors and semantic conclusions.

This should finish the controller protocol without reconstructing the vendor's internal implementation.

## Independent firmware boundary

The independent firmware should expose:

```text
glsd_power_stage_init()
glsd_power_stage_set_onoff(on)
glsd_power_stage_set_level(level, transition_ds)
glsd_power_stage_stop()
glsd_power_stage_sync()
```

Below that API, a small raw control-frame encoder may already be independently implemented from the confirmed interface:

```text
encode_control(CC, VV) -> A5 5A CC VV 04 AA
```

The Zigbee/ZCL implementation owns Zigbee semantics. The power-stage adapter owns only confirmed serial interoperability behavior.

Until the remaining semantic/startup blockers are measured, the high-level `glsd_power_stage_*` implementation remains release-gated and must not produce a flashable canary image.
