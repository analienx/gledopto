# GL-SD-301P interoperability interface

Status: **engineering interface specification**

Purpose: define the externally relevant facts required for an independently written GL-SD-301P Zigbee End Device/client firmware to interoperate with the existing dimmer hardware.

Detailed vendor-machine-code notes belong under `../vendor-firmware/reference-analysis/`; implementation-facing code uses this functional specification plus public Telink/Zigbee documentation.

## Confidence vocabulary

- `CONFIRMED` — directly established from consistent interface/application behavior or authoritative public definitions.
- `HIGH` — strongly established; hardware measurement would be corroboration rather than the primary basis.
- `INFERRED` — best explanation of confirmed evidence, but not yet independently measured at the physical boundary.
- `UNKNOWN` — implementation must not guess.

## Architecture

| Property | State | Confidence |
|---|---|---|
| Main Zigbee MCU family | Telink TLSR8258/B85, 512 KiB class | CONFIRMED |
| Exact candidate | TLSR8258F512ET32 | HIGH |
| Power-stage control | `SECOND_MCU_UART` | HIGH / software-confirmed |
| Physical PCB destination of UART | attached dimmer/power-stage controller | INFERRED; board trace would corroborate |
| TLSR directly generates variable phase-cut level | no supporting variable-duty path established | NOT ESTABLISHED |

The normal variable lamp level is carried through the serial power-controller boundary. Do not invent a new mains phase-cut algorithm in the independent firmware.

## UART interface

| Parameter | Value | State |
|---|---:|---|
| Baud | 9600 | CONFIRMED |
| Data bits | 8 | CONFIRMED |
| Parity | none | CONFIRMED |
| Stop bits | 1 | CONFIRMED |
| TX pin | PB1 | HIGH |
| RX pin | PA0 | HIGH |
| Stock transport | DMA TX + RX configured | CONFIRMED, not a wire requirement |
| Control payload bytes | 6 | CONFIRMED |
| RX required for core On/Off/Level | no | HIGH |

The Telink DMA driver's internal four-byte length prefix is not part of the UART wire payload. The stock receive callback in the analyzed application resolves to a no-op return stub, so RX is not a blocker for ordinary lamp control.

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

Identified live control senders change only bytes 2 and 3 before transmitting six bytes. There is therefore no varying checksum byte in this control-frame form.

## Family `0x01` — normal lamp output

```text
A5 5A 01 LL 04 AA
```

This is the confirmed normal Zigbee On/Off + Level power-stage path.

### OFF

```text
A5 5A 01 00 04 AA
```

Normal stock OFF updates the standard On/Off state to false and immediately runs the same serial output refresh used by Level Control. No family-`0x02` command is required.

### ON / Level

When logically ON, `LL` is the standard Zigbee `currentLevel` after the product's configured minimum-output handling:

```text
LL = max(currentLevel, configuredMinimumOutput)
```

The analyzed default minimum-output threshold is `0x02`. An explicit runtime state can permit below-minimum output; its purpose remains unnamed and is exposed as an explicit policy flag rather than guessed.

The vendor Level Control progression matches Telink's public sample-light state machinery closely enough that ordinary `MoveToLevel`, `Move`, `Step` and transition progression can use an independent/public ZCL implementation and emit the resulting `currentLevel` through family `0x01`.

### Special family-`0x01` path

A separate non-normal-output path sends:

```text
A5 5A 01 (currentLevel >> 1) 04 AA
```

It is not used by ordinary Zigbee On/Off/Level and remains isolated until its trigger is completely named.

## Stock startup synchronization

UART initialization itself does not emit a mandatory special controller frame. The application later restores/reads normal lamp state and runs the same family-`0x01` output refresh.

Current startup strategy for the independent implementation:

```text
initialize UART
restore Zigbee On/Off + currentLevel
emit normal family-0x01 output frame
```

No special mandatory startup handshake has been established.

## PC2 physical PUSH input — solved offline

Detailed functional specification: `PUSH_INPUT.md`.

The stock application polls encoded GPIO `0x0204`, which is TLSR8258 **PC2**, every 1 ms.

PC2 is not treated as a simple DC button level. The firmware validates a repeating pulse waveform:

```text
>=3 low samples
>=6 high samples, with 51-high-sample timeout
>=3 further low samples
```

After that qualification the input is considered active. This pulse-oriented behavior is consistent with a mains-derived/opto-isolated PUSH terminal sense input rather than a direct logic switch.

The semantic identification is independently corroborated by the GL-SD-301P manufacturer/user documentation: the external PUSH input performs short-press On/Off and long-press brightness adjustment, reversing long-press direction after release. That exactly matches the PC2 application state machine.

**Classification:** `PC2 = external PUSH-sense path` — HIGH confidence. PCB continuity would corroborate the net but is not required to reproduce behavior.

### Short PUSH

When a qualified activation ends with 51 consecutive high samples, stock invokes its normal On/Off updater:

```text
ON  -> OFF
OFF -> ON
```

The resulting power-stage output is therefore the same normal family-`0x01` frame described above.

### Long PUSH

While the qualified pulse waveform remains present, stock accumulates **low PC2 samples**:

- first level step after 1001 accumulated low samples;
- repeat level step every 301 additional accumulated low samples;
- high gaps below 51 samples preserve the activation and do not clear the accumulated long-hold counter.

Because this is a pulse waveform, accumulated low-sample time is not necessarily equal to elapsed wall-clock time.

### Local level-step policy

```text
currentLevel <= 150 -> step 10
currentLevel >  150 -> step 25
```

Initial direction is downward from zero-initialized state. Long-release reverses direction for the next long activation.

Up:

```text
min(254, currentLevel + step)
```

Down — preserving the observed stock edge behavior:

```text
currentLevel <= step ? 2 : currentLevel - step
```

The local adjustment routine is gated by logical On/Off state.

Independent implementation:

```text
src/glsd301p_push_input.h
src/glsd301p_push_input.c
tests/test_glsd301p_push_input.c
```

The decoder emits semantic `TOGGLE` / `LEVEL_STEP` events and does not directly send UART or mutate ZCL state.

## Family `0x02` — auxiliary operation/pattern control

```text
A5 5A 02 MM 04 AA
```

Observed values:

```text
00 01 02 03 04 0F
```

This family belongs to a separate timed/stateful engine and vendor/private-control paths. It is not used by normal Zigbee On/Off/Level and is not required for the solved PC2 PUSH toggle/level behavior.

Individual value names remain `UNKNOWN`.

## Separate six-byte configuration/state message

A distinct endpoint-11 command-processing path transmits six configuration/state bytes directly rather than using the `A5 5A .. 04 AA` control template. Its semantics remain unknown and it is not established as necessary for ordinary On/Off/Level or PC2 PUSH behavior.

## Solved without UART wiring

The reference firmware plus public Telink source and public product behavior establish offline:

- secondary UART power-stage architecture;
- 9600 8N1 transport;
- likely PB1 TX / PA0 RX assignment;
- six-byte control framing;
- normal electrical OFF;
- normal ON/current-level output;
- minimum-output handling;
- normal Level Control integration;
- startup state re-sync;
- RX non-dependency for core operation;
- PC2 1-ms PUSH pulse decoder;
- short physical PUSH -> On/Off toggle;
- long physical PUSH -> repeated Level changes;
- stock local step sizes and direction reversal.

A physical UART capture or PCB trace is now corroboration/optional parity work, not a prerequisite for implementing basic client dimming and physical PUSH behavior.

## Remaining full-parity questions

- exact semantic names of family-`0x02` values;
- purpose/trigger of the `currentLevel >> 1` family-`0x01` path;
- vendor configuration-state message fields;
- exact meaning of the below-minimum bypass state;
- optional controller telemetry semantics;
- terminal-to-PC2 and UART PCB traces, if physical corroboration is desired.

## Independent firmware boundary

```text
Zigbee/ZCL state + PC2 PUSH decoder
        |
        v
normal output policy
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

The installed production unit remains outside the first-flash path; this specification does not authorize flashing or mains-side probing.
