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
4. ED-009: load a validated application startup record. Missing or invalid
   records default to logical OFF, level `0xFE`, minimum `0x02`. Standard
   writable `StartUpOnOff` selects OFF/ON/toggle/previous; read-only
   `StartUpCurrentLevel=0xFF` restores committed brightness. This load never
   sends an output frame or changes network storage;
5. arm the guard only if restored level/minimum state is valid;
6. apply the desired restored logical state through the guard;
7. route subsequent Zigbee, PUSH and PB4 output requests through the same guard.

Watchdog/reset/brownout restart naturally returns to step 1. Configured ON or
previous-ON may energize only after the complete guard sequence succeeds.
The policy applies to all boots, including watchdog resets; it does not claim
to distinguish a mains cut from a software reset. A faulted/unready runtime
cannot persist a fault-forced OFF over the last normal user state.

ED-009 stores policy and stable user state in application item `0xE6`, using
the existing SDK NV journal, an application version/CRC and write readback.
The stack-NV epoch item `0xE5` and its migration value are unchanged. Factory
reset still erases the application module along with the existing stack wipe.
Normal samples require idle Level, no pending/inflight UART and ready output.
The 1 s maintenance event waits for two matching safe samples at least 1 s
apart: usually 1–2 s after a state/settings change. A power cut before that
restores the prior committed snapshot. Write failures retain that snapshot
and retry at most once per 30 s. No write occurs for unchanged idle state.

## Network commissioning and recovery surface

Vendor-firmware OTA recovery is only deliverable while the device is on a
Zigbee network. The pinned Telink SDK never starts network steering by
itself, so the application owns it:

- **Factory-new steering.** A 1 s maintenance tick starts
  `bdb_networkSteerStart()` whenever BDB init has completed, the device is not
  joined, is factory new and no rejoin/steering operation is outstanding.
  Failed or refused attempts back off 5, 10, 20, ... s, capped at 300 s; an
  attempt with no terminal status is abandoned after 180 s. An accepted
  attempt ends only on a terminal commissioning status or that watchdog:
  BDB already reports "joined" while the Trust Center link-key exchange is
  running, and a failed exchange makes the stack leave and report
  `TCLK_EX_FAILURE`, which backs off like any other failure. A device that
  still holds network state is never steered (rejoin owns it), so preserved
  network state is never overwritten.
- **Stranded network state.** A device that is neither joined nor factory
  new, with no rejoin or steering outstanding, starts a rejoin attempt every
  60 s. An accepted rejoin that still has not joined after 15 min is
  abandoned, and that kick starts a fresh one, so a stalled stack backoff
  cannot keep the device off the network.
- **Rejoin states on a factory-new device.** A rejoin attempt (active or
  waiting to retry) on a device whose network state is gone can never
  succeed, so it hands over to steering.
- **Leave.** Only the reset gesture's Leave reboots. Any other own Leave
  needs no special handling, whether it is BDB's own after a failed
  link-key exchange, a coordinator removal (Zigbee2MQTT "remove"), or a
  Leave with rejoin. A factory-new device steers again (with its RAM
  backoff intact), and one that kept its network state rejoins.
  **Consequence:** like most Zigbee devices, a removed unit re-joins the
  next time permit-join is opened. To keep it out, power it off or add its
  IEEE address to the Zigbee2MQTT `blocklist`. Three review rounds showed
  that every "stay quiet after removal" variant also created a path where
  the device never comes back by itself, which would forfeit the vendor
  OTA recovery, so there is deliberately none.
- **Reset gesture.** Ten short PUSH activations, each at most 1000 ms after the
  previous one, request a network reset. No rejoin is started during the
  reboot countdown. Joined: `zb_factoryReset()`
  (broadcast Leave) and a reboot within 10 s (2 s after the Leave confirm).
  Not joined: a reboot after 1 s. Before that reboot, if the stack
  still reports network state, the SDK's `nv_resetToFactoryNew()` clears it
  (the outgoing NWK frame counter is kept), so the device always boots
  factory new and steers. On an already factory-new device the
  gesture only restarts steering immediately. A hold (dimming) or a longer
  pause restarts the count; each press still toggles the output normally.
- **OTA query.** After every join the OTA client queries the server every
  12 h; a failed or aborted transfer re-arms that query (deferred to the
  1 s tick, because the abort can come from inside the OTA timer itself).
  Zigbee2MQTT only offers an image during an explicit request, so **an
  aborted recovery transfer is retried by re-sending the same request**; the
  device never retries a recovery by itself. If the SDK refuses to switch to
  a fully received image (invalid image or failed boot-flag write), the
  device reboots onto its current image so no stale RAM transfer state
  survives. The pinned SDK could also stay in "download in progress" for
  good (two early returns in the Query Next Image Response handler, e.g. a
  failed buffer allocation) and refuse every later Image Notify, which would
  block the recovery. Pinned SDK patch P7 completes the upgrade as ABORT on
  both paths. No timing heuristic aborts transfers, so slow or
  server-paced recovery downloads are never cut off by the application.
- **Observability.** Basic:0xFF10 byte 47 reports the connectivity state
  (idle / rejoin active / rejoin retry pending / steering / steering
  backoff) and the steering-failure count (saturating
  at 15).

Re-joining a factory-new device requires Zigbee2MQTT permit-join. It then
re-interviews as `GL-SD-301P-ED` / `analienx`; install
`devices/gl-sd-301p/zigbee2mqtt/glsd301p_ed.js` as an external converter for
normal control and OTA checks. The vendor recovery itself is an explicit-URL
request on the `ota_update/update/downgrade` topic (Zigbee2MQTT >= 2.9.0),
which does not depend on the device definition; Zigbee2MQTT does not
hash-check such images, so the served bytes must be verified against the
manifest SHA-512 first.

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
