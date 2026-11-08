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

## CH340G TX-only path — recommended software for photographed adapter

Our new **stock-only** Python sender is
[glsd301p_stock_txonly.py](../../tools/glsd301p_stock_txonly.py).
It is an offline-tested adaptation of [pvvx TX-only USB-COM Writer v0.5](https://github.com/pvvx/pvvx.github.io/blob/master/ATC_MiThermometer/USBCOMFlashTx.html).
Unlike the old bidirectional Python `flash` command, **it never reads the
target** and only needs the adapter's **TXD-to-SWS signal, common GND
and separately qualified isolated 3.3 V supply**. It does not require RXD,
RTS, a Telink programmer, a hardware flash dump or browser Web Serial.
But no echo, flash status, CRC or memory readback is possible.

**Simplest Windows workflow, using only the GitHub Actions ZIP:**

1. Download the latest successful
   [stock recovery Actions artifact](https://github.com/analienx/gledopto/actions/workflows/glsd301p-stock-recovery.yml)
   and extract its contents into one directory. Current workflow includes
   the `glsd301p_stock_txonly.py` sender, its two Python support
   modules, all three verified BINs, a manifest, hashes, instructions and
   `requirements.txt`. No Git checkout and **no local BIN generation**.
2. From that directory, in Windows Terminal / Command Prompt:

   ```cmd
   python -m pip install -r requirements.txt
   python glsd301p_stock_txonly.py plan --bundle-dir .
   python glsd301p_stock_txonly.py ports
   ```

   The `plan` command checks the archived stock **SHA-256**, Telink
   CRC/version and sector write bounds **before any USB work**. Identify the
   actual CH340G COM port from `ports`.
3. **Only after verifying all PCB pads, low-voltage isolation, supply,
   TXD logic voltage, and mechanically stable contacts** (with the mains
   fully disconnected), send the first, inactive-marker image:

   ```cmd
   python glsd301p_stock_txonly.py stage --bundle-dir . --port COM3
   ```

   Confirm by typing `TX_ONLY_STAGE`. This erases/writes only
   application bank A, sector by sector, with the boot marker still
   `FF 4E 4C 54` (inactive). The script writes an explicit
   `*-txonly-stage-sent.json` **transmission-only receipt**.
4. **Only after the entire stage has been transmitted without host-side
   errors and all wiring/power conditions remain valid**, send the one-byte
   boot-marker activation in a *separate* command:

   ```cmd
   python glsd301p_stock_txonly.py activate --bundle-dir . --port COM3
   ```

   Confirm by typing `TX_ONLY_ACTIVATE_UNVERIFIED`. The
   command requires the stage receipt from the **same COM port and same
   staged SHA-256**, sends only `0x4B` at `0x00008`,
   **without sector erase**, and refuses to repeat automatically.

   **Crucial limitation:** "sent" means UART transmitted data; the MCU
   gives **no response whatsoever**. The stage may have been partially
   missed despite successful serial writes, in which case enabling bank A
   could still fail to boot. The two-phase process reduces one interruption
   window but **cannot prove image validity without readback**.
5. Disconnect USB and independent 3.3 V supply; reassemble and restore
   mains safely. Verify **Zigbee2MQTT complete interview** plus OnOff, Level
   and physical PUSH before declaring the unit recovered. Do not repeat
   the writer blindly on failure.

**Activation caveat for your adapter:** pvvx v0.5 repeatedly transmits
a CPU-stop command during a 3-second SWire activation window. A running
custom firmware may reconfigure SWS, so a genuine **isolated low-voltage
reset/power cycle during activation** may be necessary. The photographed
CH340G has no verified accessible RTS/reset pin. Our script does **not**
pretend to toggle one. Without a validated power/reset plan, even
a correct TXD connection may fail to enter programming mode; stop rather
than connecting 230 V or improvising reset wiring.

### Can the LaskaKit mini-hooks be used on the PCB?

**Yes, on a protruding conductor or plated through-hole where the hook
mechanically grabs and survives a gentle disconnected-board pull test.**
The hook must not pull loose, slide into a neighbouring pad, short traces
or rely on being held by hand over a multi-minute flash.

**Not reliably on a flat, isolated gold/solder test pad.** User reports:
- A [small-board repair discussion](https://www.reddit.com/r/AskElectronics/comments/1869q3x/)
  reports success using a test hook on a board **with a drilled hole**,
  but says it cannot attach to a small flat pad; experienced respondents
  recommend spring-loaded pogo probes/fixtures instead.
- A [probe/grabber discussion](https://www.reddit.com/r/AskElectronics/comments/zwezlj/)
  explains that hook probes still require exposed **legs to grab**, whereas
  flat pads need dedicated PCB contacts or a test fixture.

**For the three small Gledopto PCB points shown earlier, do not assume
your hook can grip the bare circular pad.** If a plated hole or accessible
connected component lead exists, it may work, but **we have not traced
and electrically identified those alternative contacts**. A pogo-pin
fixture with a clamped PCB, or professionally soldered temporary fine
wires, is a more reliable option. Even if using only two signal clips
(GND and SWS), safe 3.3 V supply still has to be established.

**Do not attempt with only the photographed supplies if you lack a
multimeter and a proven isolated low-voltage power source.** Previous
photo pad assignments were merely visual guesses, not continuity
verification. The CH340G selector alone does not prove TXD voltage.

---

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
- The [pvvx browser TX-only USB-COM Flash Writer](https://pvvx.github.io/ATC_MiThermometer/USBCOMFlashTx.html)
  supports **TX→SWS** and inspired our new standalone
  `glsd301p_stock_txonly.py` sender. The unmodified browser writer
  always erases sectors from address zero and **cannot safely perform
  our one-byte no-erase activation**: use the new two-phase repo script,
  not the browser "Write" button for stage+activation.
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
* The **original Telink SWire protocol is bidirectional**, but pvvx's
  **TX-only subset** explicitly imitates only the write/control
  sequences, without acknowledgements. Our new `glsd301p_stock_txonly.py`
  is based on this approach and uses **TXD only**. The separate older
  `glsd301p_stock_swire_recovery.py flash` remains bidirectional and
  must not be confused with TX-only.
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
