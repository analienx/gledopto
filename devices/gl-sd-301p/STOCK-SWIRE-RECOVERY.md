# GL-SD-301P stock recovery over SWire — write-only route

**Status:** offline image conversion **validated by CI**, **not physically flashed/verified**.
**Scope:** restore GLEDOPTO stock firmware, **not** experimental firmware.
**Target:** GLEDOPTO GL-SD-301P, TLSR8258F512ET32, 512 KiB flash.
**Incident:** [#9](https://github.com/analienx/gledopto/issues/9) — custom GLSD-ED-007 interrupted Zigbee; physical PUSH still works.

## Fastest safe path (Windows, from this branch)

These commands run from the root of a checkout of
`recovery/glsd301p-stock-swire-20261108`.

1. Install Python 3 and prepare the **stock** image (no programmer or device required):

   ```cmd
   python tools\glsd301p_stock_swire_recovery.py prepare
   ```

   This reads the already-referenced source
   `devices/gl-sd-301p/vendor-firmware/originals/GL-SD-301P_V20851203_20251230.ota`
   and creates `local-recovery/glsd301p-stock-bank-a.bin` plus a JSON
   hash/range report. If the input hash, OTA/Telink structure, embedded CRC,
   identity or safety limits differ, it **stops without producing a flash command**.

2. For the supported *COM-port-SWire* programmer, install its Python dependency:

   ```cmd
   python -m pip install pyserial
   ```

3. With the dimmer **completely disconnected from 230 V** and a correctly
   established isolated low-voltage 3.3 V programming setup, run:

   ```cmd
   python tools\glsd301p_stock_swire_recovery.py flash --port COM3
   ```

   Replace COM3 with the real COM port. The tool checks the stock binary
   again, downloads a **commit- and Git-blob-pinned** copy of pvvx's
   `TLSR825xComFlasher.py`, prints the fixed `wf 0x00000` write command
   and requires the exact `WRITE_STOCK_BANK_A` confirmation.
   It will **never** issue `ea` (erase all), write arbitrary addresses or
   use a custom GLSD-ED image. The default `--activate-ms 0` does not pulse
   RTS; `--activate-ms 70` should only be used when a qualified RESET/RTS
   connection and power sequence have been established.

   For attended automation with independently verified electrical conditions:
   ```cmd
   python tools\glsd301p_stock_swire_recovery.py flash --port COM3 --confirm WRITE_STOCK_BANK_A
   ```

   Noninteractive confirmation is **not** electrical proof. Do not run
   unattended against an unqualified board.

4. A completed writer process means **write reported**, not readback verified.
   Disconnect all USB/debug and 3.3 V equipment, reassemble the mains dimmer
   professionally, and only then restore normal power. Confirm stock firmware
   boots, Zigbee2MQTT joins **and completes interview**, then check On/Off,
   Level and physical PUSH. Until then, report `PROGRAMMER_REPORTED_SUCCESS,
   READBACK_UNVERIFIED`, **not** `RECOVERED`.

## Hardware gate: do NOT guess the test-pad pinout

The previous generated photo labeling the three pads 3V3/GND/SWS was
**unverified** and must NOT be treated as wiring authority.

* On the disconnected/verified-de-energized PCB, identify SWS/PA7, true logic
  ground, supply and any RESET point using the MCU datasheet, copper tracing
  and continuity measurements by a qualified person. Label exact board pads
  only after verifying actual connectivity. Do not rely on relative pad order.
* SWire is **bidirectional**, even for a firmware **write**; the pvvx COM-port
  emulator still needs RX/status transactions. A physically TX-only lead is
  not compatible. A readback-unavailable operator is fine **provided** the
  programmer supports the SWire exchanges required by its protocol.
* Use **3.3 V logic**, current-limited and electrically isolated supply
  appropriate to the module. Do not inject a random 3.3 V rail into an
  unidentified node. Do **not** use a 5 V UART TX/RX or assume the USB
  adapter can power the complete dimmer. Confirm the board cannot backfeed
  mains or expose USB/computer to hazardous potential.
* pvvx warns that FTDI adapters and USB-COM adapters with RX-input LEDs can
  fail with this technique. CH340/CP2102 usability depends on actual 3.3 V
  logic levels, wiring, adapter characteristics and driver support.
* Do not connect the device to 230 V while the USB programmer or probes are
  attached. If isolation cannot be established, stop and use a qualified
  mains-equipment repair/electronics technician.

## Why bank A is the simplest recovery slot

Pinned Telink Zigbee SDK `d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e`,
`tl_zigbee_sdk/zigbee/ota/ota.c::mcuBootAddrGet()`:

1. Check the **4-byte "KNLT" startup flag** at `0x00008`.
2. If it matches the boot flag, select **bank A** at `0x00000`.
3. Only otherwise check `0x40008` and select bank B at `0x40000`.

This gives bank A precedence in the SDK's software boot selection logic.
The standard Telink OTA switching path invalidates the previous bank's
marker, so restoring a valid stock app to A is the smallest targeted repair,
rather than writing an entire 512 KiB image, deleting Zigbee NV, or forcing
bank-B marker changes. **This is source-level evidence, not a guarantee that
the real boot ROM, flash layout and programmer timing have been hardware
proven on this failed physical unit.**

| Region | Address | Recovery treatment |
|---|---|---|
| **Application bank A** | `0x00000–0x33FFF` | Write the extracted stock application from `0x00000` |
| Zigbee NV A | `0x34000+` | Never erase/write |
| Application bank B | `0x40000–0x73FFF` | Leave untouched (includes failed custom firmware) |
| Factory/MAC region | `0x76000+` | Never erase/write |
| Zigbee NV B | `0x7A000+` | Never erase/write |

The utility validates the application size **and the entire rounded-up
4 KiB sector erase span** before permitting any write. The pvvx `wf`
command performs sector erases within the image range. It does not write
a Zigbee OTA wrapper. It does not erase all flash.

## Exact source identity and toolchain

* Original **vendor** OTA: `GL-SD-301P_V20851203_20251230.ota`,
  208,946 bytes, SHA-256
  `16595a38ab9783d3afc4eb58ab4ec32625249bd569c1fa6dbaf468bddc76dd72`.
* Embedded Zigbee identity: manufacturer `0x124F`, image type `0x1416`,
  fileVersion `0x28013001`; distinct from the experimental `0x7F080001`.
* Original source provenance and licence limits:
  [vendor-firmware/MANIFEST.json](vendor-firmware/MANIFEST.json).
  Generated binaries stay in ignored `local-recovery/` — never publish a
  newly derived vendor binary as an independent open-source release.
* Programmer: [pvvx/TlsrComSwireWriter](https://github.com/pvvx/TlsrComSwireWriter),
  source commit `042613b1b47a1598856b0ff3e8a133f2918798a5`,
  script Git blob `44b13b908c9ea2b7619edf3a78ba0843ebfda214`,
  program `TLSR825xComFlasher.py`. The Python wrapper validates the exact
  blob before executing it. Alternatively point `--programmer PATH` to a
  downloaded script matching that hash.
* Pinned Telink software basis:
  [Telink SDK `ota.c`](https://github.com/telink-semi/telink_zigbee_sdk/blob/d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e/tl_zigbee_sdk/zigbee/ota/ota.c).
* Host-only safety CI:
  `python -m unittest discover -s tools/tests -p test_glsd301p_stock_swire_recovery.py -v`;
  and `.github/workflows/glsd301p-stock-recovery.yml`.

## If it doesn't boot or interview

Stop. Do not use `ea`, blindly overwrite `0x40000`, reset Zigbee NV or
try random pin combinations. Keep the write log and the generated manifest,
then verify the supply, wiring, erase/write status, startup-bank assumption
and physical startup with a qualified programmer/technician. Because the
available setup cannot dump flash, **the write cannot establish what is
already present on bank B or in NV**. Retained incompatible network state can
still prevent interview even after the original application is restored:
restore of the stock app and successful network recovery are separate
acceptance conditions.

**Experimental firmware remains DO NOT FLASH under issue #9.**
