# Batch 10 — exact full-size CRC-invalid shadow probe on production GL-SD-301P — 2026-09-06

Gate head: `de99b0cd678a9e1769d43797372e377314f4d550` (`research/wireless-dump-stager`)
CI run 34045773995 on that head: SUCCESS (pinned TC32 rebuild + exact real candidate + shadow + forensics).
This is the exact frozen shadow tooling/hashes from supervisor comment 5560610124.

## Offline build reproducibility (WSL, pinned TC32 v2.0 + pinned Telink SDK V3.7.2.0)

```text
real candidate  156806 B   sha256 1b05e890fdebf753f8d588286bd67c956fbd24e48c8df1eaa16fd6d4477b50f8
                           sha512 36d4eb2be5f949160fd5c0dd9115a9263bffce9b94df80c9bd8feed9d1aabd2d66e172f108de25154c80273a8d6c544865d8be237cbf4bca3827db3c144a814c
                           inner  (final.bin) 156740 B, sha256 923d2aa70390931c3bd17cb8ec0d73285b27d964913cf5f84cd0c67cc73783c8
shadow        156806 B   sha256 7bd1c59ca8b98c53d4a2bd1864cb8d8f937ff028767fac355e3c351f7b2bff95
                           sha512 04a500a87ea2782e3f010c95e87cd3d941d462f07bc648753f3dfab9a659a7b22dbefb4bd721682f3197db1da1c8d530efeef83367702de1b7e0cf40830ecd8d
```

All four hashes match the supervisor-frozen values exactly.

Independent forensics (`tools/telink_ota_forensics.py --json` at de99b0cd):

```text
manufacturerCode 0x124F / imageType 0x1416 / fileVersion 0x7F00FFFF (inner + outer)
hardwareVersionMin/Max 2 / 2
5D02 preamble valid · startup marker 0x544C4E4B valid · declared inner size 156740 valid
outer_identity_matches_inner true
Telink xcrc32 stored 0xF95273EA vs expected 0xF95273EB  -> INVALID (only fatal condition: telink_crc_mismatch)
```

Byte diff vs the real candidate is exactly 10, confined to the supervisor-declared offsets:
outer fileVersion 0x0E..0x11, inner fileVersion 0x44..0x47, CRC trailer 0x2C482..0x2C485
=> [0x0E,0x0F,0x10, 0x44,0x45,0x46, 156802,156803,156804,156805].

Repo regression at de99b0cd: `tools/tests/test_glsd_stager_shadow_probe.py` -> 4 tests OK.

## Evidence / hygiene gates closed (per 5560610124 / 5560768653)

```text
BATCH9_EVIDENCE_COMMITTED   = 201f005 on research/wireless-dump-stager (B9 summary present)
STALE_8899_LISTENER         = removed (PID 5758 /tmp/v8-ota-server.js killed); PORT_8899_FREE (ss verified in container + host)
NO_SCHEDULED_OTA            = confirmed; only unrelated TS011F plug was unscheduled 15:09; ota/ dir empty
NO_GLOBAL_OVERRIDE_INDEX     = none configured; no zigbee_ota_override_index_location
```

## Live transfer (authorized; per-device, one-use loopback URL, target a4c13850cfcdb3a4 only)

```text
request  {"id":"0xa4c13850cfcdb3a4","url":"http://127.0.0.1:18900/probe-<one-use-token>/glsd-stager-neutral.shadow-crc-reject.ota"}
server   one-use loopback HTTP server (free port 18900, bind + self-fetch of the exact token path serving 156806B verified BEFORE the request; the real fetch served 156806B once then 404
transfer began 20:12:34 (container-local); progress reports advanced monotonically to ~98% (>156806-byte shadow streamed to device over individual Zigbee image blocks)
result    20:30:54 (local) = 18:30:54 UTC
          "OTA update of 0xa4c13850cfcdb3a4 failed with reason: INVALID_IMAGE"
```

## Final matrix (target a4c13850cfcdb3a4 only)

```text
EXACT_SHADOW_REPRODUCED             = PASS (bit-exact WSL reproduction + host sha256==shadow)
FULL_SIZE_TRANSFER_COMPLETED        = PASS (156806 B served by Z2M and streamed to device; ~98% before validation)
INVALID_IMAGE_OBSERVED              = PASS (device-side Telink validator rejected image after full transfer)
STOCK_TUPLE_UNCHANGED               = PASS (swBuildID 20651203 / dateCode 20240704 / hwVersion 2 / appVersion 1 / stackVersion 2; update.state back to "available")
NORMAL_CONTROL_AFTER_REJECTION      = PASS (ON / OFF / 25% / 75% verified after rejection)
UNEXPECTED_RESET_LEAVE_REJOIN       = NO   (nwkAddr 17901 constant; bridge health leave_count 0 / network_address_changes 0)
REPAIR_REQUIRED                     = NO
POWER_CYCLE_PERSISTENCE             = PASS (after a physical power cycle: returned on same IEEE a4c13850cfcdb3a4 / nwk 17901, frozen stock tuple, update "available", normal ON/OFF/Level control)
```

## Post-run state

```text
LIVE_CUSTOM_OTA                     = NO_GO  (unchanged)
REAL_BOOTABLE_STAGER                = NO_GO  (unchanged)
PRIVATE_0xFC00_EXTRACTION           = NO_GO  (unchanged)
RECONSTRUCTED_STOCK_OTA             = NO_GO  (unchanged)
FINAL_CUSTOM_END_DEVICE_FIRMWARE   = NO_GO  (unchanged)
```

Raw artifacts (candidate binary, shadow binary + sidecar, scripts, JSON results, container log extracts) remain on the HA host under
`/config/zigbee2mqtt/gledopto_probe/acceptance/b10/`. This summary contains no credentials, network keys, or addresses beyond the
already-published frozen identity.