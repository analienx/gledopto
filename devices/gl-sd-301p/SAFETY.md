# GL-SD-301P runtime safety contract

This document defines deliberate safety behavior for the independently written
GL-SD-301P interoperability firmware.  It is **not** a claim that stock firmware
implements every rule below.  Where safety and exact stock parity conflict, the
independent firmware may intentionally be more conservative.

## Non-negotiable output invariants

1. **Boot locked OFF.**
   - UART/power-stage output starts not-ready after every reset.
   - The first allowed power-stage command is the confirmed OFF frame:
     `A5 5A 01 00 04 AA`.
   - Non-zero output is not permitted until application state has been restored
     and explicitly armed through `glsd301p_output_guard_arm()`.

2. **Logical OFF dominates every auxiliary path.**
   - A Zigbee/local logical OFF state always maps to the confirmed OFF frame.
   - PB4 qualification must never energize the load while logical state is OFF,
     even though the stock PB4 handler's physical/business purpose remains
     unknown.

3. **Invalid level state fails OFF.**
   - ZCL `currentLevel == 0xFF` is treated as unknown/invalid, never as a wire
     level.
   - `minimum_output == 0xFF` is rejected whenever minimum clamping is active.
   - OFF remains encodable regardless of corrupt level/minimum state.

4. **Runtime faults latch OFF.**
   - A latched safety fault makes the output guard not-ready and blocks non-zero
     output.
   - Clearing the fault does not resume the prior output.  The guard returns to
     the boot-locked state and must be explicitly re-armed after validated
     reinitialization.

5. **Unknown family-0x02 semantics are not part of the core runtime path.**
   - The low-level frame encoder retains the observed family for forensic/full-
     parity work, but normal On/Off/Level, PUSH and PB4 compatibility use only
     the solved family-0x01 path.
   - No production integration may invent semantic names or trigger family 0x02
     values until a required behavior is independently established.

## Zigbee architecture invariants

The eventual integrated build must enforce on the same reproducible SHA:

```c
ZB_ED_ROLE = 1
ZB_ROUTER_ROLE = 0
ZB_MAC_RX_ON_WHEN_IDLE = 1
PM_ENABLE = 0
```

and must preserve mains power-source reporting, endpoint 11, standard On/Off and
Level Control server behavior, direct-device binding, group-addressed On/Off and
Level reception, reporting, physical PUSH behavior, minimum-level policy and
reliable electrical OFF.

A production candidate must prove that it cannot route, accept children or act
as a transit hop.  Group reception is required even though the device is an End
Device.

## Power-stage integration order

The Telink integration should initialize in this order:

1. initialize GPIO/UART without emitting a non-zero frame;
2. initialize `glsd301p_output_guard_t` (locked/not-ready);
3. transmit one confirmed OFF frame through the guard;
4. restore application state: the boot state is fixed (logical OFF, level
   `0xFE`, minimum `0x02`); no output state is persisted, and the ZCL
   `StartUpOnOff`/`StartUpCurrentLevel` attributes are read-only and report
   exactly this behavior;
5. arm the guard only if restored level/minimum state is valid;
6. apply the desired restored logical state through the guard;
7. route subsequent Zigbee, PUSH and PB4 output requests through the same guard.

Watchdog/reset/brownout restart naturally returns to step 1, so stale RAM or a
partial application initialization cannot immediately re-energize the load.

## Network commissioning and recovery surface

Vendor-firmware OTA recovery is only deliverable while the device is on a
Zigbee network. The pinned Telink SDK never starts network steering by
itself, so the application owns it:

- **Factory-new steering.** A 1 s maintenance tick starts
  `bdb_networkSteerStart()` whenever BDB init has completed, the device is not
  joined, is factory new and no rejoin/steering operation is outstanding.
  Failed or refused attempts back off 5, 10, 20, ... s, capped at 300 s; an
  attempt with no terminal status is abandoned after 180 s. A device that
  still holds network state is never steered (rejoin owns it), so preserved
  network state is never overwritten.
- **Leave.** A successful own-Leave confirm (coordinator remove or the reset
  gesture) reboots cleanly after 2 s; the reboot re-runs the locked-OFF boot
  sequence and the factory-new device steers again.
- **Reset gesture.** Ten short PUSH activations, each at most 1000 ms after the
  previous one, request a network reset: `zb_factoryReset()` (broadcast Leave,
  drop network state) followed by a reboot within 10 s. On an already
  factory-new device the gesture only restarts steering immediately. A hold
  (dimming) or a longer pause restarts the count; each press still toggles the
  output normally.
- **OTA query.** After every join the OTA client queries the server every
  12 h. Images are only delivered when the server answers with one.

Re-joining a factory-new device requires Zigbee2MQTT permit-join. It then
re-interviews as `GL-SD-301P-ED` / `analienx`; install
`devices/gl-sd-301p/zigbee2mqtt/glsd301p_ed.js` as an external converter so
the device keeps `ota: true` and the recovery request stays deliverable.

None of these paths touches the output guard; a reboot always returns to the
boot-locked OFF state.

## PB4 policy

The solved compatibility behavior is retained while logically ON:

```text
qualified PB4 high -> family 0x01 value (currentLevel >> 1)
```

The logical Zigbee level is not modified.  Because the physical/business role of
PB4 is still unknown, the implementation must not expose a guessed user-facing
name such as "fault", "overtemperature" or "derating".  The only deliberate
safety deviation is that logical OFF overrides PB4 and remains electrically OFF.

## Verification gates

Host CI must cover at least:

- boot-locked ON request -> OFF;
- OFF with corrupt `currentLevel`/minimum -> OFF;
- `currentLevel=0xFF` ON request -> OFF;
- fault-latched ON/PB4 request -> OFF;
- clearing a fault alone cannot resume output;
- PB4 while logical OFF -> OFF;
- valid armed normal and PB4 paths preserve the confirmed six-byte vectors.

The future real Telink build must add same-SHA compile/link/runtime-contract gates
for the End-Device role, no-router capability, endpoint/clusters, group RX,
reporting and the output-guard call surface before `first_flashable_canary_allowed`
can become true.

## Deployment status

These rules improve the software safety envelope; they do **not** by themselves
authorize flashing any installed device.  Live deployment remains separately
gated by reproducible firmware, exact artifact attestation and explicit operator
authorization.
