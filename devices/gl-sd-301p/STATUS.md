# STATUS — gl-sd-301p

## 2026-09-03 — Phase 1 (software-only fingerprinting) executed

- Executor session per `analienx/config:skills/supervisor-executor/SKILL.md` v2.1.
- SAFETY_CLASS: SOFTWARE_READONLY. No writes to the device. No OTA update.
  Device-initiated protocol replies (`queryNextImageResponse` NO_IMAGE_AVAILABLE)
  were protocol-mandated responses to the supervisor-authorized check only.
- Live mutations (authorized by the supervisor procedure comment): temporary
  external OTA-diagnostic converter, temporary `ota.disable_automatic_update_check`,
  one read-only diagnostic extension. ALL REVERTED and verified (bindings and
  configured reporting byte-identical to pre-probe baseline).
- Result: **PARTIAL PASS**. All software-only probes exhausted. Remaining
  unknowns (MCU exact, power-stage architecture) require a sacrificial spare.
- Evidence: `evidence/phase1-software-only-20260903/` (raw originals on the HA
  host under `/config/zigbee2mqtt/gledopto_probe/`).

## Next

1. Acquire sacrificial GL-SD-301P spare.
2. Unpowered teardown of the spare: module/MCU markings, power-stage trace
   review (direct Telink GPIO/timer vs second MCU UART), wired SWS/debug access.
3. Full-flash backup of the spare + static analysis (Telink flash layout).
4. Only then: firmware plan for RX-on-when-idle End Device build
   (`ZB_ED_ROLE=1`, `ZB_ROUTER_ROLE=0`, `RX_ON_WHEN_IDLE=1`, `PM_ENABLE=0`).
