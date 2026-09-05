# Wireless Dump Protocol Specification (v1)

## Overview
This protocol defines the host-to-stager communication for extracting the inactive flash bank over Zigbee.
It uses a manufacturer-specific cluster to avoid interfering with standard ZCL clusters.

## Zigbee Parameters
- **Endpoint:** 11 (or dedicated private endpoint)
- **Cluster ID:** 0xFC00 (Manufacturer Specific)
- **Direction:** Client (Host/Coordinator) to Server (Stager)
- **Profile:** 0x0104 (HA)

## Commands

### 0x00 INFO_REQ / 0x01 INFO_RSP
Host requests stager identity and flash layout.
**INFO_RSP Payload:**
- `protocol_version` (u8)
- `active_bank` (u8)
- `inactive_bank` (u8)
- `flash_size` (u32)
- `app_size_guess` (u32)
- `stager_version` (u16)

### 0x10 READ_REQ / 0x11 READ_RSP
Host requests a chunk of flash memory.
**READ_REQ Payload:**
- `region` (u8): 0=inactive_app, 1=active_app, 2=factory, 3=mac, 4=nv
- `offset` (u32): byte offset within the region
- `length` (u8): requested bytes (max 64)
- `sequence` (u16): request sequence number

**READ_RSP Payload:**
- `sequence` (u16): echoes request sequence
- `region` (u8)
- `offset` (u32)
- `length` (u8): actual bytes returned
- `data` (bytes): payload
- `crc32` (u32): Telink-style reflected CRC32 over `data`

### 0x20 HASH_REQ / 0x21 HASH_RSP
Host requests SHA256 of a region (optional, for final verification).

### 0x30 ABORT
Stager should immediately cease dump operations and prepare for reboot/OTA return.

## Safety Invariants
1. Stager MUST refuse `region` > 0 unless explicitly unlocked.
2. Stager MUST NOT erase any flash region during dump.
3. Host MUST verify `crc32` on every chunk.
4. Host MUST track `sequence` to detect dropped frames.
