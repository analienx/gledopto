import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import glsd_dump_protocol as proto
import glsd_z2m_bridge as bridge
import glsd_z2m_dump as dump
import telink_ota_forensics as forensics


def make_invalidated_stock(size=240):
    b = bytearray([0xA5] * size)
    struct.pack_into("<I", b, 2, 0x26013001)
    b[6:8] = b"\x5D\x02"
    b[8:12] = b"\x00\x4E\x4C\x54"
    struct.pack_into("<H", b, 0x12, 0x124F)
    struct.pack_into("<H", b, 0x14, 0x1416)
    struct.pack_into("<I", b, 0x18, size)
    valid = bytearray(b)
    valid[8] = 0x4B
    crc = forensics.telink_xcrc32(valid[:-4])
    struct.pack_into("<I", b, size - 4, crc)
    return bytes(b)


def encode_info(info):
    return proto.INFO_RESPONSE.pack(
        info.protocol_version,
        info.stager_build_id,
        info.session_id,
        info.flash_jedec_id,
        info.flash_size,
        info.bank_a_base,
        info.bank_b_base,
        info.bank_a_flag32,
        info.bank_b_flag32,
        info.inferred_stager_base,
        info.inferred_old_base,
        info.old_declared_size,
        info.old_tail_crc32,
        int(info.old_reconstructed_crc_valid),
        info.allowed_read_start,
        info.allowed_read_length,
        info.journal_state,
        int(info.rollback_compiled),
    )


class SyntheticStagerTransport:
    def __init__(self, image, *, fail_once_offset=48):
        self.image = image
        self.build_id = 0x20260905
        self.session_id = 0x11223344
        self.fail_once_offset = fail_once_offset
        self.failed = False
        self.read_sequences = []
        stored_crc = struct.unpack_from("<I", image, len(image) - 4)[0]
        self.info = proto.StagerInfo(
            protocol_version=proto.PROTOCOL_VERSION,
            stager_build_id=self.build_id,
            session_id=self.session_id,
            flash_jedec_id=0x1460C8,
            flash_size=proto.FLASH_SIZE_512K,
            bank_a_base=proto.BANK_A_BASE,
            bank_b_base=proto.BANK_B_BASE,
            bank_a_flag32=proto.TELINK_INVALIDATED_FLAG,
            bank_b_flag32=proto.TELINK_STARTUP_FLAG,
            inferred_stager_base=proto.BANK_B_BASE,
            inferred_old_base=proto.BANK_A_BASE,
            old_declared_size=len(image),
            old_tail_crc32=stored_crc,
            old_reconstructed_crc_valid=True,
            allowed_read_start=0,
            allowed_read_length=len(image),
            journal_state=0xFF,
            rollback_compiled=False,
        )
        proto.validate_info(self.info)

    def rpc(self, op, payload, *, timeout_ms=10000):
        self.assert_timeout(timeout_ms)
        if op == "ping":
            version, nonce = dump.PING_REQUEST.unpack(payload)
            return dump.PING_RESPONSE.pack(
                version, nonce, self.build_id, self.session_id
            )
        if op == "info":
            self.assertEqualBytes(payload, b"")
            return encode_info(self.info)
        if op == "read":
            req = proto.ReadRequest.decode(payload)
            self.read_sequences.append((req.offset, req.seq))
            if req.session_id != self.session_id:
                raise AssertionError("runner sent wrong session")
            if req.offset == self.fail_once_offset and not self.failed:
                self.failed = True
                raise TimeoutError("synthetic dropped DATA response")
            data = self.image[req.offset : req.offset + req.length]
            return proto.DataFrame(
                self.session_id, req.seq, req.offset, data
            ).encode()
        if op == "abort":
            self.assertEqualBytes(payload, b"")
            return b""
        raise AssertionError(f"unexpected operation {op}")

    @staticmethod
    def assert_timeout(value):
        if not 1000 <= value <= 30000:
            raise AssertionError("timeout escaped bridge range")

    @staticmethod
    def assertEqualBytes(a, b):
        if a != b:
            raise AssertionError(f"payload differs: {a!r} != {b!r}")

    def close(self):
        pass


class Z2MDumpIntegrationTests(unittest.TestCase):
    def test_bridge_contract_is_exact_target_and_length_locked(self):
        request = bridge.new_request("read", b"X" * 13, timeout_ms=2500)
        encoded = json.loads(request.to_json())
        self.assertEqual(encoded["target"], bridge.TARGET_IEEE)
        self.assertEqual(encoded["payload_hex"], (b"X" * 13).hex())
        with self.assertRaises(ValueError):
            bridge.BridgeRequest("x", "read", b"X" * 12, 2500).to_json()

        good = json.dumps(
            {
                "protocol_version": 1,
                "request_id": request.request_id,
                "target": bridge.TARGET_IEEE,
                "op": "read",
                "status": "ok",
                "payload_hex": "00",
            }
        )
        self.assertEqual(bridge.parse_response(good, expected=request).payload, b"\x00")

        wrong_target = json.loads(good)
        wrong_target["target"] = "0x0000000000000001"
        with self.assertRaises(ValueError):
            bridge.parse_response(json.dumps(wrong_target), expected=request)

    def test_synthetic_end_to_end_dump_retries_with_new_seq_and_finalizes(self):
        raw = make_invalidated_stock()
        transport = SyntheticStagerTransport(raw)
        runner = dump.DumpRunner(transport, request_timeout_ms=2000, retries=2)
        with tempfile.TemporaryDirectory() as td:
            state = Path(td) / "dump"
            result = runner.dump(state)
            self.assertTrue(result["pass"])
            self.assertEqual((state / "raw_after_ota.bin").read_bytes(), raw)
            reconstructed = (state / "reconstructed_stock.bin").read_bytes()
            self.assertEqual(reconstructed[8], 0x4B)
            self.assertEqual(raw[:8] + raw[9:], reconstructed[:8] + reconstructed[9:])

            seqs_at_48 = [seq for off, seq in transport.read_sequences if off == 48]
            self.assertEqual(len(seqs_at_48), 2)
            self.assertGreater(seqs_at_48[1], seqs_at_48[0])

    def test_ping_info_session_disagreement_fails_before_any_read(self):
        raw = make_invalidated_stock()

        class BadInfoTransport(SyntheticStagerTransport):
            def rpc(self, op, payload, *, timeout_ms=10000):
                if op == "info":
                    bad = proto.StagerInfo(
                        **{**self.info.__dict__, "session_id": self.session_id + 1}
                    )
                    return encode_info(bad)
                return super().rpc(op, payload, timeout_ms=timeout_ms)

        transport = BadInfoTransport(raw, fail_once_offset=-1)
        with self.assertRaises(ValueError):
            dump.DumpRunner(transport).probe()
        self.assertEqual(transport.read_sequences, [])


if __name__ == "__main__":
    unittest.main()
