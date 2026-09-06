from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import make_glsd_stager_index as indexer
import make_ota_acceptance_probe as probe
import telink_ota_forensics as forensics


def make_valid_ota(version=indexer.STOCK_FILE_VERSION + 1, payload_size=256):
    b = bytearray([0xA5] * payload_size)
    struct.pack_into("<I", b, 2, version)
    b[6:8] = b"\x5D\x02"
    struct.pack_into("<I", b, 8, forensics.TELINK_STARTUP_FLAG)
    struct.pack_into("<H", b, 0x12, indexer.TARGET_MFG_CODE)
    struct.pack_into("<H", b, 0x14, indexer.TARGET_IMAGE_TYPE)
    struct.pack_into("<I", b, 0x18, payload_size)
    struct.pack_into("<I", b, payload_size - 4, forensics.telink_xcrc32(b[:-4]))
    payload = bytes(b)

    name = b"GLSD READ ONLY STAGER".ljust(32, b"\x00")
    sub = struct.pack("<HI", 0x0000, len(payload)) + payload
    total = 56 + len(sub)
    header = struct.pack(
        "<IHHHHHIH32sI",
        forensics.OTA_MAGIC,
        0x0100,
        56,
        0,
        indexer.TARGET_MFG_CODE,
        indexer.TARGET_IMAGE_TYPE,
        version,
        2,
        name,
        total,
    )
    return header + sub


class StagerIndexTests(unittest.TestCase):
    def test_valid_target_image_yields_hardware_locked_entry(self):
        with tempfile.TemporaryDirectory() as td:
            image = Path(td) / "stager.ota"
            image.write_bytes(make_valid_ota())
            entry = indexer.build_entry(image, url="http://127.0.0.1:8080/stager.ota")
        self.assertEqual(entry["manufacturerCode"], 0x124F)
        self.assertEqual(entry["imageType"], 0x1416)
        self.assertEqual(entry["modelId"], "GL-SD-301P")
        self.assertEqual(entry["manufacturerName"], "GLEDOPTO")
        self.assertEqual((entry["hardwareVersionMin"], entry["hardwareVersionMax"]), (2, 2))
        self.assertEqual(len(entry["sha512"]), 128)

    def test_stock_or_lower_version_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            image = Path(td) / "stock.ota"
            image.write_bytes(make_valid_ota(indexer.STOCK_FILE_VERSION))
            with self.assertRaises(ValueError):
                indexer.build_entry(image, url="http://127.0.0.1/stock.ota")

    def test_intentionally_invalid_acceptance_probe_is_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            image = Path(td) / "probe.ota"
            image.write_bytes(probe.build_ota(indexer.STOCK_FILE_VERSION + 1, 512))
            with self.assertRaises(ValueError):
                indexer.build_entry(image, url="http://127.0.0.1/probe.ota")


if __name__ == "__main__":
    unittest.main()
