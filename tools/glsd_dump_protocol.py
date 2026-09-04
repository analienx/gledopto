#!/usr/bin/env python3
"""Host-side protocol helpers and resumable reassembler for GL-SD dump stager.

No device access is implemented here. The module defines framing used by the
future stager and validates/reassembles read-only flash chunks captured from it.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
import struct

PROTOCOL_VERSION = 1
MAX_CHUNK = 64
CMD_HELLO = 0x00
CMD_INFO = 0x01
CMD_READ_REQUEST = 0x10
CMD_DATA = 0x11
CMD_FINISH = 0x12
CMD_ABORT = 0x7F
DATA_PREFIX = struct.Struct('<IIB')


@dataclass(frozen=True)
class DataFrame:
    stream_id: int
    offset: int
    data: bytes
    crc32: int

    def encode(self) -> bytes:
        if not (1 <= len(self.data) <= MAX_CHUNK):
            raise ValueError('chunk length out of range')
        crc = binascii.crc32(self.data) & 0xFFFFFFFF
        return DATA_PREFIX.pack(self.stream_id, self.offset, len(self.data)) + self.data + struct.pack('<I', crc)

    @classmethod
    def decode(cls, raw: bytes) -> 'DataFrame':
        if len(raw) < DATA_PREFIX.size + 4:
            raise ValueError('short DATA frame')
        sid, off, n = DATA_PREFIX.unpack_from(raw)
        if n < 1 or n > MAX_CHUNK or len(raw) != DATA_PREFIX.size + n + 4:
            raise ValueError('invalid DATA frame length')
        data = raw[DATA_PREFIX.size:DATA_PREFIX.size + n]
        got = struct.unpack_from('<I', raw, DATA_PREFIX.size + n)[0]
        want = binascii.crc32(data) & 0xFFFFFFFF
        if got != want:
            raise ValueError(f'chunk CRC mismatch got=0x{got:08X} want=0x{want:08X}')
        return cls(sid, off, data, got)


class Reassembler:
    def __init__(self, stream_id: int, total_len: int, chunk_size: int = MAX_CHUNK):
        if total_len <= 0:
            raise ValueError('total_len must be positive')
        if chunk_size < 1 or chunk_size > MAX_CHUNK:
            raise ValueError('bad chunk size')
        self.stream_id = stream_id
        self.total_len = total_len
        self.chunk_size = chunk_size
        self.buf = bytearray(total_len)
        self.received: set[int] = set()

    def add(self, frame: DataFrame) -> None:
        if frame.stream_id != self.stream_id:
            raise ValueError('stream mismatch')
        if frame.offset < 0 or frame.offset + len(frame.data) > self.total_len:
            raise ValueError('chunk outside image')
        if frame.offset % self.chunk_size != 0:
            raise ValueError('unaligned chunk offset')
        idx = frame.offset // self.chunk_size
        expected_len = min(self.chunk_size, self.total_len - frame.offset)
        if len(frame.data) != expected_len:
            raise ValueError('unexpected chunk length')
        if idx in self.received:
            if bytes(self.buf[frame.offset:frame.offset + len(frame.data)]) != frame.data:
                raise ValueError('duplicate chunk differs')
            return
        self.buf[frame.offset:frame.offset + len(frame.data)] = frame.data
        self.received.add(idx)

    @property
    def chunk_count(self) -> int:
        return (self.total_len + self.chunk_size - 1) // self.chunk_size

    def missing_offsets(self) -> list[int]:
        return [i * self.chunk_size for i in range(self.chunk_count) if i not in self.received]

    def complete(self) -> bool:
        return len(self.received) == self.chunk_count

    def sha256(self) -> str:
        if not self.complete():
            raise ValueError('image incomplete')
        return hashlib.sha256(self.buf).hexdigest()

    def write(self, path: Path) -> None:
        if not self.complete():
            raise ValueError('refusing to write incomplete image')
        path.write_bytes(self.buf)


def main() -> int:
    ap = argparse.ArgumentParser(description='Reassemble JSONL-captured GL-SD DATA frames')
    ap.add_argument('jsonl', type=Path)
    ap.add_argument('--stream-id', type=lambda x: int(x, 0), required=True)
    ap.add_argument('--total-len', type=lambda x: int(x, 0), required=True)
    ap.add_argument('--chunk-size', type=int, default=MAX_CHUNK)
    ap.add_argument('--out', type=Path)
    ns = ap.parse_args()
    r = Reassembler(ns.stream_id, ns.total_len, ns.chunk_size)
    for line in ns.jsonl.read_text().splitlines():
        if not line.strip():
            continue
        obj = json.loads(line)
        r.add(DataFrame.decode(bytes.fromhex(obj['payload_hex'])))
    status = {
        'received': len(r.received),
        'expected': r.chunk_count,
        'missing_offsets': r.missing_offsets(),
        'complete': r.complete(),
    }
    if r.complete():
        status['sha256'] = r.sha256()
        if ns.out:
            r.write(ns.out)
    print(json.dumps(status, indent=2))
    return 0 if r.complete() else 3


if __name__ == '__main__':
    raise SystemExit(main())
