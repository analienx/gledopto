#!/usr/bin/env python3
"""Wire framing and strict host-side validation for GL-SD dump protocol v1.

This module does not talk to Zigbee hardware and exposes no flash mutation.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import json
from dataclasses import dataclass, asdict
from pathlib import Path
import struct

PROTOCOL_VERSION = 1
MAX_CHUNK = 48

CMD_PING = 0x00
CMD_INFO = 0x01
CMD_READ = 0x02
CMD_ABORT = 0x03
CMD_STATUS = 0x04
RSP_BIT = 0x80

BANK_A_BASE = 0x00000
BANK_B_BASE = 0x40000
FLASH_SIZE_512K = 0x80000
APP_LIMIT = 0x34000
TELINK_STARTUP_FLAG = 0x544C4E4B
TELINK_INVALIDATED_FLAG = 0x544C4E00

READ_REQUEST = struct.Struct("<IIIB")  # session_id, seq, offset, length
DATA_PREFIX = struct.Struct("<IIIB")   # session_id, seq, offset, length
DATA_SUFFIX = struct.Struct("<IB")     # crc32(data), status
INFO_RESPONSE = struct.Struct("<BIIIIIIIIIIIIBIIBB")


@dataclass(frozen=True)
class StagerInfo:
    protocol_version: int
    stager_build_id: int
    session_id: int
    flash_jedec_id: int
    flash_size: int
    bank_a_base: int
    bank_b_base: int
    bank_a_flag32: int
    bank_b_flag32: int
    inferred_stager_base: int
    inferred_old_base: int
    old_declared_size: int
    old_tail_crc32: int
    old_reconstructed_crc_valid: bool
    allowed_read_start: int
    allowed_read_length: int
    journal_state: int
    rollback_compiled: bool

    @classmethod
    def decode(cls, raw: bytes) -> "StagerInfo":
        if len(raw) != INFO_RESPONSE.size:
            raise ValueError(f"invalid INFO response length {len(raw)}; expected {INFO_RESPONSE.size}")
        values = INFO_RESPONSE.unpack(raw)
        obj = cls(
            protocol_version=values[0],
            stager_build_id=values[1],
            session_id=values[2],
            flash_jedec_id=values[3],
            flash_size=values[4],
            bank_a_base=values[5],
            bank_b_base=values[6],
            bank_a_flag32=values[7],
            bank_b_flag32=values[8],
            inferred_stager_base=values[9],
            inferred_old_base=values[10],
            old_declared_size=values[11],
            old_tail_crc32=values[12],
            old_reconstructed_crc_valid=bool(values[13]),
            allowed_read_start=values[14],
            allowed_read_length=values[15],
            journal_state=values[16],
            rollback_compiled=bool(values[17]),
        )
        validate_info(obj)
        return obj


def validate_info(info: StagerInfo) -> None:
    """Fail closed unless INFO matches the proven 512-KiB application-bank model."""
    if info.protocol_version != PROTOCOL_VERSION:
        raise ValueError("protocol version mismatch")
    if info.flash_size != FLASH_SIZE_512K:
        raise ValueError(f"unexpected flash size 0x{info.flash_size:X}")
    if (info.bank_a_base, info.bank_b_base) != (BANK_A_BASE, BANK_B_BASE):
        raise ValueError("unexpected bank geometry")
    if info.inferred_stager_base not in (BANK_A_BASE, BANK_B_BASE):
        raise ValueError("invalid stager bank")
    expected_old = (
        BANK_B_BASE if info.inferred_stager_base == BANK_A_BASE else BANK_A_BASE
    )
    if info.inferred_old_base != expected_old:
        raise ValueError("old bank is not opposite stager bank")
    if not (0x20 <= info.old_declared_size < APP_LIMIT):
        raise ValueError("old application declared size outside safe range")
    stager_flag = (
        info.bank_a_flag32
        if info.inferred_stager_base == BANK_A_BASE
        else info.bank_b_flag32
    )
    old_flag = (
        info.bank_a_flag32
        if info.inferred_old_base == BANK_A_BASE
        else info.bank_b_flag32
    )
    if stager_flag != TELINK_STARTUP_FLAG:
        raise ValueError("executing stager bank does not have a valid Telink startup flag")
    if old_flag != TELINK_INVALIDATED_FLAG:
        raise ValueError("old bank is not in the expected post-OTA invalidated state")
    if info.allowed_read_start != 0:
        raise ValueError("v1 only permits reads relative to old-bank offset zero")
    if info.allowed_read_length != info.old_declared_size:
        raise ValueError("allowed read length must equal old declared size")
    if not info.old_reconstructed_crc_valid:
        raise ValueError("stager did not prove reconstructed old-bank CRC")
    if info.allowed_read_length > APP_LIMIT:
        raise ValueError("read range exceeds application slot")


@dataclass(frozen=True)
class ReadRequest:
    session_id: int
    seq: int
    offset: int
    length: int

    def encode(self) -> bytes:
        if not (1 <= self.length <= MAX_CHUNK):
            raise ValueError("READ length out of range")
        if self.offset < 0 or self.offset > 0xFFFFFFFF:
            raise ValueError("READ offset out of range")
        return READ_REQUEST.pack(
            self.session_id & 0xFFFFFFFF,
            self.seq & 0xFFFFFFFF,
            self.offset,
            self.length,
        )

    @classmethod
    def decode(cls, raw: bytes) -> "ReadRequest":
        if len(raw) != READ_REQUEST.size:
            raise ValueError("invalid READ request length")
        sid, seq, off, length = READ_REQUEST.unpack(raw)
        if not (1 <= length <= MAX_CHUNK):
            raise ValueError("READ length out of range")
        return cls(sid, seq, off, length)


@dataclass(frozen=True)
class DataFrame:
    session_id: int
    seq: int
    offset: int
    data: bytes
    status: int = 0
    crc32: int | None = None

    def encode(self) -> bytes:
        if not (1 <= len(self.data) <= MAX_CHUNK):
            raise ValueError("chunk length out of range")
        crc = binascii.crc32(self.data) & 0xFFFFFFFF
        return (
            DATA_PREFIX.pack(
                self.session_id & 0xFFFFFFFF,
                self.seq & 0xFFFFFFFF,
                self.offset,
                len(self.data),
            )
            + self.data
            + DATA_SUFFIX.pack(crc, self.status & 0xFF)
        )

    @classmethod
    def decode(cls, raw: bytes) -> "DataFrame":
        if len(raw) < DATA_PREFIX.size + DATA_SUFFIX.size:
            raise ValueError("short DATA frame")
        sid, seq, off, n = DATA_PREFIX.unpack_from(raw)
        expected = DATA_PREFIX.size + n + DATA_SUFFIX.size
        if n < 1 or n > MAX_CHUNK or len(raw) != expected:
            raise ValueError("invalid DATA frame length")
        data = raw[DATA_PREFIX.size : DATA_PREFIX.size + n]
        got_crc, status = DATA_SUFFIX.unpack_from(raw, DATA_PREFIX.size + n)
        want_crc = binascii.crc32(data) & 0xFFFFFFFF
        if got_crc != want_crc:
            raise ValueError(
                f"chunk CRC mismatch got=0x{got_crc:08X} want=0x{want_crc:08X}"
            )
        return cls(sid, seq, off, data, status, got_crc)


class Reassembler:
    def __init__(
        self, session_id: int, total_len: int, chunk_size: int = MAX_CHUNK
    ):
        if total_len <= 0 or total_len >= APP_LIMIT:
            raise ValueError("total_len outside application slot")
        if chunk_size < 1 or chunk_size > MAX_CHUNK:
            raise ValueError("bad chunk size")
        self.session_id = session_id
        self.total_len = total_len
        self.chunk_size = chunk_size
        self.buf = bytearray(total_len)
        self.received: set[int] = set()
        self.seq_by_index: dict[int, int] = {}

    @property
    def chunk_count(self) -> int:
        return (self.total_len + self.chunk_size - 1) // self.chunk_size

    def add(self, frame: DataFrame) -> None:
        if frame.status != 0:
            raise ValueError(f"device returned READ status {frame.status}")
        if frame.session_id != self.session_id:
            raise ValueError("session mismatch")
        if frame.offset + len(frame.data) > self.total_len:
            raise ValueError("chunk outside image")
        if frame.offset % self.chunk_size != 0:
            raise ValueError("unaligned chunk offset")
        idx = frame.offset // self.chunk_size
        expected_len = min(self.chunk_size, self.total_len - frame.offset)
        if len(frame.data) != expected_len:
            raise ValueError("unexpected chunk length")
        if idx in self.received:
            old = bytes(self.buf[frame.offset : frame.offset + len(frame.data)])
            if old != frame.data:
                raise ValueError("duplicate chunk differs")
            return
        self.buf[frame.offset : frame.offset + len(frame.data)] = frame.data
        self.received.add(idx)
        self.seq_by_index[idx] = frame.seq

    def missing_offsets(self) -> list[int]:
        return [
            i * self.chunk_size
            for i in range(self.chunk_count)
            if i not in self.received
        ]

    def complete(self) -> bool:
        return len(self.received) == self.chunk_count

    def sha256(self) -> str:
        if not self.complete():
            raise ValueError("image incomplete")
        return hashlib.sha256(self.buf).hexdigest()

    def write(self, path: Path) -> None:
        if not self.complete():
            raise ValueError("refusing to write incomplete image")
        path.write_bytes(self.buf)

    def to_state(self) -> dict:
        return {
            "protocol_version": PROTOCOL_VERSION,
            "session_id": self.session_id,
            "total_len": self.total_len,
            "chunk_size": self.chunk_size,
            "received_offsets": sorted(i * self.chunk_size for i in self.received),
        }


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Reassemble JSONL-captured GL-SD DATA frames"
    )
    ap.add_argument("jsonl", type=Path)
    ap.add_argument("--session-id", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--total-len", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--chunk-size", type=int, default=MAX_CHUNK)
    ap.add_argument("--out", type=Path)
    ns = ap.parse_args()
    r = Reassembler(ns.session_id, ns.total_len, ns.chunk_size)
    for line in ns.jsonl.read_text().splitlines():
        if not line.strip():
            continue
        obj = json.loads(line)
        r.add(DataFrame.decode(bytes.fromhex(obj["payload_hex"])))
    status = {
        "received": len(r.received),
        "expected": r.chunk_count,
        "missing_offsets": r.missing_offsets(),
        "complete": r.complete(),
    }
    if r.complete():
        status["sha256"] = r.sha256()
        if ns.out:
            r.write(ns.out)
    print(json.dumps(status, indent=2))
    return 0 if r.complete() else 3


if __name__ == "__main__":
    raise SystemExit(main())
