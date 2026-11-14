# STATUS — GL-SD-301P

## 2026-11-14 (latest): ED-009 installed by OTA, working End Device

The owner chose reliable OTA only, with no SWire fallback. Before the update,
the SDK boot-time `voltage_detect(1)` reset loop was removed
(`VOLTAGE_DETECT_ENABLE=0`, app-level PB3 guard on startup saves, read-only
Basic `0xFF11` supply snapshot; `20123fe`). CI then published the exact OTA
on manual runs (`15ffb75`); its bytes are identical to the reviewed
`20123fe` build.

Live sequence on `0xa4c13850cfcdb3a4`:

1. The converter `1b5915c6…` was hot-installed through
   `bridge/request/converter/save`, after checking it against the live
   zigbee-herdsman-converters 26.115.1. ED-008 kept its old exposes.
2. The image was staged in the Zigbee2MQTT data directory with a one-entry
   index (sha512, modelId, min/max fileVersion = ED-008). A read-only check
   returned `update_available: true`. The first index had
   `hardwareVersionMin/Max` and never matched, because the Telink query
   carries no hardware version (`fc = 0`).
3. A single targeted update ran with 50-byte blocks: 1526 s, status ok,
   from `GLSD-ED-008`/`0x7F090001` to `GLSD-ED-009`/`0x7F0A0001`.
   Zigbee2MQTT re-interviewed the device itself; it stayed an End Device.
4. Bench `post-ota`: fresh build, fresh version, converter and supply all
   PASS. The first PB3 reading was 3272 mV with 0 refused saves.

Owner acceptance: in daily use and "works as charm". ED-009 is the working
End Device firmware for this dimmer. Still open: the bench `startup`
power-cycle cases, stock-to-custom conversion by OTA and an exercised
vendor recovery.

## 2026-11-01 — third review round: "dormant" withdrawn

A third independent review showed that every way of keeping a removed unit
quiet also created a path where it never comes back on its own. Dormant is
gone, and a CI gate forbids it. A removed unit re-joins on the next
permit-join; use the Zigbee2MQTT `blocklist` to keep it out. Also added:
a rejoin-to-steering handover on a factory-new device and pinned SDK patch
P7, which keeps the OTA client from sticking in "download in progress" (the
round-3 timing watchdog was replaced by it after the fourth review). Identity unchanged (`GLSD-ED-007`,
`0x7F080001`, never flashed); bind to the exact image hash in the review
record.

## 2026-11-01 (later) — two independent acceptance review rounds, fixes in

The over-the-air identity is now **`GLSD-ED-007`, APP_BUILD `0x08`,
fileVersion `0x7F080001`**, dateCode `20261101`. It supersedes
`GLSD-ED-006` / `0x7F070001`, which was never flashed.

- Round 1 (A1–A10) and round 2 (B1–B10) were done by separate reviewers,
  each checked against the pinned SDK source. They found real recovery gaps:
  a reboot loop after a failed TC link-key exchange, a device that could be
  stranded off the network, an OTA re-query that silently did nothing, and
  a Leave with rejoin wrongly treated as removal. All are fixed with tests.
  Dispositions:
  `HARDENING-FLASH-READINESS-REVIEW-20261101.md`.
- Live read-only checks: Zigbee2MQTT is `2.14.2`, which is enough for URL
  requests. The live tuple matches the preflight. The stock unit is a
  **Router**, and up to 11 End Devices may use it as parent; they will
  re-parent after the switch to End Device.
- Still required: hosted CI on the final head, the external converter
  installed, and explicit human authorization. No device action is
  authorized.

## 2026-11-01 — flash-readiness review: recovery surface fixed, not yet re-gated

The active candidate is the PR #8 hardening branch
(`codex/glsd301p-client-hardening`); PR #6 `760c141` stays frozen as the
previous reviewed reference.

A flash-readiness review of `109b6b1` found that a factory-new device could
never commission again (no network steering, no reset path, no reboot on
Leave). That would make vendor OTA recovery undeliverable after a Leave or an
unrestorable stock NV. Fixed together with a planned recovery request,
a Zigbee2MQTT external converter, read-only StartUp attributes, OTA query
after join and a corrected over-the-air identity (`GLSD-ED-006`,
`0x7F070001`). Details and local verification:
`HARDENING-FLASH-READINESS-REVIEW-20261101.md`.

Gate state: locally the host harnesses, architecture firewall and real TC32
build pass; hosted CI, a fresh independent acceptance review and read-only
Zigbee2MQTT downgrade verification are still required. No device action is
authorized.

## 2026-09-18 — execution model and durable project state reconciled

The project now follows Supervisor ↔ Executor protocol 2.2 execution
designations from `analienx/config/main`:

```text
PROJECT_ID              = gledopto-gl-sd-301p
DEFAULT_EXECUTION_MODE  = host_specialist
OPTIONAL_EXECUTION_MODE = foundry_isolated
BUILD_LANE              = firmware builder CI
HOST_SPECIALIST         = USB, MQTT, HA API
```

Execution mode is separate from capability and mutation authority. Isolated
engineering/review should use `foundry_isolated` with a suitable
AUTONOMOUS/EXPERT profile and `WORKSPACE_IMPLEMENTATION`. Live canary/device
execution should de-escalate to deterministic
`host_specialist + OPERATOR + BOUNDED_RUNTIME` with exact authorization,
gates, evidence and rollback.

## 2026-09-09 — safety-sweep fixes green and candidate frozen

The independent safety review of the previous candidate found four blockers:
SDK/application ABI mismatch, physical PUSH not cancelling remote transitions,
reserved Level `0xFF` normalization, and blocking runtime UART.

All four are closed at:

```text
PR   = #6
HEAD = 760c141925f29e517831c92afd61bd9c15e1b7b5
```

Same-head required workflows:

```text
34276666901  Interoperability boundary and host tests  SUCCESS
34276666846  GL-SD-301P final no-spare readiness       SUCCESS
```

Exact-head target facts:

```text
STACK_ARCHIVE=libzb_ed.a
ROUTER_ARCHIVE_LINKED=NO
ZB_ED_ROLE=1
ZB_ROUTER_ROLE=0
ZB_MAC_RX_ON_WHEN_IDLE=1
PM_ENABLE=0
ENDPOINT=11
POWER_SOURCE=MAINS
ADC_FLASH_SAFETY_PIN=GPIO_PB3_VENDOR_FIRMWARE_CONFIRMED
UART=9600_8N1_PB1_TX_PA0_RX
ZCL_SPEC_CLUSTER_INFO_ABI=size18_attrTbl6_register10_appCb14
UART_RUNTIME_TRANSPORT=NONBLOCKING_OFF_PRIORITY_LATEST_NORMAL
```

Frozen artifact identity:

```text
final bytes      130436
final SHA256     d6f41fb720002390817b56cc925dae08a657ca9f77b94c72c646af49a7426551
wrapper bytes    130502
wrapper SHA256   43e6996fb55dd5569f450117ac430b8dd11d35f23a878b4680439f84b65c899b
```

The software/reproducibility/preflight gate is GREEN. The live production unit
has not yet been proven running the custom End Device image.

## 2026-09-08 — independent safety review failed old candidate

Review branch `review/safety-sweep-20260908` correctly blocked the earlier
candidate after reproducing four release blockers. No live production mutation
was performed. That review is retained as evidence of the fail-closed process;
its blockers are superseded only by the exact fixed SHA above.

## 2026-09-07 to 2026-09-08 — architecture and target convergence

Independent analysis established:

- Telink TLSR8258-family Zigbee MCU, 512 KiB flash class;
- separate power-stage controller over 9600 8N1 UART;
- PB1 TX / PA0 RX;
- PC2 physical PUSH input;
- PB4 auxiliary compatibility behavior;
- PB3 vendor-confirmed flash/VBAT safety ADC input;
- exact normal six-byte power-stage control family;
- independently authored RX-on-when-idle End Device target using `libzb_ed.a`;
- fail-closed runtime/output guard and host-side interoperability tests.

The project moved from the original spare-dependent research plan to the
explicit `PRODUCTION_ONLY_NO_SPARE` track with stronger preflight/recovery
gates.

## Earlier evidence

The September 3 software-only fingerprinting and MCU/flash forensics remain
historically valid evidence, but their then-open architecture/spare conclusions
are superseded by the later same-model vendor-firmware analysis and independently
verified target work.

See repo `evidence/`, device interoperability/safety documentation, PR #6, and
issue #1 for the detailed chronology.

## Next

1. Fresh independent executor review of the exact frozen head if required by the
   latest issue authorization.
2. Exact-device live preflight for only `0xa4c13850cfcdb3a4`.
3. If every bounded gate passes, execute the single authorized canary using
   `host_specialist + OPERATOR + BOUNDED_RUNTIME`.
4. Verify boot, Zigbee rejoin/OTA reachability, OnOff/Level, physical PUSH
   short/hold takeover, PB4 coexistence and recovery availability.
5. On any anomaly, STOP and follow the validated recovery path; no blind retries
   or broad OTA publication.
