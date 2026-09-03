# Tuya extraction session — 2026-09-03

Session: `glsd-tuya-guided-20260903T190945Z` (Z2M snapshot) and extract dirs
`glsd-tuya-extract-20260903T2018/2037/2040Z`.

## Result

**Tuya Cloud confirms the GL-SD-301P is Tuya-managed, but currently exposes
no OTA URL.**

```text
TUYA_CHILD_DEVICE_ID = bf84f22bd18834fddfyisf   (node_id = a4c13850cfcdb3a4 — our exact IEEE)
TUYA_GATEWAY_ID      = bf47000fd4eac93dcc217x (GW018-DM)
TUYA_PRODUCT_ID      = 1jlpstyg ("Smart Lighting", category dj = Light Source)
TUYA_REGION          = eu (Central Europe, openapi.tuyaeu.com)
FIRMWARE_V2_GET      = success — channel 3, type_desc "ZigBee Module",
                       current_version 1.0.0, upgrade_status 0,
                       no url field exposed
FIRMWARE_LEGACY_GET  = blocked — 28841101 "This API is not subscribed"
FIRMWARE_URL_FOUND   = no
DOWNLOADED_FILE      = none
```

## Interpretation

- The live GL-SD-301P sits behind a Tuya gateway on PID `1jlpstyg` with
  ZigBee-module firmware `1.0.0` per cloud records and **no published update**
  on that channel — Tuya serves OTA URLs only when a vendor-published upgrade
  exists for the PID/channel, so there is currently no GL-SD binary obtainable
  from the cloud.
- The legacy `upgrade-infos` endpoint could still expose historical/current
  update info including URLs — it requires subscribing the **Device
  Management / IoT Core** API product to the project (user, Tuya Developer
  Platform → Cloud → Development → project → API tab).
- Live tuple remains `0x124F / 0x1416 / 0x26013001` (swBuild 20651203) per our
  direct Zigbee evidence; the Tuya `1.0.0` versioning is Tuya's own module
  record, not the Gledopto swBuildId.

## Hard invariants held

No OTA-start POST, no Smart Life update click, no reset/re-pair beyond the
user's gateway pairing, no vendor binary committed (none was served).
Production unit remains on the Tuya gateway pending the operator's next step;
return-to-Z2M restore-check will run after the extraction path is exhausted.