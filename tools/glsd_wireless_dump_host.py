#!/usr/bin/env python3
"""
glsd_wireless_dump_host.py
Host-side logic for the GL-SD-301P wireless dump protocol.
Implements chunking, CRC32 validation, sequence tracking, and resume bitmaps.
"""
import struct
import hashlib
import json
from pathlib import Path

REGION_INACTIVE_APP = 0
CHUNK_SIZE = 48  # Matches Telink OTA_IMAGE_MAX_DATA_SIZE

def build_crc_table():
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (0xEDB88320 ^ (c >> 1)) if (c & 1) else (c >> 1)
        table.append(c)
    return table

_CRC_TABLE = build_crc_table()

def xcrc32(buf: bytes, init: int = 0xFFFFFFFF) -> int:
    crc = init & 0xFFFFFFFF
    for b in buf:
        crc = (crc >> 8) ^ _CRC_TABLE[(crc ^ b) & 0xFF]
    return crc & 0xFFFFFFFF

def build_read_req(region: int, offset: int, length: int, sequence: int) -> bytes:
    if length > 64:
        raise ValueError("Chunk length cannot exceed 64 bytes")
    return struct.pack("<BIBH", region, offset, length, sequence)

def parse_read_rsp(data: bytes) -> dict:
    if len(data) < 9:
        raise ValueError("READ_RSP too short")
    seq, region, offset, length = struct.unpack_from("<HBIB", data, 0)
    if len(data) < 9 + length + 4:
        raise ValueError("READ_RSP truncated payload/CRC")
    payload = data[9:9+length]
    stored_crc = struct.unpack_from("<I", data, 9+length)[0]
    computed_crc = xcrc32(payload)
    return {
        "sequence": seq,
        "region": region,
        "offset": offset,
        "length": length,
        "payload": payload,
        "stored_crc": stored_crc,
        "computed_crc": computed_crc,
        "crc_valid": stored_crc == computed_crc
    }

class ResumeBitmap:
    def __init__(self, total_length: int, chunk_size: int):
        self.total_length = total_length
        self.chunk_size = chunk_size
        self.total_chunks = (total_length + chunk_size - 1) // chunk_size
        self.received = [False] * self.total_chunks
        
    def mark_received(self, offset: int):
        idx = offset // self.chunk_size
        if 0 <= idx < self.total_chunks:
            self.received[idx] = True
            
    def is_received(self, offset: int) -> bool:
        idx = offset // self.chunk_size
        if 0 <= idx < self.total_chunks:
            return self.received[idx]
        return False
        
    def get_missing_offsets(self) -> list:
        return [i * self.chunk_size for i, rec in enumerate(self.received) if not rec]
        
    def save(self, path: str):
        with open(path, "w") as f:
            json.dump({"total": self.total_length, "chunk": self.chunk_size, "map": self.received}, f)
            
    @classmethod
    def load(cls, path: str):
        with open(path, "r") as f:
            data = json.load(f)
        bm = cls(data["total"], data["chunk"])
        bm.received = data["map"]
        return bm
