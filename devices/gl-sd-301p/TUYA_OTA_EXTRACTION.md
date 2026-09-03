# GL-SD-301P Tuya OTA extraction — operator runbook

This procedure temporarily moves the installed `LivingRoomCircleLightDimmer`
(`0xa4c13850cfcdb3a4`) from Zigbee2MQTT to a Tuya Zigbee gateway, reads vendor
firmware metadata, downloads any exposed OTA file, then returns the dimmer to
its original Zigbee2MQTT network.

The Python tool is deliberately **read-only for Tuya firmware operations**. It
contains no Tuya POST call that starts an OTA update.

## What the tool automates

- snapshots the target's current Zigbee2MQTT `database.db` record;
- preserves EP11 bindings, configured reporting and group membership;
- sanitizes configuration output so MQTT/network secrets are not written;
- authenticates to Tuya Cloud using `tuya-connector-python`;
- waits for a GL-SD device to appear after physical pairing;
- queries both current and legacy firmware-information GET endpoints;
- extracts firmware URLs if Tuya exposes them;
- immediately downloads signed/expiring URLs;
- calculates MD5/SHA-256/SHA-512;
- parses a Zigbee OTA header and Telink payload identity when present;
- optionally starts a `tshark` PCAP capture for encrypted gateway traffic evidence;
- after rejoining Z2M, compares live groups/binds/reporting to the original snapshot;
- generates **review-only** MQTT restoration commands for anything missing.

## Laptop prerequisites

Windows PowerShell:

```powershell
cd <local checkout of analienx/gledopto>
py -m venv .venv-tuya
.\.venv-tuya\Scripts\Activate.ps1
python -m pip install -U pip
pip install -r tools\requirements-tuya-ota.txt
python tools\tuya_glsd_migrate.py doctor
```

Required:

- Python 3.10+;
- OpenSSH client with the existing `ssh ha` alias working;
- `tuya-connector-python`;
- `PyYAML`.

Optional:

- Wireshark/TShark. This is useful as evidence only; modern Tuya traffic is TLS
  encrypted, so the Cloud API is the primary extractor.

## Tuya developer setup

Before the migration, create/link a Tuya Cloud project for the same Smart Life
account that owns the Tuya gateway.

Set the credentials **only in the local PowerShell environment**. Never post
these values to GitHub or chat:

```powershell
$env:TUYA_ACCESS_ID = '<project Access ID>'
$env:TUYA_ACCESS_KEY = '<project Access Secret>'
```

Europe is the default endpoint. For another data center pass `--region us`,
`--region cn`, or `--region in`. `TUYA_ENDPOINT` can override the endpoint.

## Recommended one-command workflow

```powershell
python tools\tuya_glsd_migrate.py guided --region eu
```

The script performs these phases:

### Phase 1 — pre-migration snapshot

No physical action is needed. It reads `/config/zigbee2mqtt/database.db` and
relevant non-secret configuration through `ssh ha` and writes a local session
directory.

Do not continue if the snapshot fails.

### Phase 2 — move GL-SD-301P to Tuya

When prompted:

1. In Smart Life open the Tuya Zigbee gateway and enable adding a Zigbee
   sub-device.
2. Physically factory-reset the installed GL-SD-301P with **five quick RESET
   presses**.
3. Pair it to the Tuya gateway.
4. Do **not** approve or start a firmware update in Smart Life.

The Python tool polls Tuya Cloud. When it identifies the dimmer it issues only:

```text
GET /v1.1/iot-03/devices/{device_id}
GET /v2.0/cloud/thing/{device_id}/firmware
GET /v1.0/iot-03/devices/{device_id}/upgrade-infos
```

If automatic device discovery is unavailable in the project, copy the GL-SD
**child device ID** from Tuya Developer Platform and run:

```powershell
python tools\tuya_glsd_migrate.py tuya-watch --region eu --device-id '<DEVICE_ID>'
```

If Tuya returns a firmware URL, the tool downloads it immediately because the
URL can be signed and short-lived.

### Optional gateway PCAP

If the Tuya gateway's traffic traverses the laptop and TShark is installed,
identify the capture interface with:

```powershell
tshark -D
```

Then:

```powershell
python tools\tuya_glsd_migrate.py guided --region eu --pcap-interface 4
```

The resulting `tuya_gateway_capture.pcapng` may show DNS/TLS endpoints and
request timing. Do not expect it to reveal the HTTPS firmware URL by itself.

### Phase 3 — return the dimmer to Zigbee2MQTT

After extraction finishes:

1. physically factory-reset the GL-SD-301P again;
2. enable Zigbee2MQTT permit-join;
3. rejoin the dimmer to the original Zigbee network;
4. wait for an interview success;
5. press Enter in the guided tool.

The script then compares live state to the original snapshot. It checks:

- same IEEE address;
- EP11 bindings;
- Group 110 / other group membership;
- configured reporting.

If anything is missing, it writes:

```text
restore_diff.json
restore_plan.json
restore_commands.REVIEW_BEFORE_RUN.ps1
```

The PowerShell file is **not executed automatically**. Supervisor/Executor must
review the plan before applying MQTT mutations.

## Non-interactive commands

Snapshot only:

```powershell
python tools\tuya_glsd_migrate.py snapshot-z2m --ssh ha
```

Wait/query Tuya:

```powershell
python tools\tuya_glsd_migrate.py tuya-watch --region eu --watch
```

Post-rejoin comparison:

```powershell
python tools\tuya_glsd_migrate.py restore-check .\glsd-tuya-session-...\z2m_snapshot.json
```

Generate a restoration plan:

```powershell
python tools\tuya_glsd_migrate.py restore-plan .\glsd-tuya-session-...\z2m_snapshot.json
```

## Expected extraction artifacts

Successful Cloud extraction produces a local directory containing some or all
of:

```text
tuya_devices.sanitized.json
tuya_firmware_metadata.sanitized.json
firmware_urls.json
<downloaded vendor firmware>
downloads.json
tuya_gateway_capture.pcapng       # only when requested
```

For a Zigbee OTA file, `downloads.json` records header fields including:

```text
manufacturer_code_hex
image_type_hex
file_version_hex
header_string
total_image_size
```

For a Telink-style payload it also checks the app version, Telink boot marker,
manufacturer/image type and application-size field.

## Hard stop conditions

Stop and return evidence instead of improvising if:

- the pre-migration Z2M snapshot cannot find IEEE `0xa4c13850cfcdb3a4`;
- Tuya Cloud lists multiple ambiguous GLEDOPTO devices and no explicit device ID
  is available;
- any Tuya API unexpectedly requires an update-start action;
- downloaded firmware has an unexpected manufacturer/image identity;
- the device rejoins Z2M with a different IEEE;
- restoration differences cannot be mapped safely to known targets/groups.

Do not flash any downloaded image during this procedure.
