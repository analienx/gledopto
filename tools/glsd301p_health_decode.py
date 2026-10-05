#!/usr/bin/env python3
"""Decode GL-SD-301P RAM health snapshots (Basic cluster 0xFF10, v2, 48 B LE).

Layout (offset, type, meaning):
    0  u8   version (2)
    1  u8   network: 0 disconnected, 1 joined, 2 joining
    2  u8   flags (bit 0 runtime ready, 1 fault latched, 2 actual MAC
                rxOnWhenIdle, 3 OFF pending, 4 normal frame pending,
                5 UART busy, 6 IO event registered, 7 boot OFF TX complete)
    3  u8   latest BDB/startup status
    4  u32  uptime ms, modulo uint32
    8  u32  last IO age; 0xFFFFFFFF before first service
   12  u32  oldest pending/in-flight UART age; 0 when idle
   16  u32  maximum IO gap
   20  u32  UART deadline faults
   24  u32  timer registration faults
   28  u32  parent-loss count
   32  u32  rejoin-start count
   36  u32  rejoin-failure count
   40  u32  rejoin-success count
   44  u16  SDK exception line
   46  u8   SDK exception code
   47  u8   reserved, zero
"""
from __future__ import annotations

import argparse
import json
import struct
import sys

HEALTH_SIZE = 48
HEALTH_VERSION = 2

NET_STATES = {0: "disconnected", 1: "joined", 2: "joining"}

FLAG_NAMES = (
    "runtime_ready",
    "fault_latched",
    "mac_rx_on_idle",
    "off_pending",
    "normal_pending",
    "uart_busy",
    "io_registered",
    "boot_off_complete",
)


class HealthDecodeError(ValueError):
    pass


def decode_health_snapshot(data: bytes) -> dict:
    if len(data) != HEALTH_SIZE:
        raise HealthDecodeError(
            f"health snapshot must be {HEALTH_SIZE} bytes, got {len(data)}"
        )
    version = data[0]
    if version != HEALTH_VERSION:
        raise HealthDecodeError(
            f"unsupported health version {version}, want {HEALTH_VERSION}"
        )
    net_raw = data[1]
    if net_raw not in NET_STATES:
        raise HealthDecodeError(f"unknown network state {net_raw}")
    flags_raw = data[2]
    words = struct.unpack_from("<10I", data, 4)
    exc_line = struct.unpack_from("<H", data, 44)[0]
    return {
        "version": version,
        "network": NET_STATES[net_raw],
        "network_raw": net_raw,
        "flags": {
            name: bool(flags_raw & (1 << bit))
            for bit, name in enumerate(FLAG_NAMES)
        },
        "flags_raw": flags_raw,
        "bdb_status": data[3],
        "uptime_ms": words[0],
        "last_io_age_ms": words[1],
        "uart_age_ms": words[2],
        "io_max_gap_ms": words[3],
        "uart_deadline_faults": words[4],
        "timer_reg_faults": words[5],
        "parent_losses": words[6],
        "rejoin_starts": words[7],
        "rejoin_failures": words[8],
        "rejoin_successes": words[9],
        "exc_line": exc_line,
        "exc_code": data[46],
        "reserved": data[47],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Decode a GL-SD-301P v2 health snapshot."
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("hex", nargs="?",
                       help="48-byte snapshot as hex")
    group.add_argument("--binary-file", metavar="PATH",
                       help="read the 48 raw bytes from a file")
    args = parser.parse_args(argv)
    try:
        if args.binary_file:
            with open(args.binary_file, "rb") as fh:
                raw = fh.read()
        else:
            raw = bytes.fromhex(args.hex)
        decoded = decode_health_snapshot(raw)
    except (ValueError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(decoded, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
