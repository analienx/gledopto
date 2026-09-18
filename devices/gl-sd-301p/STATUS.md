# STATUS — GL-SD-301P

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
