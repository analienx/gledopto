import pytest
import sys
sys.path.insert(0, 'tools')
from glsd_wireless_dump_host import build_read_req, parse_read_rsp, xcrc32, ResumeBitmap

def test_build_read_req():
    req = build_read_req(region=0, offset=0x1000, length=48, sequence=1)
    assert len(req) == 9
    assert req == b'\x00\x00\x10\x00\x00\x30\x01\x00'

def test_parse_read_rsp_valid():
    payload = b'A' * 48
    crc = xcrc32(payload)
    rsp = struct.pack("<HBIB", 1, 0, 0x1000, 48) + payload + struct.pack("<I", crc)
    parsed = parse_read_rsp(rsp)
    assert parsed["crc_valid"] is True
    assert parsed["sequence"] == 1
    assert parsed["payload"] == payload

def test_parse_read_rsp_bad_crc():
    payload = b'A' * 48
    rsp = struct.pack("<HBIB", 1, 0, 0x1000, 48) + payload + struct.pack("<I", 0xDEADBEEF)
    parsed = parse_read_rsp(rsp)
    assert parsed["crc_valid"] is False

def test_resume_bitmap():
    bm = ResumeBitmap(100, 48)
    assert bm.total_chunks == 3
    assert bm.get_missing_offsets() == [0, 48, 96]
    bm.mark_received(0)
    bm.mark_received(48)
    assert bm.get_missing_offsets() == [96]
