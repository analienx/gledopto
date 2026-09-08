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

## Family `0x01` — lamp output and PB4 auxiliary override

```text
A5 5A 01 LL 04 AA
```

This is the confirmed normal Zigbee On/Off + Level power-stage path and is also reused by the separate PB4 half-scale compatibility path.

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

### PB4 half-scale path — trigger solved

The previously unnamed `currentLevel >> 1` sender is now tied to **PB4**.

After PB4 remains high for 11 consecutive ~1 ms polls, stock repeatedly requests:

```text
A5 5A 01 (currentLevel >> 1) 04 AA
```

while PB4 remains high. A low PB4 sample resets qualification. The logical Zigbee `currentLevel` itself is not changed.

The trigger and arithmetic are therefore solved; the physical/business reason for PB4 is still `UNKNOWN`. Do not rename it thermal/overload/zero-cross without further evidence.

Detailed contract: `PB4_AUX_INPUT.md`.

## Stock startup synchronization

UART initialization itself does not emit a mandatory special controller frame. The application later restores/reads normal lamp state and runs the same family-`0x01` output refresh.

Current startup strategy for the independent implementation:

```text
initialize UART
restore Zigbee On/Off + currentLevel
emit normal family-0x01 output frame
```

No special mandatory startup handshake has been established.

## Local onboard keys — separated from PC2/PB4

The stock application also uses Telink's public-style `kb_scan_key(0, 1)` keyboard scanner. The decoded scan pins/key map are:

| GPIO | Encoded | Keycode | Functional role | Confidence |
|---|---:|---:|---|---|
| PC4 | `0x0210` | 1 | RESET | HIGH |
| PC3 | `0x0208` | 2 | LEVEL | HIGH |

PB4 is **not** a keyboard scan pin. PC2 is handled separately by the external PUSH pulse decoder.

The PC4/keycode-1 path includes the stock minimum-brightness configuration flow; the PC3/keycode-2 path is the local Level control path. Their existence further excludes PB4 from the normal user-button subsystem.

## PC2 external PUSH input — solved offline

Detailed functional specification: `PUSH_INPUT.md`.

The stock application polls encoded GPIO `0x0204`, TLSR8258 **PC2**, every 1 ms. PC2 is not treated as a simple DC button level; the firmware validates a repeating pulse waveform:

```text
>=3 low samples
>=6 high samples, with 51-high-sample timeout
>=3 further low samples
```

After qualification, a short completed activation toggles standard Zigbee On/Off. A sustained activation performs repeated standard Level changes. Long-release reverses the direction for the next long activation.

### Long PUSH step policy

```text
currentLevel <= 150 -> step 10
currentLevel >  150 -> step 25
```

First step occurs after 1001 accumulated low PC2 samples; repeats occur every 301 additional low samples. Upward behavior clamps to 254. Downward behavior deliberately preserves the stock edge:

```text
currentLevel <= step ? 2 : currentLevel - step
```

Independent implementation:

```text
src/glsd301p_push_input.h
src/glsd301p_push_input.c
tests/test_glsd301p_push_input.c
```

## PB4 auxiliary compatibility input — behavior solved, role unknown

Detailed functional specification: `PB4_AUX_INPUT.md`.

| Property | Value | State |
|---|---|---|
| GPIO | PB4 (`0x0110`) | CONFIRMED |
| Direction | input | CONFIRMED |
| Stock internal bias | 100 kΩ pulldown | HIGH |
| Poll cadence | ~1 ms | HIGH |
| Active level | high | CONFIRMED |
| Qualification | 11 consecutive high polls | CONFIRMED |
| Low behavior | reset qualification | CONFIRMED |
| Qualified behavior | family-`0x01`, value=`currentLevel >> 1` on each poll | CONFIRMED |
| Mutates logical Zigbee level | no | CONFIRMED |
| Physical/business role | `UNKNOWN` | UNKNOWN |

Because qualified PB4 reduces the physical output request while leaving logical level unchanged, a protection/derating interpretation is plausible. It is **not** relied upon by the implementation. Preserving the stock behavior is safer than deleting it merely because the semantic name is unknown.

Independent implementation:

```text
src/glsd301p_pb4_compat.h
src/glsd301p_pb4_compat.c
tests/test_glsd301p_pb4_compat.c
```

## Family `0x02` — auxiliary operation/pattern control

```text
A5 5A 02 MM 04 AA
```

Observed values:

```text
00 01 02 03 04 0F
```

This family belongs to a separate timed/stateful engine and vendor/private-control paths. It is not used by normal Zigbee On/Off/Level, the solved PC2 PUSH output, or the PB4 half-scale family-`0x01` request itself.

Individual value names remain `UNKNOWN`.

## Separate six-byte configuration/state message

A distinct endpoint-11 command-processing path transmits six configuration/state bytes directly rather than using the `A5 5A .. 04 AA` control template. Its semantics remain unknown and it is not established as necessary for ordinary On/Off/Level, PC2 PUSH, or PB4 compatibility behavior.

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
- PC2 external PUSH pulse decoder and short/long behavior;
- onboard keyboard mapping PC4=Reset / PC3=Level;
- PB4 sustained-high qualification;
- exact PB4 half-scale output behavior without naming its physical cause.

A physical UART capture or PCB trace is corroboration/optional parity work, not a prerequisite for implementing basic client dimming, external PUSH, or the PB4 compatibility behavior.

## Remaining full-parity questions

- exact semantic names of family-`0x02` values;
- physical/business role of PB4;
- vendor configuration-state message fields;
- exact meaning of the below-minimum bypass state;
- optional controller telemetry semantics;
- PCB traces, if physical corroboration is desired.

## Independent firmware boundary

```text
PC2 PUSH decoder -----\
PC3/PC4 local keys ----> Zigbee/ZCL state ----> normal output policy ----> family 01 UART
PB4 aux decoder -------/                         \
                                                  -> PB4 half-scale family 01 request
```

The installed production unit remains outside the first-flash path; this specification does not authorize flashing or mains-side probing.
