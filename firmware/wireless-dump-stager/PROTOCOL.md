# GL-SD wireless dump protocol v1

This protocol is intentionally **read-only**. There is no flash-write, erase,
boot-flag, OTA-start, or factory-data mutation command in v1.

Transport: manufacturer-specific ZCL cluster `0xFC00` on the temporary stager.
The stager is expected to remain on the existing Zigbee network using the
preserved Telink NV area.

| ID | Direction | Meaning |
|---|---|---|
| `0x00 HELLO` | host→device | Request protocol info. |
| `0x01 INFO` | device→host | Protocol version, active bank, source bank, readable length, max chunk. |
| `0x10 READ_REQUEST` | host→device | `{stream_id, offset, length}`; offset relative to source bank. |
| `0x11 DATA` | device→host | `{stream_id, offset, length, data, crc32(data)}`. |
| `0x12 FINISH` | device→host | Final byte count and SHA-256 if available. |
| `0x7F ABORT` | either | End transfer without state mutation. |

The host drives the transfer. This avoids a device-side flood, supports retry,
and lets the receiver resume only missing offsets after RF loss.

## Source-bank rule

For the 512 KiB B85/TLSR8258 layout under investigation:

- bank A starts at `0x00000`
- bank B starts at `0x40000`
- the stager MUST determine its own active bank before choosing the source
- source = the other bank
- reads are capped to the application-bank range proved offline
- MAC/NV/factory/calibration sectors are outside the application-dump scope

The first target is the previous application image, not a full device clone.

## Integrity

Every DATA frame carries CRC32 of its chunk. The host also computes SHA-256 of
the reassembled image. Duplicate chunks are accepted only if bytes are exactly
identical. A conflicting duplicate is a hard failure.
