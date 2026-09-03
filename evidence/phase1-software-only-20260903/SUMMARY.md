# Phase 1 — software-only fingerprinting evidence (2026-09-03)

Executor session against the live Z2M 2.13.0 / ZHC 26.90.0 / zigbee-herdsman
10.8.0 stack (EmberZRNet 9.1.1 coordinator). All probes read-only per the
supervisor procedure in analienx/gledopto#1. Sanitized evidence only; raw
originals remain on the HA host under `/config/zigbee2mqtt/gledopto_probe/`.

## Method

- A temporary external converter (`gledopto_ota_diag.js`:
  `m.light({configureReporting:false, ota:true})`, no configure routine) was
  added so the documented bridge OTA **check** (never update) was possible;
  `ota.disable_automatic_update_check: true` was set first. Both were reverted
  and verified afterwards.
- A temporary read-only Z2M external extension performed ZCL attribute reads
  and foundation discovery via the live controller, recorded device frames,
  and captured the device-reported OTA descriptor via the same
  imageNotify → queryNextImageRequest → NO_IMAGE_AVAILABLE flow the official
  check uses. It was removed after the probes.

## Key results

- EP11 simple descriptor: in `genBasic,genIdentify,genGroups,genScenes,genOnOff,
  genLevelCtrl,lightingColorCtrl,touchlink`; out `genOta`. EP242 out `greenPower`.
- Basic (EP11): zclVersion 3, appVersion 1, stackVersion 2, hwVersion 2,
  manufacturerName GLEDOPTO, modelId GL-SD-301P, dateCode 20240704,
  powerSource 1 (mains single phase), deviceEnabled 1, swBuildId "20651203".
  UNSUPPORTED_ATTRIBUTE: 0x0008–0x000E, 0x0011.
- OnOff: onOff=0, startUpOnOff=1. Level: currentLevel read OK,
  startUpCurrentLevel=255; 0x0002/0x0003/0x000F/0x0010–0x0014 UNSUPPORTED.
- Foundation discovery (0x0C, 0x11, 0x13, 0x15) on all four clusters, standard
  and manufacturer-specific (0x124F) frames: **NO RESPONSE AT ALL**. The device
  does not implement ZCL discovery. Attribute 0x124F-framed reads of standard
  attributes succeed (the device honours manufacturer-specific framing).
- Live OTA descriptor (device-reported in queryNextImageRequest):
  `{"fieldControl":0,"manufacturerCode":4687,"imageType":5142,"fileVersion":637612033}`
  = 0x124F / 0x1416 / 0x26013001.
- Official bridge check: `{"status":"ok","data":{"downgrade":false,
  "update_available":false,...}}`; no `source`/`release_notes` in the response
  (no matching public index entry).
- Public zigbee-OTA index: 14 entries with manufacturerCode 4687; NONE with
  imageType 5142. Gledopto-family entries use imageType 0x14xx and ~200–290 KB
  images (e.g. GL-B-007P 5154/0x24024001, GL-D-004P 5202/0x15040001,
  GL-S-006P 5172/0x17000001, Candeo C-ZB-LC20v2 5145/0x32000001). This is a
  product/firmware-family pattern, NOT an MCU identifier.
- Hubitat community corroboration: a second GL-SD-301P reports
  `124F-1416-28013001` (same imageType 0x1416, fileVersion 0x28013001,
  swBuild 20851203) and identical EP 0x0B cluster set.
- No public or local stock binary for the GL-SD-301P was found.
- No MCU/bootloader/UART strings can be obtained software-only without a
  binary; power-stage architecture has no software-only evidence.

## Files

- `ota-live-descriptor.json` — device-reported OTA descriptor (KEY ARTIFACT).
- `ota-check-evidence.json` — official bridge check request/response capture.
- `zcl-probe-results-v2.json` — batched reads + discovery + event fetch.
- `zcl-probe-individual.json`, `zcl-probe-focused.json` — per-attribute reads
  with UNSUPPORTED_ATTRIBUTE statuses.
- `device-entry.json`, `db-record.json` — baseline descriptor/database records.
- `config-relevant.sanitized.json` — relevant Z2M config (secrets redacted).
- `artifact-hunt.json` — host-side artifact search results.

## Verification after restore

Bindings and configured reporting byte-identical to the pre-probe baseline;
stock converter active; temporary config flag removed; extension and converter
files deleted.
