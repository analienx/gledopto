# Zigbee2MQTT integration — GL-SD wireless dump

Status: **implemented and offline-tested; NOT authorized for live production loading**.

Pinned source contracts used for this integration:

- Zigbee2MQTT `2.14.0`
- zigbee-herdsman `10.9.1`
- zigbee-herdsman-converters `26.103.0`

The integration is intentionally split into:

- `glsd_wireless_dump_contract.mjs` — pure request validation, no I/O;
- `glsd_wireless_dump_extension.mjs` — Z2M external extension using
  herdsman's normal `Endpoint.command()` request/response waiter;
- `tools/glsd_z2m_bridge.py` — matching Python JSON codec;
- `tools/glsd_z2m_dump.py` — guarded PING/INFO/READ dump orchestrator.

## Hard target lock

The external extension and Python bridge both compile in exactly:

```text
IEEE     0xa4c13850cfcdb3a4
endpoint 11
cluster  0xFC00
```

A caller cannot redirect the integration with a friendly name, another IEEE,
or another endpoint. Every RPC request must repeat the exact target IEEE.

## MQTT RPC

Relative request topic:

```text
bridge/request/glsd_wireless_dump
```

Relative response topic:

```text
bridge/response/glsd_wireless_dump
```

Zigbee2MQTT prefixes these with its configured MQTT base topic.

Request shape:

```json
{
  "protocol_version": 1,
  "request_id": "caller-generated-correlation-id",
  "target": "0xa4c13850cfcdb3a4",
  "op": "read",
  "payload_hex": "...",
  "timeout_ms": 10000
}
```

Only these operations exist:

```text
ping   payload 5 bytes
info   payload 0 bytes
read   payload 13 bytes
abort  payload 0 bytes
```

There is no write, erase, reset, binding, group, network-management, OTA, or
rollback operation in this bridge.

## Correlation layers

The live path intentionally performs multiple independent checks:

1. herdsman `Endpoint.command()` waits for the configured response command with
   matching address/endpoint/cluster/ZCL transaction sequence number;
2. the stager READ response echoes `session_id + seq + offset + length`;
3. `GuardedPersistentDump` requires that exact four-tuple to match its single
   outstanding persisted request;
4. DATA CRC32 is validated before persistence;
5. persisted chunk SHA-256 + CRC journal is revalidated on resume;
6. finalization reconstructs exactly byte `+0x08` and requires the Telink
   application xcrc32 to validate.

A transport retry allocates and fsyncs a strictly higher protocol sequence
before the replacement READ can be emitted. A late reply to the old request is
therefore stale even if the network delivers it later.

## External-extension implementation notes

At `start()`, the extension adds a **host-only** custom cluster definition to
herdsman so `0xFC00` raw-buffer commands can be serialized and parsed. This
registration itself sends no Zigbee frame.

Actual Zigbee I/O occurs only after an explicit request is received on the MQTT
RPC topic. The extension serializes one operation at a time and uses
`sendPolicy: immediate`; it does not alter bindings/reporting/device options.

Do not load this extension on the production Zigbee2MQTT instance until the
Supervisor explicitly opens the live-stager gate.

## CLI

The live CLI needs paho-mqtt:

```text
pip install -r tools/requirements-wireless-dump.txt
```

Credentials should be supplied through the environment rather than command-line
password text. The default password variable is `MQTT_PASSWORD`.

Example syntax is intentionally documentation-only; it is **not live approval**:

```text
python tools/glsd_z2m_dump.py STATE_DIR \
  --broker BROKER --base-topic zigbee2mqtt --username USER
```

On every invocation the runner performs a fresh PING nonce check followed by a
fresh fail-closed INFO geometry check. An existing state directory can resume
only if the INFO/session/geometry/IEEE binding is identical.

## OTA index builder

`tools/make_glsd_stager_index.py` only creates an offline one-entry Z2M index.
It does not serve or schedule an image. It rejects anything that is not:

- outer `0x124F / 0x1416`;
- fileVersion strictly greater than observed stock `0x26013001`;
- a boot-valid Telink application;
- inner identity consistent with the generated target image;
- locked to model `GL-SD-301P`, manufacturer `GLEDOPTO`, hwVersion `2`.

The deliberately invalid-CRC acceptance probe is rejected by this path.

## Still-blocked device-side items

None of this host/integration work proves the production module's exact current
silicon/flash revision or supplies the missing TC32 compiler/chip support
package. A real bootable stager build remains blocked until those target facts
and toolchain artifacts are proven. `LIVE_CUSTOM_OTA` therefore remains NO-GO.
