# GL-SD wireless dump protocol v1

Status: **device-independent core + host/Z2M integration implemented and offline-tested; no live production OTA authorized**.

The extraction protocol is intentionally read-only. There is no command in the
v1 extraction build that can erase/write flash, change a boot marker, factory
reset, alter bindings/groups, or mutate Zigbee network/factory/MAC/calibration
storage.

Transport target: cluster-specific ZCL on private cluster `0xFC00`, unicast only.
The current host bridge is additionally compile-time locked to IEEE
`0xa4c13850cfcdb3a4`, endpoint `11`.

## Constants

```text
protocol_version = 1
max_flash_data   = 48 bytes
bank A           = 0x00000
bank B           = 0x40000
application cap  = < 0x34000
512-KiB flash    = 0x80000
```

The 48-byte cap matches Telink's own `OTA_IMAGE_MAX_DATA_SIZE` and is a hard
maximum, not a throughput target.

## Commands

Implemented v1 request IDs:

```text
0x00 PING
0x01 INFO
0x02 READ
0x03 ABORT
```

Responses use `request | 0x80`.

`0x04 STATUS` remains reserved in source constants but is deliberately
**unsupported by the dispatcher**. It must not be advertised as an implemented
wire command until a concrete read-only response contract and tests are added.

Rollback commands are deliberately **not** part of the extraction build. They
belong to a separately gated canary-only rollback build after the transactional
journal has been fault-injection tested.

## PING

Request:

```text
protocol_version : u8
host_nonce       : u32 LE
```

Response:

```text
protocol_version : u8
host_nonce       : u32 LE
tager_build_id   : u32 LE
session_id       : u32 LE
```

The host requires the echoed nonce and protocol version, then cross-checks the
PING build/session IDs against a fresh INFO response. This rejects stale or
cross-session responses before any READ is issued.

## INFO

INFO exposes:

```text
protocol_version
stager_build_id
session_id
flash_jedec_id
flash_size
bank_a_base
bank_b_base
bank_a_flag32
bank_b_flag32
inferred_stager_base
inferred_old_base
old_declared_size
old_tail_crc32
old_reconstructed_crc_valid
allowed_read_start
allowed_read_length
journal_state
rollback_compiled
```

Host-side `validate_info()` fails closed unless:

- protocol version is exact;
- flash size is exactly the proved 512-KiB profile;
- banks are exactly `0x00000` and `0x40000`;
- old bank is opposite the executing stager bank;
- executing stager marker is valid and old bank marker has the expected Telink
  post-OTA invalidated shape;
- declared old application size is `>=0x20` and `<0x34000`;
- read range begins at relative offset zero and equals the declared app size;
- the stager already verified the reconstructed old-bank Telink CRC.

Resume state is bound to the complete validated INFO geometry, target IEEE,
protocol/build/session identity and chunk size.

## READ

Request wire fields, little-endian:

```text
session_id : u32
seq        : u32
offset     : u32   # relative to old application base
length     : u8    # 1..48
```

Response:

```text
session_id : u32
seq        : u32
offset     : u32
length     : u8
data       : u8[length]
crc32      : u32   # standard finalized CRC32 of data only
status     : u8    # zero = success
```

Only one READ may be outstanding in the guarded live v1 path.

A sequence number is fsynced to guarded host state **before** the corresponding
request may be emitted. Sequences are strictly increasing across retries and
process restarts. A retry therefore replaces the pending request with a higher
sequence; a late response to the previous request cannot match.

The stager rejects any request outside the old application's declared size or
the proved application slot. NV/MAC/factory/calibration regions are never
readable through this protocol.

## ABORT

ABORT carries no payload and is a read-only transport no-op in v1. It does not
erase data, change a marker, reset the device or alter network state.

## Live Z2M correlation

The version-pinned Zigbee2MQTT bridge uses herdsman's normal
`Endpoint.command()` response waiter. This supplies the first correlation
layer: address/endpoint/cluster/response-command/ZCL-TSN matching.

The protocol supplies an independent second layer: the guarded host requires
`session_id + seq + offset + length` to match its single persisted pending READ
exactly before a DATA frame is ingested.

The bridge itself exposes only PING/INFO/READ/ABORT and is compile-time locked
to the target IEEE, endpoint and cluster.

## Host persistence

Lower persistent store:

```text
session.json
raw_after_ota.bin.part
received.bitmap.json
chunk_checksums.jsonl
raw_after_ota.bin
reconstructed_stock.bin
validation.json
```

Live-session guard:

```text
guarded_session.json
```

For each accepted chunk the lower store persists, in order:

1. bytes into the partial image + `fsync`;
2. SHA-256/CRC/offset/length journal row + `fsync`;
3. received bitmap via atomic replace.

On resume every bitmap-committed chunk is reread and must match its fsynced
journal SHA-256 and CRC32. Journal-only rows are safe after a crash before
bitmap commit. A crash after the lower store commits but before the caller
returns is recoverable because received state is derived from the lower store;
protocol sequence state was already persisted before request transmission.

Duplicate chunks are accepted by the lower offline primitive only when the bytes
on disk match exactly. In the guarded live layer a stale duplicate with no
current pending request is rejected.

## Finalization

Finalization requires every chunk and no outstanding READ. It reconstructs only
relative byte `+0x08` from `0x00` to `0x4B`, requires that to be the **only**
reconstruction diff, then validates:

```text
fw[6:8] == 5D 02
u32le(fw[8:12]) == 0x544C4E4B
0 < declared_size < 0x34000
Telink xcrc32(fw[0:N-4], init=0xFFFFFFFF, no final XOR)
    == u32le(fw[N-4:N])
```

Raw and reconstructed SHA-256 values are recorded separately. Raw firmware
artifacts remain local/private and must not be committed.
