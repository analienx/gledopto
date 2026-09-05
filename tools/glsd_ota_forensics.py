#!/usr/bin/env python3
"""
glsd_ota_forensics.py
Improved offline forensics tool for GLEDOPTO/Telink OTA images.
Implements exact sub-element parsing, Telink CRC validation, and strict verdicts.
"""
import argparse
import struct
import sys
import hashlib
import json

TL_START_UP_FLAG = 0x544C4E4B
MARKER_OFFSET = 8
APP_HEADER_MAGIC_5D02 = b"\x5d\x02"
DECLARED_SIZE_OFFSET = 0x18

def build_crc_table():
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (0xEDB88320 ^ (c >> 1)) if (c & 1) else (c >> 1)
        table.append(c)
    return table

_CRC_TABLE = build_crc_table()

def xcrc32(buf: bytes, init: int) -> int:
    crc = init & 0xFFFFFFFF
    for b in buf:
        crc = (crc >> 8) ^ _CRC_TABLE[(crc ^ b) & 0xFF]
    return crc & 0xFFFFFFFF

def parse_zigbee_ota_header(data: bytes):
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != 0x0BEEF11E:
        raise ValueError(f"not a Zigbee OTA file (magic=0x{magic:08X})")
    hdr_len = struct.unpack_from("<H", data, 6)[0]
    mfr_code = struct.unpack_from("<H", data, 10)[0]
    image_type = struct.unpack_from("<H", data, 12)[0]
    file_ver = struct.unpack_from("<I", data, 14)[0]
    hdr_string = data[20:52].rstrip(b"\x00").decode("ascii", errors="replace")
    total_image_size = struct.unpack_from("<I", data, 52)[0]
    return {
        "headerLength": hdr_len,
        "manufacturerCode": mfr_code,
        "imageType": image_type,
        "fileVersion": file_ver,
        "headerString": hdr_string,
        "totalImageSize": total_image_size,
    }

def analyze(path: str):
    with open(path, "rb") as f:
        data = f.read()
    
    result = {
        "file": path,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "sha512": hashlib.sha512(data).hexdigest(),
        "container_verdict": "UNKNOWN",
        "auth_indicator": "UNKNOWN",
        "production_ota_go": False
    }
    
    try:
        hdr = parse_zigbee_ota_header(data)
        result["header"] = hdr
    except ValueError as e:
        result["container_verdict"] = "INVALID_NOT_ZIGBEE_OTA"
        result["error"] = str(e)
        return result

    if hdr["totalImageSize"] != len(data):
        result["container_verdict"] = "INVALID_SIZE_MISMATCH"
        return result

    off = hdr["headerLength"]
    tag = struct.unpack_from("<H", data, off)[0]
    length = struct.unpack_from("<I", data, off + 2)[0]
    payload_start = off + 6
    app = data[payload_start:payload_start + length]
    
    result["subelement"] = {"tag": tag, "length": length}
    
    if tag == 0xF000:
        result["container_verdict"] = "AES_WRAPPED_TELINK_OTA"
        result["auth_indicator"] = "AES_WRAPPER_PRESENT"
        return result
        
    if tag != 0x0000:
        result["container_verdict"] = "UNKNOWN_SUBELEMENT"
        return result

    if len(app) < 8 or app[6:8] != APP_HEADER_MAGIC_5D02:
        result["container_verdict"] = "INVALID_TELINK_APP_HEADER"
        return result
        
    marker = struct.unpack_from("<I", app, MARKER_OFFSET)[0]
    if marker != TL_START_UP_FLAG:
        result["container_verdict"] = "INVALID_TELINK_BOOT_MARKER"
        return result
        
    declared_size = struct.unpack_from("<I", app, DECLARED_SIZE_OFFSET)[0]
    if declared_size != length:
        result["container_verdict"] = "INVALID_TELINK_DECLARED_SIZE"
        return result
        
    stored_crc = struct.unpack_from("<I", app, declared_size - 4)[0]
    computed = xcrc32(app[:declared_size - 4], 0xFFFFFFFF)
    if stored_crc != computed:
        result["container_verdict"] = "INVALID_TELINK_CRC"
        return result

    result["container_verdict"] = "VERIFIED_PLAIN_TELINK_OTA"
    result["auth_indicator"] = "NO_CONTAINER_AUTH_DETECTED"
    return result

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--json", help="Output JSON file")
    args = ap.parse_args()
    
    res = analyze(args.path)
    print(json.dumps(res, indent=2))
    if args.json:
        with open(args.json, "w") as f:
            json.dump(res, f, indent=2)

if __name__ == "__main__":
    main()
