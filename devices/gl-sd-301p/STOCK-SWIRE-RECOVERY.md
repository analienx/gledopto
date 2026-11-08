# GL-SD-301P stock recovery over SWire — write-only route

**Status:** offline image conversion **validated by CI**, **not physically flashed/verified**.
**Scope:** restore GLEDOPTO stock firmware, **not** experimental firmware.
**Target:** GLEDOPTO GL-SD-301P, TLSR8258F512ET32, 512 KiB flash.
**Incident:** [#9](https://github.com/analienx/gledopto/issues/9) — custom GLSD-ED-007 interrupted Zigbee; physical PUSH still works.

## Prebuilt stock firmware BINs — GitHub Actions (no local build)

**Already generated and independently checked on GitHub's hosted runner.**
[Successful workflow run 42523757482](https://github.com/analienx/gledopto/actions/runs/42523757482)
for source commit `b148ecaf2b03321ccc60df2de0237e4bf46c5b84`
published the artifact
**`glsd301p-stock-swire-recovery-b148ecaf2b03321ccc60df2de0237e4bf46c5b84`**,
artifact ID `13634591809`. Find it under the run's **Artifacts** section
and download the ZIP while signed into GitHub. It expires **2026-11-22**;
subsequent branch runs create their own fresh artifacts.

| Artifact file | Bytes | SHA-256 |
|---|---:|---|
| `glsd301p-stock-bank-a.bin` | 208,884 | `7ba122f1ebc9f6ec419d9ea6b27676d55c2a2aad864a03e90b896546248c4b48` |
| `glsd301p-stock-bank-a-stage.bin` | 208,884 | `b2645f96869f4fb50f64749c14be281696200279cc3c8bca837836d95429d637` |
| `glsd301p-stock-bank-a-activate.bin` | 1 | `86be9a55762d316a3026c2836d044f5fc76e34da10e1b45feee5f18be7edb177` |
| `glsd301p-stock-bank-a.json` | 1,061 | See `SHA256SUMS` |
| `SHA256SUMS` | 384 | Checksum manifest |

**The plain bank-A BIN is the verified original stock application.** For the
two-phase SWire process, use **the stage BIN first** (inactive boot flag), then
**only the one-byte activation BIN**, as described below. Do not flash the
Zigbee `.ota` package raw, and do not directly flash the plain bank-A BIN
if following the protected two-phase procedure. The BINs are **prepared but
not proven on physical hardware**.

CI generates these directly from the original vendor OTA with a pinned hash.
No local PC is needed to *generate* them; physical programming still requires
a suitable SWire adapter connected to the completely mains-isolated device.

## Fastest safe path (Windows, from this branch)


These commands run from the root of a checkout of
`recovery/glsd301p-stock-swire-20261108`.

1. Install Python 3 and prepare the **stock** image (no programmer or device required):

   ```cmd
   python tools\glsd301p_stock_swire_recovery.py prepare
   ```

   This reads the already-referenced source
   `devices/gl-sd-301p/vendor-firmware/originals/GL-SD-301P_V20851203_20251230.ota`
   and creates `local-recovery/glsd301p-stock-bank-a.bin` (canonical
   raw stock app), `...-stage.bin` (boot marker inactive), `...-activate.bin`
   (one byte), plus a JSON hash/range report. If the input hash, OTA/Telink structure, embedded CRC,
   identity or safety limits differ, it **stops without producing a flash command**.

2. For the supported *COM-port-SWire* programmer, install its Python dependency:

   ```cmd
   python -m pip install pyserial
   ```

   Identify the adapter's **actual** COM port (read-only listing, no writes):

   ```cmd
   python tools\glsd301p_stock_swire_recovery.py ports
   ```

   Select the port matching the **SWire adapter**, not the Zigbee coordinator.

3. With the dimmer **completely disconnected from 230 V** and a correctly
   established isolated low-voltage 3.3 V programming setup, run:

   ```cmd
   python tools\glsd301p_stock_swire_recovery.py flash --port COM3
   ```

   Replace COM3 with the real COM port. The tool checks the stock binary
   again, downloads a **commit- and Git-blob-pinned** copy of pvvx's
   `TLSR825xComFlasher.py`, displays both fixed write operations, and
   requires the exact `WRITE_STOCK_BANK_A` confirmation. **Phase 1:**
   `wf 0x00000 ...-stage.bin` writes the entire app with the startup marker
   deliberately invalid (`FF 4E 4C 54`). **Phase 2 (only if phase 1 succeeds):**
   `we 0x00008 ...-activate.bin` programs a single `4B` byte WITHOUT
   erasing the sector. This completes `4B 4E 4C 54` and enables bank A.
   If phase 1 fails, phase 2 is never attempted.
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

## Available owner hardware (2026-11-08) — compatibility triage

Photographs show an Allegro **CH340G USB-to-TTL UART adapter**, whose
5-pin breakout is labeled `5V / 3V3 / TXD / RXD / GND`, with a
3.3 V/5 V selector, and LaskaKit female-Dupont-to-mini-hook cables.

**These two products alone do not make the physical SWire connection
qualified or safe.** Specifically:

- The CH340G is **not an SWire programmer natively**. The current repo
  `flash` subcommand uses pvvx's `TLSR825xComFlasher.py` to emulate the
  **bidirectional** SWire protocol over COM; it requires a validated
  TXD/SWS + RXD return/echo wiring topology and appropriate signal voltage.
  Do not connect TXD/RXD to board pads arbitrarily.
- An alternative [pvvx browser TX-only USB-COM Flash Writer](https://pvvx.github.io/ATC_MiThermometer/USBCOMFlashTx.html)
  explicitly supports **TX→SWS**, GND and isolated 3.3 V supply,
  with manual power-cycle/activation if RTS is absent. This is a
  **different programmer path**, and its ordinary write feature is not
  currently integrated with or verified for the repo's **two-phase
  stage + erase-free one-byte activation** protocol. Do not assume the
  existing staged BIN can be uploaded to the browser writer and complete
  recovery. The full plain stock BIN would sacrifice our staged-marker
  interruption protection; do not use blindly.
- The 3.3 V selector/rail **does not prove** TXD is 3.3 V or that the
  module has enough current to power all circuitry on this dimmer.
  Verify TXD idle voltage, 3.3 V voltage/current budget and ground
  isolation with proper instruments. Never apply 5 V directly to
  the Telink's logic pins or use the unknown mains dimmer supply.
- Mini-hook clips are for **exposed leads**, not guaranteed electrical
  contact on unpopulated flat PCB test pads; use a fixture/pogo contact
  or correctly qualified soldered lead after pads are verified.
- At minimum, obtain/borrow a **multimeter** for continuity and voltage
  checks and a reliable low-voltage **isolated, current-limited 3.3 V
  supply** if the verified module rail cannot safely power the board.
  Confirm exact SWS/PA7, GND and supply traces using a datasheet and
  measurements; the earlier generated pin labels are **not verified**.
- The user need not buy a dedicated Telink programmer **if** the
  CH340G protocol/wiring is validated, but the current hardware inventory
  does **not** authorize connecting USB or attempting a write.

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
| **Application bank A** | `0x00000–0x33FFF` | Stage the stock app with invalid marker at `0x00000`, then atomically enable with one byte at `0x00008` |
| Zigbee NV A | `0x34000+` | Never erase/write |
| Application bank B | `0x40000–0x73FFF` | Leave untouched (includes failed custom firmware) |
| Factory/MAC region | `0x76000+` | Never erase/write |
| Zigbee NV B | `0x7A000+` | Never erase/write |

The utility validates the application size **and the entire rounded-up
4 KiB sector erase span** before permitting any write. The pvvx `wf`
stage command performs sector erases within the image range; `we` activation
at `0x00008` does **not** erase. Staging with `0xFF` at byte 8 makes the
bank-A startup flag invalid until the final one-byte activation. This avoids
selecting an incomplete A image after an interrupted first phase. No full
flash erase and no Zigbee OTA wrapper write occurs. Without physical
readback or a power-loss test, this is **not a guarantee of atomic recovery
on the actual hardware**.

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
