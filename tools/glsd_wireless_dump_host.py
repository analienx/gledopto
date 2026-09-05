#!/usr/bin/env python3
"""Persistent offline host state for GL-SD wireless application extraction.

This deliberately stops short of Zigbee transport. It consumes already-captured
DATA payloads, persists them crash-safely enough for resume, reconstructs only
the standard Telink boot-marker byte, and refuses finalization unless the exact
Telink application CRC validates.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import sys
from typing import Any

HERE = Path(__file__).resolve().parent


def _load(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


proto = _load("glsd_dump_protocol_runtime", "glsd_dump_protocol.py")
forensics = _load("telink_ota_forensics_runtime", "telink_ota_forensics.py")

SESSION_JSON = "session.json"
PARTIAL_BIN = "raw_after_ota.bin.part"
RAW_BIN = "raw_after_ota.bin"
BITMAP_JSON = "received.bitmap.json"
CHECKSUMS_JSONL = "chunk_checksums.jsonl"
RECON_BIN = "reconstructed_stock.bin"
VALIDATION_JSON = "validation.json"


def _atomic_json(path: Path, obj: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=path.parent, delete=False
    ) as f:
        json.dump(obj, f, indent=2, sort_keys=True)
        f.write("\n")
        tmp = Path(f.name)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


class PersistentDump:
    def __init__(self, state_dir: Path, metadata: dict[str, Any]):
        self.state_dir = state_dir
        self.meta = metadata
        self.session_id = int(metadata["session_id"])
        self.total_len = int(metadata["total_len"])
        self.chunk_size = int(metadata.get("chunk_size", proto.MAX_CHUNK))
        if self.chunk_size < 1 or self.chunk_size > proto.MAX_CHUNK:
            raise ValueError("unsafe chunk size in session metadata")
        if self.total_len <= 0 or self.total_len >= proto.APP_LIMIT:
            raise ValueError("unsafe total length in session metadata")
        self.received: set[int] = set()

    @classmethod
    def create(
        cls,
        state_dir: Path,
        *,
        session_id: int,
        total_len: int,
        chunk_size: int = proto.MAX_CHUNK,
        target_ieee: str,
    ) -> "PersistentDump":
        state_dir.mkdir(parents=True, exist_ok=True)
        if any(state_dir.iterdir()):
            raise ValueError("state directory must be empty for a new session")
        meta = {
            "protocol_version": proto.PROTOCOL_VERSION,
            "session_id": session_id,
            "total_len": total_len,
            "chunk_size": chunk_size,
            "target_ieee": target_ieee,
        }
        obj = cls(state_dir, meta)
        _atomic_json(state_dir / SESSION_JSON, meta)
        with (state_dir / PARTIAL_BIN).open("wb") as f:
            f.truncate(total_len)
            f.flush()
            os.fsync(f.fileno())
        _atomic_json(state_dir / BITMAP_JSON, {"received_offsets": []})
        with (state_dir / CHECKSUMS_JSONL).open("w", encoding="utf-8") as f:
            f.flush()
            os.fsync(f.fileno())
        return obj

    @classmethod
    def open(cls, state_dir: Path) -> "PersistentDump":
        meta = json.loads((state_dir / SESSION_JSON).read_text())
        obj = cls(state_dir, meta)
        bitmap = json.loads((state_dir / BITMAP_JSON).read_text())
        obj.received = {int(x) for x in bitmap.get("received_offsets", [])}
        obj._validate_bitmap()
        part = state_dir / PARTIAL_BIN
        if not part.exists() or part.stat().st_size != obj.total_len:
            raise ValueError("partial file missing or wrong size")
        obj._validate_persisted_chunks()
        return obj

    @property
    def chunk_count(self) -> int:
        return (self.total_len + self.chunk_size - 1) // self.chunk_size

    def _validate_bitmap(self) -> None:
        for off in self.received:
            if off < 0 or off >= self.total_len or off % self.chunk_size:
                raise ValueError(f"invalid persisted offset {off}")

    def _read_checksum_records(self) -> dict[int, list[dict[str, Any]]]:
        path = self.state_dir / CHECKSUMS_JSONL
        if not path.exists():
            if self.received:
                raise ValueError("checksum journal missing for persisted chunks")
            return {}
        records: dict[int, list[dict[str, Any]]] = {}
        for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip():
                continue
            try:
                rec = json.loads(line)
                off = int(rec["offset"])
                length = int(rec["length"])
                sha256 = str(rec["sha256"])
                crc32 = str(rec["crc32"])
            except (KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
                raise ValueError(f"invalid checksum journal record at line {lineno}") from exc
            if off < 0 or off >= self.total_len or off % self.chunk_size:
                raise ValueError(f"checksum journal has invalid offset {off}")
            expected_len = min(self.chunk_size, self.total_len - off)
            if length != expected_len:
                raise ValueError(f"checksum journal has invalid length at offset {off}")
            if len(sha256) != 64:
                raise ValueError(f"checksum journal has invalid sha256 at offset {off}")
            try:
                int(sha256, 16)
                int(crc32, 16)
            except ValueError as exc:
                raise ValueError(f"checksum journal has malformed digest at offset {off}") from exc
            records.setdefault(off, []).append(rec)
        return records

    def _validate_persisted_chunks(self) -> None:
        """Prove every bitmap-committed chunk still matches the fsynced journal.

        The journal is written before the bitmap. Therefore extra journal rows are
        legitimate after a crash before bitmap commit and are ignored for resume.
        Any bitmap-committed offset, however, must have a consistent journal record
        and the current bytes on disk must match its SHA-256 and CRC32.
        """
        records = self._read_checksum_records()
        part = self.state_dir / PARTIAL_BIN
        with part.open("rb") as f:
            for off in sorted(self.received):
                expected_len = min(self.chunk_size, self.total_len - off)
                rows = records.get(off)
                if not rows:
                    raise ValueError(f"persisted chunk {off} has no checksum journal record")
                row_pairs = {
                    (str(r["sha256"]).lower(), str(r["crc32"]).lower()) for r in rows
                }
                if len(row_pairs) != 1:
                    raise ValueError(f"conflicting checksum journal history at offset {off}")
                expected_sha, expected_crc_text = next(iter(row_pairs))
                f.seek(off)
                data = f.read(expected_len)
                if len(data) != expected_len:
                    raise ValueError(f"persisted chunk {off} is truncated")
                actual_sha = hashlib.sha256(data).hexdigest()
                actual_crc = binascii.crc32(data) & 0xFFFFFFFF
                if actual_sha != expected_sha:
                    raise ValueError(f"persisted chunk {off} sha256 mismatch")
                try:
                    expected_crc = int(expected_crc_text, 16) & 0xFFFFFFFF
                except ValueError as exc:
                    raise ValueError(f"persisted chunk {off} crc32 is malformed") from exc
                if actual_crc != expected_crc:
                    raise ValueError(f"persisted chunk {off} crc32 mismatch")

    def missing_offsets(self) -> list[int]:
        return [
            off
            for off in range(0, self.total_len, self.chunk_size)
            if off not in self.received
        ]

    def ingest(self, payload: bytes) -> dict[str, Any]:
        frame = proto.DataFrame.decode(payload)
        if frame.session_id != self.session_id:
            raise ValueError("captured frame session does not match session.json")
        if frame.offset % self.chunk_size:
            raise ValueError("captured frame offset is not chunk-aligned")
        expected_len = min(self.chunk_size, self.total_len - frame.offset)
        if expected_len <= 0 or len(frame.data) != expected_len:
            raise ValueError("captured frame length/range mismatch")
        if frame.status != 0:
            raise ValueError(f"device returned READ status {frame.status}")

        part = self.state_dir / PARTIAL_BIN
        if frame.offset in self.received:
            with part.open("rb") as f:
                f.seek(frame.offset)
                existing = f.read(len(frame.data))
            if existing != frame.data:
                raise ValueError("duplicate captured chunk differs from disk")
            return {"duplicate": True, "offset": frame.offset}

        with part.open("r+b", buffering=0) as f:
            f.seek(frame.offset)
            f.write(frame.data)
            f.flush()
            os.fsync(f.fileno())

        digest = hashlib.sha256(frame.data).hexdigest()
        with (self.state_dir / CHECKSUMS_JSONL).open("a", encoding="utf-8") as log:
            log.write(
                json.dumps(
                    {
                        "seq": frame.seq,
                        "offset": frame.offset,
                        "length": len(frame.data),
                        "crc32": f"0x{frame.crc32:08X}",
                        "sha256": digest,
                    },
                    sort_keys=True,
                )
                + "\n"
            )
            log.flush()
            os.fsync(log.fileno())

        self.received.add(frame.offset)
        _atomic_json(
            self.state_dir / BITMAP_JSON,
            {"received_offsets": sorted(self.received)},
        )
        return {"duplicate": False, "offset": frame.offset, "sha256": digest}

    def finalize(self) -> dict[str, Any]:
        missing = self.missing_offsets()
        if missing:
            raise ValueError(f"dump incomplete; {len(missing)} chunks missing")

        self._validate_persisted_chunks()
        raw = (self.state_dir / PARTIAL_BIN).read_bytes()
        if len(raw) != self.total_len:
            raise ValueError("partial dump size changed unexpectedly")

        reconstructed, reconstruction = (
            forensics.reconstruct_invalidated_telink_app(raw)
        )
        # Post-stager old bank is expected to have exactly the Telink-invalidated
        # marker byte. A zero-diff result means this was not the expected source
        # bank/state and is therefore not accepted by the extraction workflow.
        diffs = reconstruction["diffs"]
        if len(diffs) != 1 or diffs[0]["offset"] != forensics.TELINK_MARKER_OFFSET:
            raise ValueError(
                "expected exactly one reconstructed Telink marker byte at +0x08"
            )
        if not reconstruction["validation"].get("valid"):
            raise ValueError("reconstructed stock application failed Telink validation")

        raw_path = self.state_dir / RAW_BIN
        raw_path.write_bytes(raw)
        recon_path = self.state_dir / RECON_BIN
        recon_path.write_bytes(reconstructed)

        validation = {
            "protocol_version": proto.PROTOCOL_VERSION,
            "target_ieee": self.meta["target_ieee"],
            "session_id": self.session_id,
            "total_len": self.total_len,
            "chunk_size": self.chunk_size,
            "received_chunks": self.chunk_count,
            "raw_sha256": hashlib.sha256(raw).hexdigest(),
            "reconstructed_sha256": hashlib.sha256(reconstructed).hexdigest(),
            "reconstruction_diffs": diffs,
            "telink_application": reconstruction["validation"],
            "pass": True,
        }
        _atomic_json(self.state_dir / VALIDATION_JSON, validation)
        return validation


def _cmd_create(ns) -> int:
    PersistentDump.create(
        ns.state_dir,
        session_id=ns.session_id,
        total_len=ns.total_len,
        chunk_size=ns.chunk_size,
        target_ieee=ns.target_ieee,
    )
    print(json.dumps({"created": str(ns.state_dir)}, indent=2))
    return 0


def _cmd_ingest(ns) -> int:
    obj = PersistentDump.open(ns.state_dir)
    for line in ns.jsonl.read_text().splitlines():
        if not line.strip():
            continue
        item = json.loads(line)
        obj.ingest(bytes.fromhex(item["payload_hex"]))
    print(
        json.dumps(
            {
                "received": len(obj.received),
                "expected": obj.chunk_count,
                "missing_offsets": obj.missing_offsets(),
                "complete": not obj.missing_offsets(),
            },
            indent=2,
        )
    )
    return 0


def _cmd_status(ns) -> int:
    obj = PersistentDump.open(ns.state_dir)
    print(
        json.dumps(
            {
                **obj.meta,
                "received": len(obj.received),
                "expected": obj.chunk_count,
                "missing_offsets": obj.missing_offsets(),
                "complete": not obj.missing_offsets(),
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


def _cmd_finalize(ns) -> int:
    obj = PersistentDump.open(ns.state_dir)
    print(json.dumps(obj.finalize(), indent=2, sort_keys=True))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Persistent offline host for captured GL-SD dump frames"
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("create")
    c.add_argument("state_dir", type=Path)
    c.add_argument("--session-id", type=lambda x: int(x, 0), required=True)
    c.add_argument("--total-len", type=lambda x: int(x, 0), required=True)
    c.add_argument("--chunk-size", type=int, default=proto.MAX_CHUNK)
    c.add_argument("--target-ieee", required=True)
    c.set_defaults(func=_cmd_create)

    i = sub.add_parser("ingest-jsonl")
    i.add_argument("state_dir", type=Path)
    i.add_argument("jsonl", type=Path)
    i.set_defaults(func=_cmd_ingest)

    s = sub.add_parser("status")
    s.add_argument("state_dir", type=Path)
    s.set_defaults(func=_cmd_status)

    f = sub.add_parser("finalize")
    f.add_argument("state_dir", type=Path)
    f.set_defaults(func=_cmd_finalize)

    ns = ap.parse_args()
    return ns.func(ns)


if __name__ == "__main__":
    raise SystemExit(main())
