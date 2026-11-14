# GL-SD-301P PB4 auxiliary-input compatibility

Status: **behavior solved; business/physical role UNKNOWN**

This specification preserves the observable stock behavior of TLSR8258 GPIO **PB4** without assigning an unsupported semantic name such as thermal fault, overload, zero-cross, or user button.

## Pin and polling

- GPIO: `PB4`
- Telink encoded value: `0x0110`
- direction: input
- stock internal bias: 100 kΩ pulldown
- poll cadence: approximately 1 ms, in the same periodic callback that services PC2 PUSH input

PB4 is not one of the stock Telink keyboard scan inputs used for the onboard Level/Reset buttons. The decoded keyboard scan pins are PC4 and PC3; PC2 is the separate external PUSH-terminal sense path.

## Qualification behavior

The stock application counts consecutive PB4-high polls.

```text
PB4 low
    -> qualification counter = 0
    -> no auxiliary refresh

PB4 high for polls 1..10
    -> counter increments
    -> no auxiliary refresh

PB4 high on poll 11
    -> auxiliary half-scale refresh requested

PB4 remains high
    -> auxiliary half-scale refresh requested on each further poll
```

Any low sample resets qualification immediately.

## Half-scale output behavior

The auxiliary refresh does **not** update the logical Zigbee Level Control `currentLevel`.

Instead it sends the normal family-`0x01` power-stage frame with the value byte derived as:

```text
physicalRequest = currentLevel >> 1
```

Wire form:

```text
A5 5A 01 (currentLevel >> 1) 04 AA
```

Example:

```text
logical currentLevel = 200 (0xC8)
PB4 qualified high
physical request      = 100 (0x64)
wire frame            = A5 5A 01 64 04 AA
logical currentLevel remains 200
```

This separation between logical state and physical request is why PB4 must not be folded into the normal Level Control policy.

## Semantic classification

Confirmed:

- PB4 is a dedicated active-high auxiliary input;
- it is separately polled from the external PUSH decoder;
- it is not the onboard Level or Reset key path;
- sustained high causes the half-scale family-`0x01` refresh described above;
- the logical ZCL level is not changed by that refresh.

Not established:

- thermal protection;
- overload/current protection;
- derating request;
- power-stage controller status/fault;
- zero-cross indication;
- any other specific electrical source.

The implementation therefore uses the neutral name **PB4 auxiliary compatibility input** and preserves behavior without depending on a guessed purpose.

## Safety rationale

Ignoring PB4 would be a poor first implementation choice because its behavior reduces the physical output request while leaving the logical requested level unchanged. That shape is compatible with a protection/derating function even though the exact role is unproven.

The conservative interoperability behavior is therefore to preserve the stock PB4 response until evidence establishes that it is unnecessary.

## Independent implementation

```text
src/glsd301p_pb4_compat.h
src/glsd301p_pb4_compat.c
tests/test_glsd301p_pb4_compat.c
```

The module reports whether a half-scale refresh is required and computes `currentLevel >> 1`; it does not mutate Zigbee state itself.

The host tests cover:

- first ten consecutive high polls produce no action;
- the 11th high poll requests half-scale output;
- sustained high repeats the request;
- any low resets qualification;
- exact half-scale arithmetic;
- a golden UART vector for logical level 200;
- null input fails closed.

No physical role name is required for firmware integration.
