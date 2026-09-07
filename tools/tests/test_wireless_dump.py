import binascii
import importlib.util
from pathlib import Path
import struct
import subprocess
import shutil
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


forensics = load("forensics", "telink_ota_forensics.py")
probe = load("probe", "make_ota_acceptance_probe.py")
proto = load("proto", "glsd_dump_protocol.py")
host = load("host", "glsd_wireless_dump_host.py")


def make_valid_telink_app(size=256, marker_first=0x4B):
    b = bytearray([0xA5] * size)
    struct.pack_into("<I", b, 2, 0x26013001)
    b[6:8] = b"\x5D\x02"
    marker = bytearray(forensics.TELINK_STARTUP_FLAG.to_bytes(4, "little"))
    marker[0] = marker_first
    b[8:12] = marker
    struct.pack_into("<H", b, 0x12, 0x124F)
    struct.pack_into("<H", b, 0x14, 0x1416)
    struct.pack_into("<I", b, 0x18, size)
    # CRC must be computed on the valid stock marker form.
    valid_for_crc = bytearray(b)
    valid_for_crc[8] = 0x4B
    crc = forensics.telink_xcrc32(valid_for_crc[:-4])
    struct.pack_into("<I", b, size - 4, crc)
    return bytes(b)


class WirelessDumpTests(unittest.TestCase):
    def test_native_read_only_stager_core(self):
        gcc = shutil.which("gcc")
        if not gcc:
            self.skipTest("gcc not available")
        fw = ROOT.parent / "firmware" / "wireless-dump-stager"
        with tempfile.TemporaryDirectory() as td:
            exe = Path(td) / "stager_core_test"
            subprocess.run(
                [
                    gcc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(fw),
                    str(fw / "glsd_stager_core.c"),
                    str(fw / "tests" / "stager_core_test.c"),
                    "-o", str(exe),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            cp = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn("stager_core_test: PASS", cp.stdout)

    def test_native_dispatcher_and_cross_language_fixture(self):
        gcc = shutil.which("gcc")
        if not gcc:
            self.skipTest("gcc not available")
        fw = ROOT.parent / "firmware" / "wireless-dump-stager"
        with tempfile.TemporaryDirectory() as td:
            exe = Path(td) / "stager_e2e_fixture"
            subprocess.run(
                [
                    gcc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(fw),
                    str(fw / "glsd_stager_core.c"),
                    str(fw / "glsd_stager_dispatch.c"),
                    str(fw / "tests" / "stager_e2e_fixture.c"),
                    "-o", str(exe),
                ],
                check=True, capture_output=True, text=True,
            )
            cp = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            lines = [line.strip() for line in cp.stdout.splitlines() if line.strip()]
            self.assertTrue(lines[0].startswith("INFO="))
            info = proto.StagerInfo.decode(bytes.fromhex(lines[0].split("=", 1)[1]))
            self.assertEqual(info.session_id, 0x11223344)
            self.assertEqual(info.old_declared_size, 240)
            self.assertEqual(info.inferred_old_base, 0)
            self.assertEqual(info.inferred_stager_base, 0x40000)
            with tempfile.TemporaryDirectory() as state_td:
                obj = host.PersistentDump.create(
                    Path(state_td) / "s", session_id=info.session_id,
                    total_len=info.old_declared_size, chunk_size=48, target_ieee="fixture",
                )
                for line in reversed(lines[1:]):
                    self.assertTrue(line.startswith("DATA="))
                    obj.ingest(bytes.fromhex(line.split("=", 1)[1]))
                result = obj.finalize()
                self.assertTrue(result["pass"])
                self.assertEqual(result["reconstruction_diffs"], [{"offset": 8, "before": 0, "after": 0x4B}])

    def test_probe_parses_and_is_deliberately_crc_invalid(self):
        data = probe.build_ota(0x26013002, 512)
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "p.ota"
            p.write_bytes(data)
            r = forensics.analyze(p)
        self.assertTrue(r["offline_gate"]["container_structurally_valid"])
        self.assertTrue(r["offline_gate"]["identity_consistent"])
        self.assertFalse(r["offline_gate"]["telink_crc_convention_identified"])
        self.assertEqual(r["header"]["manufacturer_code"], 0x124F)
        self.assertEqual(r["header"]["image_type"], 0x1416)

    def test_exact_telink_crc_no_final_xor(self):
        body = b"123456789"
        # Standard CRC32 is the finalized form; Telink deliberately omits final XOR.
        self.assertEqual(binascii.crc32(body) & 0xFFFFFFFF, 0xCBF43926)
        self.assertEqual(forensics.telink_xcrc32(body), 0x340BC6D9)

    def test_telink_application_reconstructs_only_marker(self):
        raw = make_valid_telink_app(marker_first=0x00)
        reconstructed, meta = forensics.reconstruct_invalidated_telink_app(raw)
        self.assertEqual(meta["diffs"], [{"offset": 8, "before": 0, "after": 0x4B}])
        self.assertTrue(meta["validation"]["valid"])
        self.assertEqual(reconstructed[8], 0x4B)
        self.assertEqual(raw[:8] + raw[9:], reconstructed[:8] + reconstructed[9:])

    def test_unexpected_marker_rejected(self):
        raw = bytearray(make_valid_telink_app())
        raw[8] = 0xFF
        with self.assertRaises(forensics.ParseError):
            forensics.reconstruct_invalidated_telink_app(bytes(raw))

    def test_info_gate_rejects_non_opposite_bank(self):
        info = proto.StagerInfo(
            protocol_version=1,
            stager_build_id=1,
            session_id=2,
            flash_jedec_id=0,
            flash_size=0x80000,
            bank_a_base=0,
            bank_b_base=0x40000,
            bank_a_flag32=proto.TELINK_INVALIDATED_FLAG,
            bank_b_flag32=proto.TELINK_STARTUP_FLAG,
            inferred_stager_base=0x40000,
            inferred_old_base=0x40000,
            old_declared_size=0x20000,
            old_tail_crc32=0,
            old_reconstructed_crc_valid=True,
            allowed_read_start=0,
            allowed_read_length=0x20000,
            journal_state=0xFF,
            rollback_compiled=False,
        )
        with self.assertRaises(ValueError):
            proto.validate_info(info)

    def test_info_gate_rejects_bad_bank_marker_state(self):
        base = dict(
            protocol_version=1, stager_build_id=1, session_id=2, flash_jedec_id=0,
            flash_size=0x80000, bank_a_base=0, bank_b_base=0x40000,
            bank_a_flag32=proto.TELINK_INVALIDATED_FLAG,
            bank_b_flag32=proto.TELINK_STARTUP_FLAG,
            inferred_stager_base=0x40000, inferred_old_base=0,
            old_declared_size=0x20000, old_tail_crc32=0,
            old_reconstructed_crc_valid=True, allowed_read_start=0,
            allowed_read_length=0x20000, journal_state=0xFF, rollback_compiled=False,
        )
        proto.validate_info(proto.StagerInfo(**base))
        base["bank_b_flag32"] = 0xFFFFFFFF
        with self.assertRaises(ValueError):
            proto.validate_info(proto.StagerInfo(**base))

    def test_data_frame_crc_and_reassembly_out_of_order(self):
        image = bytes(range(150))
        r = proto.Reassembler(0x12345678, len(image), 48)
        frames = []
        seq = 1
        for off in range(0, len(image), 48):
            d = image[off : off + 48]
            raw = proto.DataFrame(0x12345678, seq, off, d).encode()
            frames.append(proto.DataFrame.decode(raw))
            seq += 1
        r.add(frames[2])
        r.add(frames[0])
        r.add(frames[0])
        r.add(frames[1])
        r.add(frames[3])
        self.assertTrue(r.complete())
        self.assertEqual(bytes(r.buf), image)
        self.assertEqual(r.missing_offsets(), [])

    def test_conflicting_duplicate_rejected(self):
        r = proto.Reassembler(1, 48, 48)
        a = proto.DataFrame.decode(proto.DataFrame(1, 1, 0, b"A" * 48).encode())
        b = proto.DataFrame.decode(proto.DataFrame(1, 2, 0, b"B" * 48).encode())
        r.add(a)
        with self.assertRaises(ValueError):
            r.add(b)

    def test_persistent_resume_and_finalize(self):
        raw = make_valid_telink_app(size=240, marker_first=0x00)
        with tempfile.TemporaryDirectory() as td:
            state = Path(td) / "s"
            obj = host.PersistentDump.create(
                state,
                session_id=0x1234,
                total_len=len(raw),
                chunk_size=48,
                target_ieee="0xa4c13850cfcdb3a4",
            )
            seq = 10
            frames = []
            for off in range(0, len(raw), 48):
                d = raw[off : off + 48]
                frames.append(proto.DataFrame(0x1234, seq, off, d).encode())
                seq += 1
            # Persist half, reopen, then finish out-of-order.
            obj.ingest(frames[0])
            obj.ingest(frames[2])
            obj = host.PersistentDump.open(state)
            self.assertIn(48, obj.missing_offsets())
            for idx in [1, 4, 3]:
                obj.ingest(frames[idx])
            result = obj.finalize()
            self.assertTrue(result["pass"])
            self.assertEqual(result["reconstruction_diffs"][0]["offset"], 8)
            self.assertTrue((state / host.RAW_BIN).exists())
            self.assertTrue((state / host.RECON_BIN).exists())

    def test_finalize_refuses_valid_marker_source(self):
        raw = make_valid_telink_app(size=240, marker_first=0x4B)
        with tempfile.TemporaryDirectory() as td:
            state = Path(td) / "s"
            obj = host.PersistentDump.create(
                state,
                session_id=1,
                total_len=len(raw),
                target_ieee="x",
            )
            seq = 1
            for off in range(0, len(raw), 48):
                obj.ingest(
                    proto.DataFrame(1, seq, off, raw[off : off + 48]).encode()
                )
                seq += 1
            with self.assertRaises(ValueError):
                obj.finalize()


if __name__ == "__main__":
    unittest.main()
