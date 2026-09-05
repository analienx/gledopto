# GL-SD wireless dump protocol v1

Status: **offline/host implementation; no live production OTA authorized**.

The extraction protocol is intentionally read-only. There is no command in the
v1 extraction build that can erase/write flash, change a boot marker, factory
reset, or mutate Zigbee network/factory/MAC/calibration storage.

Transport target: cluster-specific ZCL on private cluster `0xFC00`, unicast only.

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

```text
0x00 PING
0x01 INFO
0x02 READ
0x03 ABORT
0x04 STATUS
```

Responses use `request | 0x80` when implemented.

Rollback commands are deliberately **not** part of the extraction build. They
belong to a separately gated canary-only rollback build after the transactional
journal has been fault-injection tested.

## PING

Request carries protocol version + host nonce. Response echoes the nonce and
adds stager build/session identity. The host must use this to reject stale
responses from another session.

## INFO

INFO must expose at least:

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

- flash size is exactly the proved 512-KiB profile;
- banks are exactly `0x00000` and `0x40000`;
- old bank is opposite the executing stager bank;
- declared old application size is `>=0x1C` and `<0x34000`;
- read range begins at relative offset zero and equals the declared app size;
- the stager already verified the reconstructed old-bank Telink CRC.

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
crc32      : u32   # CRC32 of data only
status     : u8    # zero = success
```

Only one outstanding READ is expected in v1.

The stager must reject any request whose range is outside the old application's
declared size or outside the proved application slot. NV/MAC/factory/calibration
regions are never readable through this protocol.

## Host persistence

The host persists:

```text
session.json
raw_after_ota.bin.part
received.bitmap.json
chunk_checksums.jsonl
raw_after_ota.bin
reconstructed_stock.bin
validation.json
```

A chunk is marked received only after its bytes have been written and flushed.
Duplicate chunks are accepted only when the bytes on disk match exactly.

Finalization requires every chunk, reconstructs only relative byte `+0x08`
from `0x00` to `0x4B`, requires that to be the **only** reconstruction diff,
then validates:

```text
fw[6:8] == 5D 02
u32le(fw[8:12]) == 0x544C4E4B
0 < declared_size < 0x34000
Telink xcrc32(fw[0:N-4], init=0xFFFFFFFF, no final XOR)
    == u32le(fw[N-4:N])
```

Raw and reconstructed SHA-256 values are recorded separately. Raw firmware
artifacts remain local/private and must not be committed.
