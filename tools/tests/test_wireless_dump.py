import binascii
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / filename)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


forensics = load('forensics', 'telink_ota_forensics.py')
probe = load('probe', 'make_ota_acceptance_probe.py')
proto = load('proto', 'glsd_dump_protocol.py')


class WirelessDumpTests(unittest.TestCase):
    def test_probe_parses_and_is_deliberately_crc_invalid(self):
        data = probe.build_ota(0x26013002, 512)
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'p.ota'
            p.write_bytes(data)
            r = forensics.analyze(p)
        self.assertTrue(r['offline_gate']['container_structurally_valid'])
        self.assertTrue(r['offline_gate']['identity_consistent'])
        self.assertFalse(r['offline_gate']['crc_convention_identified'])
        self.assertEqual(r['header']['manufacturer_code'], 0x124F)
        self.assertEqual(r['header']['image_type'], 0x1416)

    def test_crc_detector_accepts_standard_little_endian_trailer(self):
        body = bytearray(128)
        struct.pack_into('<I', body, 4, 0x26013001)
        struct.pack_into('<H', body, 0x12, 0x124F)
        struct.pack_into('<H', body, 0x14, 0x1416)
        struct.pack_into('<I', body, 0x18, len(body))
        crc = binascii.crc32(body[:-4]) & 0xFFFFFFFF
        struct.pack_into('<I', body, len(body) - 4, crc)
        r = forensics.parse_telink_payload(bytes(body))
        self.assertTrue(r['crc_convention_proven'])
        self.assertIn('crc32_iso_hdlc', r['crc_match_candidates'])

    def test_data_frame_crc_and_reassembly_out_of_order(self):
        image = bytes(range(150))
        r = proto.Reassembler(0x12345678, len(image), 64)
        frames = []
        for off in range(0, len(image), 64):
            d = image[off:off + 64]
            raw = proto.DataFrame(0x12345678, off, d, 0).encode()
            frames.append(proto.DataFrame.decode(raw))
        r.add(frames[2])
        r.add(frames[0])
        r.add(frames[0])
        r.add(frames[1])
        self.assertTrue(r.complete())
        self.assertEqual(bytes(r.buf), image)
        self.assertEqual(r.missing_offsets(), [])

    def test_conflicting_duplicate_rejected(self):
        r = proto.Reassembler(1, 64, 64)
        a = proto.DataFrame.decode(proto.DataFrame(1, 0, b'A' * 64, 0).encode())
        b = proto.DataFrame.decode(proto.DataFrame(1, 0, b'B' * 64, 0).encode())
        r.add(a)
        with self.assertRaises(ValueError):
            r.add(b)


if __name__ == '__main__':
    unittest.main()
