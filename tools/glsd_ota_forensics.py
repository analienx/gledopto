#!/usr/bin/env python3
"""
glsd_ota_forensics.py (v2)
Upgraded offline forensics tool for GLEDOPTO/Telink OTA images.
- Parses Zigbee OTA header (16-bit fieldControl).
- Enumerates ALL sub-elements to prove absence of hidden crypto tags.
- Parses inner Telink app header (mfg, type, version).
- Validates Telink CRC (reflected, no final XOR).
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
    
    hdr_ver = struct.unpack_from("<H", data, 4)[0]
    hdr_len = struct.unpack_from("<H", data, 6)[0]
    field_ctrl = struct.unpack_from("<H", data, 8)[0]
    mfr_code = struct.unpack_from("<H", data, 10)[0]
    image_type = struct.unpack_from("<H", data, 12)[0]
    file_ver = struct.unpack_from("<I", data, 14)[0]
    stack_ver = struct.unpack_from("<H", data, 18)[0]
    hdr_string = data[20:52].rstrip(b"\x00").decode("ascii", errors="replace")
    total_image_size = struct.unpack_from("<I", data, 52)[0]
    
    return {
        "magic": f"0x{magic:08X}",
        "headerVersion": hdr_ver,
        "headerLength": hdr_len,
        "fieldControl": f"0x{field_ctrl:04X}",
        "manufacturerCode": f"0x{mfr_code:04X}",
        "imageType": f"0x{image_type:04X}",
        "fileVersion": f"0x{file_ver:08X}",
        "stackVersion": stack_ver,
        "headerString": hdr_string,
        "totalImageSize": total_image_size
    }

def extract_subelements(data: bytes, hdr_len: int, total_size: int):
    elements = []
    off = hdr_len
    while off + 6 <= total_size:
        tag = struct.unpack_from("<H", data, off)[0]
        length = struct.unpack_from("<I", data, off + 2)[0]
        
        payload_start = off + 6
        if payload_start + length > total_size:
            elements.append({"tag": f"0x{tag:04X}", "length": length, "error": "truncated"})
            break
            
        elements.append({
            "tag": f"0x{tag:04X}",
            "length": length,
            "offset": payload_start
        })
        off = payload_start + length
        
    trailing = total_size - off
    return elements, trailing

def parse_telink_app_header(app: bytes):
    res = {"valid": False}
    if len(app) < 0x20:
        return res
        
    magic5d02 = app[6:8]
    if magic5d02 != APP_HEADER_MAGIC_5D02:
        return res
        
    marker = struct.unpack_from("<I", app, MARKER_OFFSET)[0]
    inner_ver = struct.unpack_from("<I", app, 0x02)[0]
    inner_mfr = struct.unpack_from("<H", app, 0x12)[0]
    inner_type = struct.unpack_from("<H", app, 0x14)[0]
    declared_size = struct.unpack_from("<I", app, DECLARED_SIZE_OFFSET)[0]
    
    res = {
        "valid": True,
        "magic_5d02": magic5d02.hex(),
        "boot_marker": f"0x{marker:08X}",
        "inner_mfr": f"0x{inner_mfr:04X}",
        "inner_type": f"0x{inner_type:04X}",
        "inner_ver": f"0x{inner_ver:08X}",
        "declared_size": declared_size
    }
    return res

def validate_app_crc(app: bytes, declared_size: int):
    if declared_size < 4 or declared_size > len(app):
        return {"status": "size_out_of_range"}
    stored_crc = struct.unpack_from("<I", app, declared_size - 4)[0]
    computed = xcrc32(app[:declared_size - 4], 0xFFFFFFFF)
    return {
        "status": "PASS" if stored_crc == computed else "FAIL",
        "stored": f"0x{stored_crc:08X}",
        "computed": f"0x{computed:08X}"
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
        "production_ota_go": False
    }
    
    try:
        hdr = parse_zigbee_ota_header(data)
        result["zigbee_header"] = hdr
    except ValueError as e:
        result["container_verdict"] = "INVALID_NOT_ZIGBEE_OTA"
        result["error"] = str(e)
        return result
        
    if hdr["totalImageSize"] != len(data):
        result["container_verdict"] = "INVALID_SIZE_MISMATCH"
        return result
        
    elements, trailing = extract_subelements(data, hdr["headerLength"], len(data))
    result["subelements"] = elements
    result["trailing_bytes"] = trailing
    
    if trailing > 0:
        result["auth_indicator"] = "TRAILING_BYTES_PRESENT"
        
    crypto_tags = [e for e in elements if e["tag"] not in ("0x0000", "0xF000")]
    if crypto_tags:
        result["auth_indicator"] = "UNKNOWN_SUBELEMENT_TAGS"
        
    plain_imgs = [e for e in elements if e["tag"] == "0x0000"]
    aes_imgs = [e for e in elements if e["tag"] == "0xF000"]
    
    if aes_imgs and not plain_imgs:
        result["container_verdict"] = "AES_WRAPPED_TELINK_OTA"
        return result
        
    if not plain_imgs:
        result["container_verdict"] = "NO_TELINK_APP_FOUND"
        return result
        
    p = plain_imgs[0]
    app = data[p["offset"]:p["offset"]+p["length"]]
    telink = parse_telink_app_header(app)
    result["telink_app"] = telink
    
    if not telink["valid"]:
        result["container_verdict"] = "INVALID_TELINK_APP_HEADER"
        return result
        
    if telink["boot_marker"] != f"0x{TL_START_UP_FLAG:08X}":
        result["container_verdict"] = "INVALID_TELINK_BOOT_MARKER"
        return result
        
    crc_res = validate_app_crc(app, telink["declared_size"])
    result["crc_validation"] = crc_res
    
    if crc_res["status"] == "FAIL":
        result["container_verdict"] = "INVALID_TELINK_CRC"
        return result
        
    result["container_verdict"] = "VERIFIED_PLAIN_TELINK_OTA"
    if "auth_indicator" not in result:
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
