# GL-SD-301P firmware extraction — Supervisor research and Executor transfer

Status: **SUPERVISOR DESIGN / EXECUTOR TRANSFER**  
Date: **2026-09-05**  
Target repository: `analienx/gledopto`  
Control issue: `#1`  
Target device: `LivingRoomCircleLightDimmer`  
IEEE: `0xa4c13850cfcdb3a4`  
Model: `GL-SD-301P` / `GLEDOPTO`

This document supersedes only the earlier interpretation that *all useful work must freeze until a spare is available*. It does **not** supersede any no-flash, no-destructive-production, secret-handling, evidence, or rollback invariant.

The Supervisor owns architecture, research synthesis, firmware/host design, safety gates, acceptance criteria and code supplied for execution. The Executor owns environment access, bounded read-only verification, executing explicitly authorized packages, evidence collection, and physical actions when explicitly authorized. The Executor must not invent firmware, protocol values, flash operations, or live recovery steps.

---

## 1. Executive decision

There are now two valid extraction tracks, with a strict priority order.

### Track A — sacrificial GL-SD-301P + wired SWire/SWS full-flash read

**This is the preferred method whenever a sacrificial/spare target is available.**

Why:

1. It retrieves the exact production-family 512 KiB flash image instead of inferring compatibility from another GLEDOPTO model.
2. It reveals the exact flash chip/device geometry, both application banks, both NV areas, MAC/factory/config areas, and installed application.
3. It allows static reverse engineering of the actual GL-SD-301P firmware before any custom OTA is sent to the production unit.
4. It gives the best chance to prove whether the Telink directly controls the phase-cut stage or talks to a secondary MCU.
5. It gives a hardware recovery path before any production custom OTA experiment.

Reference read command used by the current fallback tooling for a 512 KiB device:

```text
Telink_Tools.py -p <COM> read_flash 0 524288 dump.bin
```

Do **not** treat a serial-flash utility as inherently safe for an energized mains dimmer. The sacrificial unit must be removed from mains and powered/debugged only through a correctly identified low-voltage interface. Do not guess pads or voltage. Physical mapping comes first.

### Track B — temporary custom OTA “dump stager”, then wireless exfiltration

This is now **technically plausible with materially stronger evidence than in the original handover**, but it is **not authorized on the production unit yet**.

The stager will eventually:

1. install by the stock Zigbee OTA client into the inactive application bank;
2. boot using the existing Zigbee NV if compatible;
3. leave all factory/MAC/network NV regions untouched;
4. expose a tiny private read protocol;
5. read only the old application bank;
6. transfer the old application to the host in restartable verified chunks;
7. reconstruct the one stock boot-marker byte intentionally invalidated by standard Telink OTA;
8. validate the reconstructed image with the same Telink CRC algorithm;
9. retain a transactional rollback path proven on a canary before production.

**Production Track B remains blocked until the target-specific acceptance and rollback gates in this document pass on a sacrificial unit.**

---

## 2. Ground-truth target fingerprint

Never identify firmware compatibility from `manufacturerCode + imageType + fileVersion` alone. GLEDOPTO has reused OTA identities across unrelated products.

Keep the full tuple together:

```text
friendly_name      LivingRoomCircleLightDimmer
IEEE               0xa4c13850cfcdb3a4
modelId             GL-SD-301P
manufacturerName    GLEDOPTO
swBuildId            20651203
dateCode             20240704
hwVersion            2
applicationVersion   1
stackVersion         2
manufacturerCode     0x124F / 4687
imageType            0x1416 / 5142
fileVersion          0x26013001
endpoint             11 / 0x0B
role                 Router (stock firmware behavior)
```

EP11 stock clusters:

```text
in:
  genBasic
  genIdentify
  genGroups
  genScenes
  genOnOff
  genLevelCtrl
  lightingColorCtrl
  touchlink
out:
  genOta
```

Important state to preserve/restore after any future migration or experiment:

```text
existing direct binds:
  genOnOff    -> 0xfdb1122d004b1200 EP1
  genLevelCtrl-> 0xfdb1122d004b1200 EP1

configured reporting:
  onOff       0 / 65000 / 1
  currentLevel 5 / 65000 / 1

group:
  110
```

Final custom firmware objective remains separate from extraction: mains-powered, always-listening, **non-routing ZED**, preserving direct/group OnOff and Level Control behavior. The temporary extraction stager does not need to satisfy the final-role requirement; reliability and recoverability take priority during extraction.

---

## 3. New research results that materially change the feasibility assessment

### 3.1 Standard Telink multi-address OTA really is a two-bank ping-pong design

Pinned upstream source:

```text
repo: telink-semi/telink_zigbee_sdk
ref: 09fa2c3483b3aa2f0a6f2e2cc7e267cd6f1f9277
path: tl_zigbee_sdk/stack/zigbee/ota/ota.c
path: tl_zigbee_sdk/stack/zigbee/ota/ota.h
path: tl_zigbee_sdk/proj/common/utility.c
```

Upstream behavior:

- startup flag `TL_START_UP_FLAG_WHOLE = 0x544C4E4B`;
- application can boot from `0x00000` or `FLASH_ADDR_OF_OTA_IMAGE`;
- on the 512 KiB map, OTA bank is `0x40000`;
- `mcuBootAddrGet()` actively reads both possible bank flags;
- `ota_newImageValid()` reads the candidate bank and validates size/start marker/CRC;
- when an OTA image is downloaded, data are written to the bank opposite the current boot bank;
- after verification, `ota_mcuReboot()` activates the new bank and invalidates the old bank’s first marker byte.

This resolves an original research question: **code running from one bank can read the opposite bank** in the standard Telink architecture. The vendor SDK itself does it.

### 3.2 Official 512 KiB Telink Zigbee flash map

Official Telink Zigbee SDK documentation gives the multi-address map:

```text
0x00000  application bank A
0x34000  NV_1
0x40000  application bank B / OTA image
0x76000  MAC address
0x77000  F_Cfg_Info
0x78000  U_Cfg_Info
0x7A000  NV_2
0x80000  end of 512 KiB flash
```

Consequences:

- normal application/OTA image must be below `0x34000` (208 KiB);
- both network-NV regions are outside the two application images;
- MAC/factory/config data must never be used as stager scratch space;
- a temporary stager can reserve its own last 8 KiB inside its **own application slot** if its linked image ends below `+0x32000`.

Target applicability is still a gate: the GL-SD-301P must be proven to be a 512 KiB TLSR8258-family implementation before production assumptions rely on this map.

### 3.3 Strong TLSR8258/B85 evidence, but exact silicon/flash remains a target gate

Current reverse-engineering evidence strongly favors the TLSR8258/B85 family over TLSR8278:

- QFN32 physical/package evidence is incompatible with the common TLSR8278 package hypothesis;
- TC32 startup disassembly matches the B85/8258-generation startup structure and lacks the later 8278 efuse-delay sequence;
- historical GLEDOPTO firmware in the same OTA identity family has a Telink/B85-compatible payload shape.

Do **not** convert this into `MCU_EXACT=PROVEN` until a target/spare identifies the exact part and flash size.

### 3.4 Historical same-family GLEDOPTO OTA is plain Telink OTA, not an AES/ECDSA wrapper

Pinned public artifact:

```text
repo: Koenkk/zigbee-OTA
commit: f4260fe4dfa47561f607707ad38abb829eb95a83
file: images/Gledopto/GL-C-009P(MINI)_20451203_20240227.ota
blob: 09c1e5ad3874a422cbe1e87e351e6478d4e1272e
index SHA512:
868e671255db3c753a282125cdc4c333771cf1032423968b1412f9760cb105f97874261ab56559dc1cf54c0742eec062ccf9b8a75b4ef5e85b1485e8d5fd1aac
```

Parsed OTA header:

```text
magic             0x0BEEF11E
headerVersion     0x0100
headerLength      56
fieldControl      0
manufacturerCode  0x124F
imageType         0x1416
fileVersion       0x24013001
stackVersion      0x0002
headerString      "Telink OTA Sample Usage"
totalImageSize    212738 / 0x33F02
```

First sub-element immediately after the 56-byte OTA header:

```text
tag     0x0000  = ordinary Upgrade Image
length  0x33EC4
```

It is **not** Telink’s manufacturer-specific AES element `0xF000` and there are no Zigbee signature/certificate elements in this file. Its payload contains the expected Telink image validity pattern, including `0x5D 0x02` and the `0x544C4E4B` startup marker.

This is strong evidence that GLEDOPTO shipped at least one 2024 image in the same `0x124F / 0x1416` firmware lineage using the ordinary plain Telink OTA path.

It is **not proof** that the exact GL-SD-301P stock build `20651203 / 0x26013001` accepts an arbitrary unsigned custom image. GLEDOPTO can customize the OTA callback, require an exact hardware policy, or enable Telink’s `OTA_IMAGE_UNENCRYPTION_REJECT()` on another product/build. This remains a target/canary gate.

### 3.5 Generic Telink client validation is marker/size/CRC based

The pinned Telink client:

- accepts element tag `0x0000` (plain image) or `0xF000` (AES image);
- rejects `0x0000` only if `g_otaEncryptionNeeded` was explicitly enabled;
- checks manufacturer and image type in Query Next Image Response;
- rejects the **same** file version; a lower version is only additionally blocked when the WWAH downgrade control is enabled;
- checks image size against the slot maximum;
- computes Telink `xcrc32` from initial value `0xFFFFFFFF`;
- validates an embedded CRC at the end of the Telink application image.

Telink `xcrc32` is the standard reflected CRC-32 table algorithm using polynomial table beginning `0x77073096`; unlike many high-level CRC helpers, the SDK function does **not** perform a final XOR after processing.

This is enough to define our offline validator exactly; it is not enough to declare the GLEDOPTO client unmodified.

---

## 4. Critical boot-marker/rollback correction

This is the most important correction to the original wireless plan.

At relative application offset `+0x08`, a valid Telink image contains the first byte of startup marker `0x544C4E4B`, i.e. byte `0x4B` at offset 8.

The generic OTA process intentionally does this:

1. while downloading the inactive image, convert the image’s marker byte from `0x4B` to `0xFF` before storing it;
2. after complete CRC verification, write `0x4B` to the new bank to activate it;
3. write `0x00` to the old bank’s marker byte to invalidate the old application;
4. reset.

Therefore, after the stager boots, the old firmware is expected to be intact **except its old-bank marker byte may be `0x00` instead of original `0x4B`** under the generic implementation.

### Why a simple rollback byte write is impossible

NOR flash programming can clear bits from `1 -> 0` without erase. It cannot turn `0 -> 1`. Restoring `0x00 -> 0x4B` therefore requires erasing and rewriting the 4 KiB sector that contains the old bank’s first sector.

A procedure that says “write the old flag back to `0x4B`” without an erase/rewrite transaction is physically invalid.

### Consequence for production authorization

No production stager is authorized until rollback is proven on a sacrificial unit with power-loss injection. Recovery must be designed as a transaction, not a flag poke.

---

## 5. Proposed transactional rollback architecture

This is the design target for the Supervisor implementation. It is **not currently an Executor command**.

### 5.1 Stager-owned scratch sectors

Reserve exactly two sectors at the end of the stager’s own app slot:

```text
STAGER_BACKUP_SECTOR  = stager_base + 0x32000
STAGER_JOURNAL_SECTOR = stager_base + 0x33000
```

For the two possible bases:

```text
stager at 0x00000  -> scratch 0x32000 / 0x33000
stager at 0x40000  -> scratch 0x72000 / 0x73000
```

Both choices remain below NV/factory boundaries on the 512 KiB official map.

Compile/link gate:

```text
stager application declared size < 0x32000
```

The stager may erase/write these two sectors because they belong to the temporary stager bank. It must never use `0x34000+`, `0x76000+`, or the opposite app bank as general scratch.

### 5.2 Journal format

Use a compact structure with versioning and duplicate integrity checks. Example logical fields:

```text
magic                  "GSDJ"
format_version         1
transaction_id         random/session monotonic ID
old_base               0x00000 or 0x40000
stager_base            opposite base
old_declared_size      u32
old_sector0_crc32      u32
backup_sector_crc32    u32
old_app_crc32          u32 (Telink firmware CRC expectation)
state_bits             monotonic one-way state mask
header_crc32           u32
```

Journal state must advance only by clearing bits so a state transition itself never needs an erase during the transaction.

Suggested one-way states:

```text
0xFF EMPTY
0xFE BACKUP_WRITTEN
0xFC BACKUP_VERIFIED
0xF8 OLD_SECTOR_ERASE_STARTED
0xF0 OLD_SECTOR_REWRITTEN
0xE0 OLD_SECTOR_VERIFIED
0xC0 STAGER_INVALIDATION_ARMED
0x80 COMPLETE
```

Exact encoding may change in implementation, but the property is mandatory: an interrupted transition must be distinguishable on reboot without relying on a partially rewritten counter.

### 5.3 Rollback transaction

1. **Precheck**
   - verify known 512 KiB map;
   - verify `old_base != stager_base`;
   - verify current stager marker valid;
   - verify old application declared size is plausible and `< 0x34000`;
   - reconstruct old byte `+8 = 0x4B` in RAM for CRC validation;
   - verify reconstructed old image passes Telink CRC before changing anything;
   - require host-side explicit rollback arm nonce.

2. **Backup old sector 0**
   - erase `STAGER_BACKUP_SECTOR` only;
   - copy exact 4096-byte old application sector 0 **as it exists now**;
   - verify byte-for-byte and CRC;
   - journal `BACKUP_VERIFIED`.

3. **Prepare reconstructed sector**
   - copy backup into RAM/page buffer;
   - patch relative byte `0x08` to `0x4B`;
   - compute expected sector checksum;
   - do not change any other byte.

4. **Erase old sector 0**
   - journal that destructive restore has begun;
   - erase `old_base + 0x0000` sector only.

5. **Rewrite old sector 0**
   - write reconstructed sector page-by-page;
   - read it back fully;
   - compare against the expected reconstructed 4096 bytes.

6. **Validate entire old application**
   - old marker must now be valid;
   - declared size must still match;
   - Telink CRC over the reconstructed old app must equal the stored tail CRC;
   - journal `OLD_SECTOR_VERIFIED`.

7. **Invalidate the stager only after old stock is valid**
   - writing stager marker `0x4B -> 0x00` is a safe one-way program operation and does not require erase;
   - read back;
   - journal completion if execution is still possible;
   - reset.

### 5.4 Power-loss behavior

The stager’s startup path must inspect journal state **before normal Zigbee activity**.

Required recovery branches:

```text
EMPTY / BACKUP only:
  old sector unchanged -> resume or abort safely.

ERASE_STARTED but old sector invalid/blank:
  stager is still valid -> restore old sector from backup -> verify -> continue.

OLD_SECTOR_REWRITTEN but verification not recorded:
  re-read and verify; if exact, continue; otherwise rewrite from verified backup.

OLD stock valid and stager still valid:
  safe state. Finish stager invalidation when execution returns to stager.

stager invalid, old stock valid:
  target terminal state; reset/boot stock.
```

Hardware boot behavior when both banks temporarily look valid must be characterized on the sacrificial target. Do not rely on assumed priority for safety; design correctness around the invariant that **at least one fully verified application is valid at every reset boundary**.

### 5.5 Canary requirement

Before production, repeat the complete install -> dump -> rollback cycle at least **three times** on the same sacrificial GL-SD-301P, and deliberately interrupt power during each distinct rollback state. After every interruption, prove automatic recovery or a documented wired recovery path.

Anything less is insufficient for a production mains device whose stock image is not otherwise available.

---

## 6. Wireless dump protocol v1 design

Supervisor implementation target. Executor does not invent or alter this protocol.

### 6.1 Transport

Use a private Zigbee cluster, proposed:

```text
cluster: 0xFC00
endpoint: 11 unless implementation constraints make a dedicated endpoint safer
frame: cluster-specific ZCL
manufacturer-specific bit: false
APS: normal encrypted Zigbee network transport
communication: unicast only
```

Do not misuse GLEDOPTO’s manufacturer code as an authorization token. This is our private temporary cluster, not a claim to vendor protocol ownership.

### 6.2 Maximum payload

Use **48 bytes maximum flash data per READ response** for v1.

Rationale: Telink’s own Zigbee OTA implementation defines `OTA_IMAGE_MAX_DATA_SIZE = 48`. Keeping the stager payload in that neighborhood reduces fragmentation and RAM pressure. The original 32–64 suggestion is tightened to 48.

If the complete ZCL/APS frame budget on the actual stack requires less, lower the chunk size; never increase it merely for speed.

### 6.3 Commands

Logical command IDs:

```text
0x00 PING
0x01 INFO
0x02 READ
0x03 ABORT
0x04 STATUS

# rollback commands compiled or enabled only after canary phase
0x10 ROLLBACK_PREPARE
0x11 ROLLBACK_EXECUTE
0x12 ROLLBACK_STATUS
```

Response IDs may use `request | 0x80`.

### 6.4 PING

Request:

```text
protocol_version
host_nonce
```

Response:

```text
protocol_version
host_nonce
stager_build_id
session_id
```

Purpose: prove that replies are from the intended stager/session rather than stale MQTT/Zigbee traffic.

### 6.5 INFO

INFO must return enough data for the host to refuse unsafe reads:

```text
protocol_version
stager_build_id
session_id
flash_jedec_id
flash_size
bank_a_base = 0x00000
bank_b_base = 0x40000
bank_a_flag32 @ +8
bank_b_flag32 @ +8
inferred_stager_base
inferred_old_base
old_declared_size @ old_base+0x18
old_tail_crc32
old_reconstructed_crc_valid
allowed_read_start
allowed_read_length
journal_state
rollback_compiled
```

The host must compare these values with the expected target profile before allowing READ.

### 6.6 READ

Request:

```text
session_id  u32
seq         u16/u32
offset      u32   # relative to old application base
length      u8    # 1..48
```

Stager checks:

```text
session matches
length <= 48
offset + length does not overflow
range is entirely within old application declared size
range is entirely inside old application slot
range does not touch NV/MAC/factory areas
```

Response:

```text
session_id
seq
offset
length
data[length]
chunk_crc32
status
```

One outstanding READ at a time in v1. Reliability matters more than throughput.

### 6.7 ABORT

ABORT must only terminate the dump session/return to idle. It must not factory-reset, erase the old bank, alter network NV, or implicitly rollback.

### 6.8 Host persistence and resume

Host artifacts, local/private when they contain raw firmware:

```text
session.json
raw_after_ota.bin
received.bitmap
chunk_checksums.jsonl
reconstructed_stock.bin
validation.json
```

Per chunk:

1. send READ;
2. validate session/seq/offset/length;
3. validate chunk CRC;
4. write at exact file offset;
5. fsync/flush periodically;
6. mark bitmap only after successful write;
7. retry with bounded backoff on timeout/mismatch;
8. resume from bitmap after host restart.

Do not commit raw firmware to GitHub.

### 6.9 Reconstructing the original application image

Keep two outputs:

```text
raw_after_ota.bin        # exact bytes read from old bank after stager OTA
reconstructed_stock.bin  # candidate pre-switch application
```

Expected transformation under standard Telink OTA:

```text
reconstructed_stock[0x08] = 0x4B
all other bytes identical
```

Never silently patch the raw dump. Record a machine-readable diff. Expected diff count for the standard path is exactly one byte at relative offset 8. If any additional differences are detected/required, stop and classify the standard-Telink assumption as disproven until explained.

### 6.10 Exact Telink CRC validator

Given reconstructed application bytes `fw` and declared size `N` from offset `0x18`:

```text
stored_crc = little_endian_u32(fw[N-4:N])
crc = 0xFFFFFFFF
for byte in fw[0:N-4]:
    crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF]
PASS iff crc == stored_crc
```

No final XOR.

Also validate:

```text
fw[6:8] == 5D 02
little_endian_u32(fw[8:12]) == 0x544C4E4B
0 < N < 0x34000
```

SHA-256 the raw and reconstructed files separately.

---

## 7. Temporary stager firmware safety profile

### 7.1 Keep the stager minimal

The extraction stager is not the final dimmer firmware. Avoid bringing dimming/power-stage behavior into the first canary.

Stager must:

- initialize only the radio/Zigbee/flash/watchdog/minimal logging needed for extraction;
- keep unknown power-stage/control GPIOs in their reset-safe/input/high-impedance state wherever possible;
- never issue brightness/on/off commands to an unknown secondary MCU;
- never deliberately drive the triac/gate control path;
- not touch factory/MAC/config/NV sectors;
- avoid background writes outside the normal Zigbee stack NV path;
- expose the smallest possible custom surface.

For a mains-connected production device, the safest first stager state is “light output inert/off” unless exact hardware control is already understood and preserving its state is proven safer.

### 7.2 Zigbee role during extraction

Do not prematurely force the temporary stager into the final non-routing ZED architecture.

First canary priority:

1. preserve/reuse existing Zigbee NV;
2. reappear predictably on the same network and IEEE;
3. support robust unicast extraction;
4. test network-state survival;
5. only later evaluate the final always-on ZED behavior.

A temporary Router build may therefore be the lower-risk first stager if it best matches existing NV/stack assumptions. The final product objective remains a non-routing RX-on-when-idle ZED.

### 7.3 Network-state invariant

Official Telink 512 KiB layout places network information in NV regions at `0x34000` and `0x7A000`, outside application banks. This makes preservation plausible.

But the custom stack may use different NV schema/versioning. Therefore the stager must not perform a “factory new” initialization merely because its own NV parser rejects stock NV. If it cannot safely attach using preserved state, it should stay in a diagnostic/recovery state; it must not erase/reinitialize stock network NV automatically.

A sacrificial canary must establish the exact behavior.

---

## 8. OTA delivery strategy

### 8.1 Do not modify the global OTA universe

For any future authorized canary/production stager OTA, use the narrowest Zigbee2MQTT custom-image mechanism supported by the installed version, targeted to this device/request. Do not publish the stager in a global/public OTA index and do not make it discoverable by unrelated GLEDOPTO devices.

### 8.2 Candidate OTA identity

Do not finalize the stager’s manufacturer/imageType/fileVersion until the canary package is built and the installed Zigbee2MQTT behavior is verified.

Baseline expectation from live stock query:

```text
manufacturerCode = 0x124F
imageType        = 0x1416
currentFileVer   = 0x26013001
```

The generic Telink client requires manufacturer/image type to match and current file version to differ. Server-side Zigbee2MQTT logic may impose additional upgrade/downgrade rules.

For the canary, choose a deliberately controlled temporary fileVersion and record it in the transfer ledger. Do not use the public GL-C-009P image or copy its fileVersion as a shortcut.

### 8.3 Acceptance evidence required before production OTA

At least one of:

1. exact GL-SD-301P stock OTA binary showing the same ordinary Telink image policy; or
2. sacrificial GL-SD-301P successfully accepts our synthetically built, correctly CRC’d stager OTA and boots it; preferably repeated from the same stock build; or
3. wired dump/static analysis of exact stock GL-SD-301P proves no additional signature/encryption/vendor authorization check.

The pinned GL-C-009P plain OTA is strong supportive evidence but not a substitute for one of these target-specific gates.

---

## 9. Evidence confidence model

Use the following grades to prevent an analogy from silently becoming a production assumption.

```text
E0  hypothesis / architectural analogy only
E1  one secondary or cross-model observation
E2  pinned official source OR reproducible binary fact
E3  two independent strong sources agree
E4  reproduced on sacrificial target model/build family
E5  destructive/recovery behavior repeatedly proven with fault injection
```

Current assessment:

```text
live target fingerprint                         E4 (direct live evidence)
OTA client alive + tuple                         E4
TLSR8258/B85 family                              E3
exact TLSR8258F512ET32 / 512 KiB target          E2-E3 hypothesis; requires target/spare proof -> E4
512 KiB Telink bank/NV map                       E2 official; target applicability not yet E4
opposite bank readable from application          E2 official SDK behavior
standard Telink boot-marker semantics             E2 official SDK behavior
same-family GLEDOPTO plain OTA element             E2 pinned binary
exact GL-SD client accepts arbitrary plain OTA     E1/E2 supportive only; production BLOCKED
network NV survives our stager stack               E0/E2 architectural; target test required
transactional rollback                             E0 design; must reach E5 before production
power-stage architecture                           UNKNOWN
exact GL-SD stock binary recovered                 NO
```

Production custom OTA requires:

```text
TARGET_FLASH_MAP >= E4
TARGET_OTA_ACCEPTANCE >= E4
ROLLBACK >= E5
WIRED_RECOVERY_AVAILABLE OR equivalent proven recovery path
```

---

## 10. Canary validation matrix

A sacrificial **GL-SD-301P**, preferably stock build `20651203`, is the test target. A different GLEDOPTO light is not an equivalent canary.

### C0 — wired baseline

Before any custom OTA:

- photograph PCB/module/markings;
- identify safe low-voltage power/debug pads;
- read full flash twice independently;
- SHA-256 both reads; require exact equality;
- record JEDEC/flash size if tool exposes it;
- parse both app-bank markers;
- map NV/factory regions without modifying them;
- extract exact stock application and validate Telink CRC;
- archive raw dump locally/private.

### C1 — stock network baseline

On isolated/test Zigbee network if possible:

- pair stock canary;
- capture IEEE, endpoint descriptor, role, Basic attrs, OTA tuple;
- create a known group and direct binding if hardware setup permits;
- snapshot network/NV-relevant behavior;
- power-cycle and verify persistence.

### C2 — first stager OTA acceptance

- full pre-OTA wired dump already available;
- automatic OTA disabled;
- serve only the exact stager artifact to canary;
- capture Query Next Image Request/Response and every terminal OTA event;
- verify stager boot;
- immediately wired-read both bank marker bytes;
- compare inactive/old bank against pre-OTA dump.

Pass condition for generic behavior:

```text
old app differs from pre-OTA stock only at old relative offset +8 (0x4B -> 0x00)
```

Any additional difference must be investigated before proceeding.

### C3 — network/NV survival

- prove same IEEE;
- prove device remains on test network without factory reset/rejoin if intended;
- compare NV regions bytewise against pre-OTA dump and classify expected stack writes;
- prove stager INFO/READ is reachable.

If the stager factory-initializes or destroys stock network NV, production plan must be redesigned.

### C4 — dump correctness

- wirelessly dump old app through the stager;
- reconstruct byte +8;
- compare reconstructed wireless image byte-for-byte with the pre-OTA wired stock application;
- require exact equality;
- validate Telink CRC;
- record SHA-256.

This is the key proof that wireless extraction itself is forensically correct.

### C5 — rollback correctness

- execute transactional rollback;
- verify stock boots;
- full wired flash read;
- compare restored stock app to original;
- verify network function and expected state.

### C6 — power-loss fault injection

Repeat rollback while cutting stager power at each journal state. For every state, document:

```text
state before cut
which bank flag was valid
which sector was being changed
state after power return
recovery branch taken
final stock app hash
final network state
```

No unexplained boot failure allowed.

### C7 — repetition

Three complete successful cycles minimum:

```text
stock -> stager -> wireless dump -> reconstructed hash match -> rollback -> stock
```

Only after C0–C7 pass can a production proposal be authored.

---

## 11. Production runbook — design only, NOT YET AUTHORIZED

This section defines the eventual execution shape so all tooling is built around it. It is not permission to execute it now.

### P0 preflight

Require all gates:

```text
exact target IEEE 0xa4c13850cfcdb3a4
model GL-SD-301P
swBuildId 20651203 unless a new review explicitly accepts otherwise
OTA tuple 0x124F / 0x1416 / 0x26013001 unless re-reviewed
controller/network expected
stable mains power
Z2M healthy
custom OTA artifact hash pinned
stager source/build hash pinned
rollback test evidence C0-C7 PASS
recovery equipment ready
```

### P1 snapshot

Archive before OTA:

- `zigbee2mqtt/bridge/devices` object;
- target `database.db` record;
- group membership;
- bindings;
- reporting;
- target Basic attributes;
- OTA tuple;
- relevant Z2M versions/config;
- current normal OnOff/Level functional check;
- custom converter/OTA configuration baseline.

### P2 isolate OTA action

- disable automatic OTA checks;
- ensure no unrelated firmware source/index can match;
- use per-request targeted custom firmware if installed Z2M supports it;
- one device only;
- verify artifact SHA-256 immediately before start.

### P3 OTA stager

- capture complete logs;
- stop on any tuple/header/size discrepancy;
- do not retry blindly after partial/unexpected failure;
- after boot, require PING then INFO before any READ.

### P4 INFO gate

Host refuses extraction unless:

```text
flash size/map expected
exactly one stager bank identified
old bank identified
old declared size plausible
old reconstructed CRC valid
read whitelist correct
journal clean
```

### P5 dump

- 48-byte bounded reads;
- persistent resume bitmap;
- per-chunk CRC;
- final reconstructed Telink CRC;
- final SHA-256;
- do not roll back until two independent local validation passes agree.

### P6 archive

Keep raw stock firmware private/local. Store in the repo only metadata/hashes/redacted validation, never the raw vendor binary.

### P7 rollback

Only the canary-proven transactional implementation. No manual flash-flag improvisation.

### P8 post-rollback acceptance

Require:

```text
stock Basic tuple restored
same IEEE
normal on/off/brightness works
existing group 110 present or deterministically restored
existing binds present or deterministically restored
reporting present or deterministically restored
no unexpected Z2M interview/configure changes
no new routing/network anomaly attributable to the experiment
```

If restoration is required, use the existing snapshot/restore tool and only the exact missing state; do not globally rebuild the Zigbee network.

---

## 12. Executor assignment NOW — bounded read-only readiness batch

The following work is authorized as **read-only evidence collection only**. It prepares the Supervisor to supply the implementation package without another discovery loop.

### 12.1 Mandatory bootstrap

Load current context in protocol 2.1 order:

1. external `analienx/config:skills/supervisor-executor/SKILL.md@main`;
2. external `analienx/config:supervisor/projects.yaml@main`;
3. local `analienx/gledopto/AGENTS.md`;
4. local `.supervisor/project.yaml`;
5. `devices/gl-sd-301p/README.md`;
6. `devices/gl-sd-301p/STATUS.md`;
7. this document;
8. issue #1 and newest comments.

Post standard bootstrap proof including protocol version, canonical SHAs, local HEAD/worktree status, task/control channel, protected invariants, and `STATUS=READY|BLOCKED`.

### 12.2 Repository/tooling verification

Read-only:

```text
git status --short
git rev-parse HEAD
python -m unittest discover -s tools/tests -p "test_tuya_glsd_migrate.py" -v
python tools/tuya_glsd_migrate.py --help
```

Do not “fix” failures during this batch. Capture exact outputs and classify them.

The current migration tool has changed since the original handover; report the current blob/commit and actual test count, not the old handover count.

### 12.3 Production target current-state verification

Using read-only Z2M/API/database inspection only, report:

```text
PRODUCTION_DEVICE_VISIBLE = yes/no
SAME_IEEE = yes/no
MODEL =
SW_BUILD =
OTA_TUPLE =
ROLE =
GROUP_110 = present/missing/unknown
BIND_ONOFF = present/missing/unknown
BIND_LEVEL = present/missing/unknown
REPORT_ONOFF = present/missing/unknown
REPORT_LEVEL = present/missing/unknown
```

Do not re-pair, reconfigure, bind, group-add, reset, migrate to Tuya, or run an OTA update to obtain this information.

### 12.4 Recovery/canary inventory

Report facts only:

```text
SACRIFICIAL_GL_SD_301P_AVAILABLE = yes/no
SACRIFICIAL_BUILD_IF_KNOWN =
SWS_PADS_ALREADY_IDENTIFIED = yes/no
SWIRE_PROGRAMMER_AVAILABLE = yes/no + exact hardware
SERIAL_FLASH_TOOL_AVAILABLE = yes/no + repo/ref/version
OFFICIAL_TELINK_PROGRAMMER_AVAILABLE = yes/no
TC32_TOOLCHAIN_AVAILABLE = yes/no + exact version/path
TELINK_ZIGBEE_SDK_LOCAL = yes/no + ref
POWER_STAGE_PHYSICAL_MAPPING_AVAILABLE = yes/no
ISOLATED_TEST_ZIGBEE_NETWORK_AVAILABLE = yes/no
UPSTREAM_RELAY_CONTROL_READY = yes/no/unknown
LOCAL_PRIVATE_FIRMWARE_STORAGE_READY = yes/no
```

Do not open the production unit. Do not connect any debugger to the production unit.

### 12.5 Current Tuya state

Do **not** repeat the Smart Life migration/reset merely for this batch.

Current known conclusion is:

```text
TUYA_DEVICE_IDENTITY = PROVEN
PRODUCT_FIRMWARE_API = NOT AVAILABLE TO APP-ACCOUNT AUTHORIZATION
CURRENT_UPDATE_OFFER = NONE EXPOSED
STOCK_BINARY_RECOVERED = NO
```

If credentials/current project can be checked with the existing **GET-only** tool without moving/resetting the Zigbee device, it may be reported. Otherwise leave Tuya untouched.

### 12.6 Return block

Post one result comment:

```text
## EXECUTOR RESULT — GL-SD-301P extraction readiness

SUPERVISOR_PROTOCOL = 2.1
CANONICAL_SKILL_SHA =
CANONICAL_REGISTRY_SHA =
LOCAL_HEAD =
WORKTREE = CLEAN/DIRTY
MIGRATION_TOOL_BLOB =
TESTS =

PRODUCTION_DEVICE_VISIBLE =
SAME_IEEE =
MODEL =
SW_BUILD =
OTA_TUPLE =
ROLE =
GROUP_110 =
BIND_ONOFF =
BIND_LEVEL =
REPORT_ONOFF =
REPORT_LEVEL =

SACRIFICIAL_GL_SD_301P_AVAILABLE =
SACRIFICIAL_BUILD_IF_KNOWN =
SWS_PADS_ALREADY_IDENTIFIED =
SWIRE_PROGRAMMER_AVAILABLE =
SERIAL_FLASH_TOOL_AVAILABLE =
OFFICIAL_TELINK_PROGRAMMER_AVAILABLE =
TC32_TOOLCHAIN_AVAILABLE =
TELINK_ZIGBEE_SDK_LOCAL =
POWER_STAGE_PHYSICAL_MAPPING_AVAILABLE =
ISOLATED_TEST_ZIGBEE_NETWORK_AVAILABLE =
UPSTREAM_RELAY_CONTROL_READY =
LOCAL_PRIVATE_FIRMWARE_STORAGE_READY =

TUYA_DEVICE_IDENTITY = PROVEN
CURRENT_UPDATE_OFFER = NONE EXPOSED
STOCK_BINARY_RECOVERED = NO

UNEXPECTED_STATE = none/<exact details>
STOP_REASON = none/<exact blocker>
```

Attach/supply sanitized raw evidence for any `present/yes/PASS` claim that materially changes a production gate.

### 12.7 STOP after the readiness result

The Executor is **not authorized by this transfer document** to:

- serve or flash any custom OTA to the production device;
- serve the historical GL-C-009P image to anything;
- factory-reset/re-pair/migrate the production device;
- modify groups/binds/reporting;
- write arbitrary Zigbee attributes;
- erase/write any target flash;
- open the production mains device;
- implement a different stager/protocol/rollback scheme;
- use guessed SWS pads or voltage;
- commit raw vendor firmware/secrets.

After the readiness result, stop. The Supervisor will supply code/artifacts and the next bounded authorization appropriate to the evidence.

---

## 13. Supervisor implementation package expected after readiness

The next package, authored/reviewed by the Supervisor rather than improvised by the Executor, should contain:

```text
tools/glsd_dump_host.py
  - PING/INFO/READ protocol
  - resumable bitmap
  - chunk CRC verification
  - raw/reconstructed output separation
  - exact Telink xcrc32 validator
  - SHA-256 manifest
  - strict target/map/size whitelists
  - no raw firmware logging

firmware/glsd_dump_stager/
  - pinned Telink SDK/toolchain
  - deterministic build
  - private cluster protocol
  - bank-detection code
  - old-bank read whitelist
  - unknown power-stage pins kept safe
  - rollback journal code behind a build/feature gate
  - compile-time flash-layout assertions

scripts/build_stager.*
  - reproducible build
  - OTA wrapper generation
  - header parser/self-check
  - final artifact SHA-256

scripts/validate_glsd_dump.*
  - one-byte marker reconstruction
  - Telink CRC validation
  - diff report against wired canary dump

fixtures/
  - synthetic bank-A/bank-B flash images
  - interrupted journal states
  - corrupt chunk/CRC cases

TEST_PLAN.md
  - unit tests
  - synthetic flash tests
  - canary C0-C7 procedures
  - fault-injection matrix
  - production gates
```

The first implementation should make destructive rollback commands impossible to invoke accidentally—e.g. compile them out or require a canary build flag until the dump path is already proven.

---

## 14. Scientific decision tree

```text
START
 |
 |-- sacrificial GL-SD-301P available?
 |      |
 |      +-- YES --> physically map safe debug interface
 |      |            -> 2x full SWS dump + identical hash
 |      |            -> exact MCU/flash/map proof
 |      |            -> exact stock static RE
 |      |            -> build/test stager on canary
 |      |            -> prove wireless dump == wired stock
 |      |            -> prove rollback with fault injection
 |      |            -> production proposal only after E4/E5 gates
 |      |
 |      +-- NO --> no production experiment
 |                   -> finish host/stager code offline
 |                   -> synthetic tests
 |                   -> continue vendor-support/current-update observation
 |                   -> wait for target-specific canary/recovery gate
 |
 +-- vendor exact GL-SD OTA appears before canary?
        |
        +-- YES --> download privately, hash, static RE auth/map first
        |            -> may raise acceptance confidence but still prove rollback
        |
        +-- NO --> keep canary path as authority
```

---

## 15. Bottom line

The project is no longer blocked by the question “is reading the old bank even architecturally possible?” The standard Telink SDK proves that it is.

The same-family public GLEDOPTO binary also materially weakens the hypothesis that all GLEDOPTO OTA is vendor-signed/encrypted: that 2024 image is an ordinary plain Telink OTA element with the same `0x124F / 0x1416` family identity.

The remaining production blockers are narrower and testable:

1. exact GL-SD-301P silicon/flash map applicability;
2. exact GL-SD stock-client acceptance/auth policy;
3. preserved network-NV compatibility with our stager stack;
4. exact power-stage safety behavior;
5. transactional rollback proven under power loss;
6. a real recovery surface before touching the only production unit.

Until those pass, **no production custom OTA**.

If a sacrificial GL-SD-301P is available, wired SWS extraction is the immediate highest-value action and should precede production wireless extraction. If no spare exists, continue only offline implementation/testing and read-only readiness work; do not convert strong cross-model evidence into production authorization.