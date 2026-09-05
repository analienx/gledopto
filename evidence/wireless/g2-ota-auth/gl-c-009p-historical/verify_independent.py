"""Independent re-verification of the pinned historical GLEDOPTO OTA container.

Read-only. Emits evidence/wireless/g2-ota-auth/gl-c-009p-historical/independent-verification.json
Does not reuse tools/glsd_ota_forensics.py logic: framing is re-derived and the CRC is
recomputed with a table-free bit-serial implementation so the committed tool and this
check are independent implementations of the same claim.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import zlib

_ap = argparse.ArgumentParser(description=__doc__)
_ap.add_argument('ota', nargs='?', default=r'.local/gl-c-009p.ota',
                 help='path to the downloaded historical artifact (never committed)')
_ap.add_argument('--out', default=r'evidence/wireless/g2-ota-auth/gl-c-009p-historical/independent-verification.json')
_args = _ap.parse_args()
OTA = _args.ota
OUT = _args.out

PIN = {
    "repository": "Koenkk/zigbee-OTA",
    "commit": "f4260fe4dfa47561f607707ad38abb829eb95a83",
    "path": "images/Gledopto/GL-C-009P(MINI)_20451203_20240227.ota",
    "expected_git_blob": "09c1e5ad3874a422cbe1e87e351e6478d4e1272e",
    "expected_size": 212738,
    "expected_sha512": ("868e671255db3c753a282125cdc4c333771cf1032423968b1412f9760"
                        "cb105f97874261ab56559dc1cf54c0742eec062ccf9b8a75b4ef5e85"
                        "b1485e8d5fd1aac"),
}


def crc32_bitserial_reflected(data, init=0xFFFFFFFF):
    crc = init & 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc & 0xFFFFFFFF


raw = open(OTA, 'rb').read()
size = len(raw)
sha512 = hashlib.sha512(raw).hexdigest()
sha256 = hashlib.sha256(raw).hexdigest()
blob = subprocess.run(['git', 'hash-object', OTA], capture_output=True, text=True).stdout.strip()

magic = struct.unpack_from('<I', raw, 0)[0]
hdr_len = struct.unpack_from('<H', raw, 6)[0]
field_ctrl = struct.unpack_from('<H', raw, 8)[0]
mfr = struct.unpack_from('<H', raw, 10)[0]
img_type = struct.unpack_from('<H', raw, 12)[0]
file_ver = struct.unpack_from('<I', raw, 14)[0]
stack_ver = struct.unpack_from('<H', raw, 18)[0]
hdr_string = raw[20:52].split(b'\x00')[0].decode('ascii', 'replace')
declared_total = struct.unpack_from('<I', raw, 52)[0]

sub_tag = struct.unpack_from('<H', raw, hdr_len)[0]
sub_len = struct.unpack_from('<I', raw, hdr_len + 2)[0]
payload_off = hdr_len + 6
app = raw[payload_off:payload_off + sub_len]

inner_ver = struct.unpack_from('<I', app, 0x02)[0]
inner_magic_5d02 = app[0x06:0x08]
inner_startup = struct.unpack_from('<I', app, 0x08)[0]
inner_mfr = struct.unpack_from('<H', app, 0x12)[0]
inner_type = struct.unpack_from('<H', app, 0x14)[0]
inner_declared_size = struct.unpack_from('<I', app, 0x18)[0]
stored_crc = struct.unpack_from('<I', app, sub_len - 4)[0]

candidates = {
    "bitserial_reflected_init_FFFFFFFF_no_final_xor_over_data_minus_crc":
        crc32_bitserial_reflected(app[:sub_len - 4], 0xFFFFFFFF),
    "bitserial_reflected_init_0_over_data_minus_crc":
        crc32_bitserial_reflected(app[:sub_len - 4], 0x00000000),
    "bitserial_with_crc_field_zeroed":
        crc32_bitserial_reflected(app[:sub_len - 4] + b"\x00\x00\x00\x00", 0xFFFFFFFF),
    "zlib_crc32_of_data_minus_crc": zlib.crc32(app[:sub_len - 4]),
    "zlib_crc32_of_data_minus_crc_xor_FFFFFFFF": zlib.crc32(app[:sub_len - 4]) ^ 0xFFFFFFFF,
}
winners = [k for k, v in candidates.items() if v == stored_crc]

trailing = size - (payload_off + sub_len)

g1 = {
    "expected_size_match": size == PIN["expected_size"],
    "expected_sha512_match": sha512 == PIN["expected_sha512"],
    "expected_git_blob_match": blob == PIN["expected_git_blob"],
    "observed_size": size,
    "observed_sha256": sha256,
    "observed_sha512": sha512,
    "observed_git_blob": blob,
}

gate0 = {
    "1_outer_total_size_structurally_valid": {
        "pass": declared_total == size and hdr_len + 6 + sub_len == size,
        "detail": "declaredTotalImageSize=%d fileBytes=%d framingSum=%d trailingBytes=%d"
                  % (declared_total, size, hdr_len + 6 + sub_len, trailing),
    },
    "2_upgrade_image_subelement_parsed": {
        "pass": sub_tag == 0x0000 and sub_len == inner_declared_size and trailing == 0,
        "detail": "tag=0x%04X subLen=%d(0x%X) innerDeclaredSize=%d payloadStart=%d"
                  % (sub_tag, sub_len, sub_len, inner_declared_size, payload_off),
    },
    "3_outer_identity_matches_telink_inner_fields": {
        "pass": (mfr == inner_mfr and img_type == inner_type and file_ver == inner_ver),
        "detail": "outer mfg=0x%04X type=0x%04X ver=0x%08X / inner@0x12 mfg=0x%04X "
                  "@0x14 type=0x%04X @0x02 ver=0x%08X"
                  % (mfr, img_type, file_ver, inner_mfr, inner_type, inner_ver),
    },
    "4_crc_convention_identified_from_real_image": {
        "pass": len(winners) > 0,
        "detail": "storedTailU32=0x%08X matchingConventions=%s" % (stored_crc, winners),
    },
    "5_no_unexplained_signature_or_authentication_material": {
        "pass": trailing == 0 and sub_tag != 0xF000 and field_ctrl == 0,
        "detail": "trailingBytes=%d subelementTag=0x%04X headerFieldControl=0x%04X "
                  "aesWrapperElement0xF000=absent zigbeeSignatureOrCertSubelement=absent"
                  % (trailing, sub_tag, field_ctrl),
    },
}

live_ref = {
    "source": "evidence/phase1-software-only-20260903/raw/ota-live-descriptor.json",
    "captured_at": "2026-09-03T06:52:02Z",
    "commandQueryNextImageRequest": {
        "manufacturerCode": 4687,
        "imageType": 5142,
        "fileVersion": 637612033,
        "provenance": "values transcribed from the cited device ZCL capture, not from this file",
    },
    "comparison": {
        "manufacturerCode_matches_live": mfr == 4687,
        "imageType_matches_live": img_type == 5142,
        "fileVersion_matches_live": file_ver == 637612033,
        "historical_minus_live_u32": file_ver - 637612033,
        "historical_minus_live_hex": "0x%08X" % ((file_ver - 637612033) & 0xFFFFFFFF),
        "live_fileVersion_hex": "0x%08X" % 637612033,
        "historical_fileVersion_hex": "0x%08X" % file_ver,
        "note": ("Historical GL-C-009P(MINI) container shares mfg+imageType with the live "
                 "GL-SD-301P but its fileVersion is LOWER than the device's current "
                 "0x26013001, so it is a version downgrade for this device. Identical "
                 "mfg/imageType across two different physical products confirms the "
                 "issue-#1 rule that tuple identity alone never proves firmware "
                 "compatibility."),
    },
}

doc = {
    "task": "Offline OTA Forensics Validation (Batch 1)",
    "control_issue": "analienx/gledopto#1",
    "artifact_pinned": PIN,
    "g1_artifact_hash": g1,
    "zigbee_ota_header": {
        "magic": "0x%08X" % magic,
        "magic_valid": magic == 0x0BEEF11E,
        "headerLength": hdr_len,
        "fieldControl": "0x%04X" % field_ctrl,
        "manufacturerCode": "0x%04X (%d)" % (mfr, mfr),
        "imageType": "0x%04X (%d)" % (img_type, img_type),
        "fileVersion": "0x%08X (%d)" % (file_ver, file_ver),
        "zigbeeStackVersion": "0x%04X" % stack_ver,
        "headerString": hdr_string,
        "totalImageSize": declared_total,
    },
    "subelement": {
        "tag": "0x%04X" % sub_tag,
        "length": sub_len,
        "payloadOffset": payload_off,
        "trailingBytesAfterSubelement": trailing,
    },
    "telink_inner_header": {
        "first_32_bytes_hex": app[:32].hex(' '),
        "fileVersion_at_0x02": "0x%08X" % inner_ver,
        "magic_at_0x06": inner_magic_5d02.hex(),
        "startupFlag_at_0x08": {
            "u32_little_endian_value": "0x%08X" % inner_startup,
            "on_disk_bytes": app[0x08:0x0C].hex(' '),
            "expected": "0x544C4E4B",
            "match": inner_startup == 0x544C4E4B,
            "reading_note": ("u32 value 0x544C4E4B reads 'TLNK' most-significant-byte "
                             "first; stored little-endian the on-disk bytes are "
                             "'4B 4E 4C 54' which renders as the string 'KNLT'. Same "
                             "flag, opposite byte order."),
        },
        "manufacturer_at_0x12": "0x%04X" % inner_mfr,
        "imageType_at_0x14": "0x%04X" % inner_type,
        "declaredSize_at_0x18": inner_declared_size,
        "storedCrc_at_tail": "0x%08X" % stored_crc,
    },
    "crc_candidates_tested": {k: "0x%08X" % v for k, v in candidates.items()},
    "crc_conventions_matching_stored_value": winners,
    "gate0": gate0,
    "gate0_all_pass": all(g["pass"] for g in gate0.values()),
    "live_device_comparison": live_ref,
    "interpretation_limits": [
        "Container-level only. Proves nothing about whether the GL-SD-301P bootloader would ACCEPT an image with this tuple.",
        "Cross-model evidence (GL-C-009P(MINI)), not GL-SD-301P. Does not satisfy G4 target-specific validation.",
        "Absence of a signature/AES wrapper in this historical artifact does not establish that GLEDOPTO never signs; it establishes only that this pinned build is unsigned and plain.",
        "production_ota_go remains false. No live device interaction was performed in this batch.",
    ],
}

with open(OUT, 'w', newline='\n') as f:
    json.dump(doc, f, indent=2)
    f.write('\n')

print(json.dumps(doc, indent=2))
