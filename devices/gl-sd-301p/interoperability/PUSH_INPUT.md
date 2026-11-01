# GL-SD-301P PC2 PUSH input interoperability

Status: **HIGH-confidence functional specification**

This document records the external behavior required to reproduce the GL-SD-301P physical PUSH input in the independently written End Device firmware.

## Identification

The stock application polls TLSR8258 `GPIO_PC2` (`0x0204`) every 1 ms and runs a pulse-presence state machine. That state machine:

- toggles the standard Zigbee On/Off state after a completed short activation;
- performs repeated standard Zigbee Level updates during a sustained activation;
- reverses the dim direction after a sustained activation ends.

This behavior independently matches the published GL-SD-301P user instruction for the external **PUSH** terminal: short press = On/Off, long press = brightness adjustment, with the next long press after release operating in the opposite direction.

**Classification:** `PC2 = external PUSH-sense path` — HIGH confidence. A PCB continuity trace would corroborate the physical net but is not needed to implement the observed functional behavior.

## Why PC2 must not be treated as a normal DC button

The stock application does not interpret `PC2 == low` as a complete press. It first validates a repeating low/high waveform:

1. at least 3 consecutive low samples;
2. then at least 6 high samples, while a high gap of 51 samples times out the candidate;
3. then at least 3 further low samples;
4. only then is the PUSH activation considered valid.

With a 1 ms polling interval this is consistent with a mains-derived/opto-isolated PUSH sense signal rather than a simple logic-level switch contact. The independent firmware should therefore preserve pulse-presence decoding at PC2 instead of replacing it with generic GPIO debounce.

## Functional states

The independently written decoder uses semantic states equivalent to the observed behavior:

```text
IDLE
  |  >=3 low samples
  v
FIRST_LOW
  |  >=6 high samples, but <51
  v
INTERPULSE_GAP
  |  >=3 low samples before high timeout
  v
ACTIVE
```

A high gap of 51 polling ticks before activation validation aborts the candidate and returns to `IDLE`.

## Short activation

Once `ACTIVE`, disappearance of the valid waveform for 51 consecutive high samples is interpreted as a short PUSH completion.

The stock application then invokes its standard On/Off state updater:

```text
currently ON  -> OFF
currently OFF -> ON
```

The resulting power-stage update is therefore the already-confirmed normal family-`0x01` output frame:

```text
OFF: A5 5A 01 00 04 AA
ON : A5 5A 01 LL 04 AA
```

No family-`0x02` command is required for the ordinary physical PUSH toggle.

## Long activation

While the valid PUSH waveform remains present, the stock application counts **low PC2 samples** rather than raw wall-clock milliseconds.

- First local level step: after 1001 accumulated low samples while ACTIVE.
- Repeat local level step: every further 301 accumulated low samples while long-active.
- High gaps shorter than 51 samples do not end the activation and do not clear the accumulated long-hold counter.

This is important: for an AC-derived pulse waveform, accumulated-low-sample time and real elapsed time need not be identical.

## Local level step policy

The current Zigbee Level value is read and the step size is selected as:

```text
currentLevel <= 150 -> step = 10
currentLevel >  150 -> step = 25
```

Direction is persistent across one long activation and reverses when that long activation is released.

Initial zero-initialized direction is downward.

### Upward step

```text
newLevel = min(254, currentLevel + step)
```

### Downward step

The observed stock arithmetic is deliberately preserved, including its edge behavior:

```text
if currentLevel <= step:
    newLevel = 2
else:
    newLevel = currentLevel - step
```

Therefore `currentLevel=11` with `step=10` produces `1`; this is not normalized away in the compatibility layer.

The level-step routine is gated by the standard On/Off state. A long PUSH while logically OFF does not directly run the local level adjustment routine.

## Long-release behavior

When the long-active waveform disappears for 51 consecutive high samples:

- no On/Off toggle is emitted;
- the persistent dim direction is reversed;
- the decoder returns to IDLE.

Thus the next long PUSH operates in the opposite direction, matching the product manual.

## Independent implementation

Implementation files:

```text
src/glsd301p_push_input.h
src/glsd301p_push_input.c
tests/test_glsd301p_push_input.c
```

The decoder emits semantic events only:

```text
GLSD301P_PUSH_EVENT_TOGGLE
GLSD301P_PUSH_EVENT_LEVEL_STEP
```

It does not itself send UART frames or mutate Zigbee attributes. The application layer remains responsible for applying those events to the standard On/Off / Level state, which then uses the normal family-`0x01` power-stage output path.

## Confidence / remaining work

Confirmed from software behavior:

- PC2 pin identity;
- 1 ms polling cadence;
- pulse qualification windows;
- short activation -> standard On/Off toggle;
- long activation -> standard Level changes;
- step sizes and limits/arithmetic;
- direction reversal after long release.

HIGH-confidence inference:

- PC2 is the external PUSH-terminal sense path, based on exact agreement with the manufacturer's documented PUSH behavior and the pulse-oriented electrical signature.

Still optional to corroborate physically:

- terminal-to-PC2 PCB continuity;
- exact optocoupler/sense topology and waveform duty cycle at 50/60 Hz.

Neither physical detail is required for the first independent firmware because it will read the same PC2 signal and preserve the stock pulse decoder behavior.
