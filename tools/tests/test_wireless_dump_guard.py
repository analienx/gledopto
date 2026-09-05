import importlib.util
import json
from pathlib import Path
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


proto = load("guard_test_proto", "glsd_dump_protocol.py")
guard = load("guard_test_guard", "glsd_dump_session_guard.py")


def make_info(session_id=0x11223344, old_size=96, old_base=0):
    stager_base = 0x40000 if old_base == 0 else 0
    if old_base == 0:
        bank_a_flag = proto.TELINK_INVALIDATED_FLAG
        bank_b_flag = proto.TELINK_STARTUP_FLAG
    else:
        bank_a_flag = proto.TELINK_STARTUP_FLAG
        bank_b_flag = proto.TELINK_INVALIDATED_FLAG
    info = proto.StagerInfo(
        protocol_version=proto.PROTOCOL_VERSION,
        stager_build_id=0xAABBCCDD,
        session_id=session_id,
        flash_jedec_id=0x1460C8,
        flash_size=proto.FLASH_SIZE_512K,
        bank_a_base=proto.BANK_A_BASE,
        bank_b_base=proto.BANK_B_BASE,
        bank_a_flag32=bank_a_flag,
        bank_b_flag32=bank_b_flag,
        inferred_stager_base=stager_base,
        inferred_old_base=old_base,
        old_declared_size=old_size,
        old_tail_crc32=0xDEADBEEF,
        old_reconstructed_crc_valid=True,
        allowed_read_start=0,
        allowed_read_length=old_size,
        journal_state=0xFF,
        rollback_compiled=False,
    )
    proto.validate_info(info)
    return info


class GuardedDumpTests(unittest.TestCase):
    def test_exact_pending_request_is_required_and_stale_duplicate_rejected(self):
        info = make_info()
        with tempfile.TemporaryDirectory() as td:
            obj = guard.GuardedPersistentDump.create(
                Path(td) / "s",
                info=info,
                target_ieee="0xa4c13850cfcdb3a4",
            )
            req = obj.next_request(seq=7)
            self.assertEqual((req.session_id, req.seq, req.offset, req.length),
                             (info.session_id, 7, 0, 48))
            payload = proto.DataFrame(
                info.session_id, 7, 0, b"A" * 48
            ).encode()
            obj.ingest_response(payload)
            self.assertIsNone(obj.ledger.pending)
            with self.assertRaises(ValueError):
                obj.ingest_response(payload)

    def test_wrong_session_seq_offset_and_length_all_fail_closed(self):
        info = make_info(old_size=144)
        cases = [
            (info.session_id ^ 1, 10, 0, b"A" * 48),
            (info.session_id, 11, 0, b"A" * 48),
            (info.session_id, 10, 48, b"A" * 48),
            (info.session_id, 10, 0, b"A" * 47),
        ]
        for idx, (sid, seq, off, data) in enumerate(cases):
            with self.subTest(case=idx), tempfile.TemporaryDirectory() as td:
                obj = guard.GuardedPersistentDump.create(
                    Path(td) / "s", info=info, target_ieee="fixture"
                )
                obj.next_request(seq=10)
                with self.assertRaises(ValueError):
                    obj.ingest_response(proto.DataFrame(sid, seq, off, data).encode())
                self.assertEqual(obj.inner.received, set())
                self.assertIsNotNone(obj.ledger.pending)

    def test_retry_replaces_sequence_and_late_old_response_is_stale(self):
        info = make_info()
        with tempfile.TemporaryDirectory() as td:
            obj = guard.GuardedPersistentDump.create(
                Path(td) / "s", info=info, target_ieee="fixture"
            )
            obj.next_request(seq=100)
            retry = obj.retry(seq=101)
            self.assertEqual(retry.seq, 101)
            late = proto.DataFrame(info.session_id, 100, 0, b"A" * 48).encode()
            with self.assertRaises(ValueError):
                obj.ingest_response(late)
            current = proto.DataFrame(info.session_id, 101, 0, b"A" * 48).encode()
            obj.ingest_response(current)
            self.assertIn(0, obj.inner.received)

    def test_resume_is_bound_to_fresh_info_session_and_geometry(self):
        info = make_info()
        state = None
        with tempfile.TemporaryDirectory() as td:
            state = Path(td) / "s"
            obj = guard.GuardedPersistentDump.create(
                state, info=info, target_ieee="fixture"
            )
            obj.next_request(seq=1)
            obj.ingest_response(
                proto.DataFrame(info.session_id, 1, 0, b"A" * 48).encode()
            )
            reopened = guard.GuardedPersistentDump.open(
                state, info=info, target_ieee="fixture"
            )
            self.assertEqual(reopened.inner.received, {0})

            with self.assertRaises(ValueError):
                guard.GuardedPersistentDump.open(
                    state,
                    info=make_info(session_id=info.session_id + 1),
                    target_ieee="fixture",
                )
            with self.assertRaises(ValueError):
                guard.GuardedPersistentDump.open(
                    state,
                    info=make_info(session_id=info.session_id, old_size=144),
                    target_ieee="fixture",
                )
            with self.assertRaises(ValueError):
                guard.GuardedPersistentDump.open(
                    state, info=info, target_ieee="different-ieee"
                )

    def test_bitmap_manifest_mix_is_rejected(self):
        info = make_info()
        with tempfile.TemporaryDirectory() as td:
            state = Path(td) / "s"
            guard.GuardedPersistentDump.create(
                state, info=info, target_ieee="fixture"
            )
            bitmap = json.loads((state / "received.bitmap.json").read_text())
            bitmap["received_offsets"] = [0]
            (state / "received.bitmap.json").write_text(json.dumps(bitmap))
            with self.assertRaises(ValueError):
                guard.GuardedPersistentDump.open(
                    state, info=info, target_ieee="fixture"
                )

    def test_final_short_chunk_request_uses_exact_remaining_length(self):
        info = make_info(old_size=100)
        with tempfile.TemporaryDirectory() as td:
            obj = guard.GuardedPersistentDump.create(
                Path(td) / "s", info=info, target_ieee="fixture"
            )
            for seq, off in [(1, 0), (2, 48)]:
                req = obj.next_request(seq=seq)
                self.assertEqual(req.offset, off)
                obj.ingest_response(
                    proto.DataFrame(info.session_id, seq, off, b"A" * 48).encode()
                )
            last = obj.next_request(seq=3)
            self.assertEqual((last.offset, last.length), (96, 4))
            obj.ingest_response(
                proto.DataFrame(info.session_id, 3, 96, b"B" * 4).encode()
            )
            self.assertEqual(obj.missing_offsets(), [])


if __name__ == "__main__":
    unittest.main()
