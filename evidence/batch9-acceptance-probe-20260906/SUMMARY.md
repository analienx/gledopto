# Batch 9 — CRC-reject acceptance probe on production GL-SD-301P — 2026-09-06

Probe/tooling head: `f2c4fb070fa3c7f501bfdfba2a2225b94c9ae56b`
(worktree `Temp/gledopto-probe-f2c4f`, detached, clean; branch `research/wireless-dump-stager`)
CI run 34042571896 on that head: SUCCESS — all three jobs
(Python 3.11 + native GCC + Z2M contract; Python 3.14 + native GCC + Z2M contract;
pinned Telink TC32 + TLSR8258 bank-neutral link proof).

## Probe candidate (generated fresh from this exact head)

Built with `tools/make_ota_acceptance_probe.py --unsafe-create-probe`
(default version `0x26013002`, default payload 512 B):

```text
size      574 bytes
sha256    46a92927ee295a564d87d77711db1b2c4fbd0e8cc04be216d0cd99e6d4a043e1
sha512    06e16a87c1baa0072a553e6467f22770965ea1042f8f5c2c82855d8a487e210e5dc4dbe71b2a92e39dcd9e458995c47090434dfbfa3f3c79f85ee81c850a3468
header    "GLSD CRC-REJECT PROBE - NO BOOT", mfg 0x124F, imageType 0x1416
expected_telink_xcrc32 0x3118ED10 / stored (bad) 0x3118ED11  (one-bit defect)
```

Independent parse at the same head (`tools/telink_ota_forensics.py --json`):

```text
5D02 preamble                    valid
startup marker 0x544C4E4B        valid
inner/outer mfg-image-version    consistent
payload declared size            valid (total_size_matches_header=true)
Telink xcrc32                    INVALID (telink_crc_valid=false)
forensics reason                 telink_crc_mismatch
```

Matches the supervisor-required parse exactly. Byte copies of the candidate and its
sidecar were archived on the HA host under `/config/zigbee2mqtt/gledopto_probe/acceptance/`
(host SHA-256 of the served file re-verified identical to the build before the request).

## Target identity (pre and post)

`0xa4c13850cfcdb3a4` / `LivingRoomCircleLightDimmer` / GL-SD-301P / GLEDOPTO /
swBuildId 20651203 / dateCode 20240704 / hwVersion 2 / nwkAddr 17901 / ep 11 + 242 (genOta out 25).

Pre-checks before the request (read-only):

```text
frozen tuple re-read               fresh ZCL get (state/brightness) + device object in payload — unchanged
OTA version check (no URL)         refused by Z2M: device has no upstream OTA definition (expected; URL path used instead)
automatic OTA checks               global Z2M flag disable_automatic_update_check=false, but this device has no
                                   upstream OTA definition → the periodic checker never targets it; no OTA
                                   activity for this device in logs before the authorized request
scheduled OTA                      none (today's only scheduled item — an unrelated TS011F plug — was unscheduled at 15:09)
global override index              none configured (no zigbee_ota_override_index_location); /config/zigbee2mqtt/ota/ empty
```

## Execution (one-use explicit URL, per-device request only)

```text
request   {"id":"0xa4c13850cfcdb3a4","url":"http://127.0.0.1:18900/probe-<one-use-token>/glsd-probe.ota"}
server    one-use HTTP server inside the Z2M container, 127.0.0.1:18900,
          bind + self-fetch verified BEFORE the request; serves the file exactly once (574 B) then 404s
timeline  18:19:40 OTA updating 'LivingRoomCircleLightDimmer' to latest firmware
          18:19:50 device state: update {progress:0, remaining:2, state:"updating"}   (transfer began)
          18:19:50 herdsman: OTA update of '0xa4c13850cfcdb3a4' estimated at 1.5 seconds (6 chunks)
          18:20:03 device state: update {state:"available"}                            (stock remains active)
          18:20:03 FAILED — OTA update of 0xa4c13850cfcdb3a4 failed with reason: INVALID_IMAGE
```

## Expected PASS outcome — observed

```text
CANDIDATE_REJECTED_INVALID_IMAGE     = PASS (device-side Telink validation rejected the image)
STOCK_REMAINS_ACTIVE                 = PASS (update state back to "available"; device kept running stock)
FILE_VERSION_UNCHANGED               = PASS (device object in every publish: swBuildID 20651203, dateCode 20240704, hwVersion 2)
NO_REBOOT_INTO_CANDIDATE             = PASS (no reset/rejoin/leave; networkAddress 17901 constant; linkquality 136–148)
NORMAL_CONTROL_VERIFIED              = PASS (post-probe: OFF→ON→brightness 64 (25%)→191 (75%) all correct)
NO_REPAIR_REQUIRED                   = PASS (device present, responsive, reporting; interview state intact)
```

## Attempt-1 note (invalid attempt, no mutation)

A first attempt was invalidated before any device interaction: the one-use server hit
`EADDRINUSE` on 127.0.0.1:8899 (a stale `ota-server.js` from the earlier bseed experiments is
still listening inside the container). Z2M never fetched the probe file (server log empty) and
the device (which was off at that moment) did not respond: "Device didn't respond to OTA
request". No bytes reached the device. Attempt 2 bound a free port (18900), verified the bind
and a self-fetch of the exact token path before publishing the request, and produced the valid
result above. Hygiene note for the host: the stale 8899 listener should be cleaned up later
(separate change, not part of this probe).

## Post-run state

```text
LIVE_CUSTOM_OTA                     = NO_GO   (unchanged)
REAL_BOOTABLE_STAGER                = NO_GO   (unchanged)
PRIVATE_0xFC00_EXTRACTION           = NO_GO   (unchanged)
RECONSTRUCTED_STOCK_OTA             = NO_GO   (unchanged)
ACCEPTANCE_PROBE                    = DONE — candidate rejected by the device as INVALID_IMAGE
STOCK_DEVICE                        = healthy, stock tuple unchanged, normal control verified
NO_FURTHER_LIVE_MUTATION            = none performed after the probe
```

Raw, unsanitized artifacts (probe binary, sidecar, scripts, JSON results, container log
extracts) remain on the HA host under `/config/zigbee2mqtt/gledopto_probe/acceptance/`.
This summary and the issue report contain no credentials, keys, or addresses beyond the
already-published frozen identity.
