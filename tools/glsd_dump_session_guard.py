#!/usr/bin/env python3
"""Fail-closed live-session guard for GL-SD wireless stock extraction.

The lower-level host module intentionally accepts already-captured DATA frames.
This module is the canonical layer for a live dump transport: it binds persisted
state to a freshly validated INFO response and requires every DATA response to
match the single outstanding READ request exactly.

It performs no Zigbee I/O and exposes no flash write/erase operation.
"""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
from typing import Any

HERE = Path(__file__).resolve().parent


def _load(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


proto = _load("glsd_dump_protocol_guard_runtime", "glsd_dump_protocol.py")
host = _load("glsd_wireless_dump_host_guard_runtime", "glsd_wireless_dump_host.py")

GUARD_JSON = "guarded_session.json"
GUARD_VERSION = 2


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


def _binding_payload(
    info: "proto.StagerInfo", *, target_ieee: str, chunk_size: int
) -> dict[str, Any]:
    """Return the exact validated runtime identity/geometry bound to a resume."""
    proto.validate_info(info)
    if chunk_size < 1 or chunk_size > proto.MAX_CHUNK:
        raise ValueError("unsafe chunk size")
    return {
        "guard_version": GUARD_VERSION,
        "protocol_version": info.protocol_version,
        "stager_build_id": info.stager_build_id,
        "session_id": info.session_id,
        "flash_jedec_id": info.flash_jedec_id,
        "flash_size": info.flash_size,
        "bank_a_base": info.bank_a_base,
        "bank_b_base": info.bank_b_base,
        "bank_a_flag32": info.bank_a_flag32,
        "bank_b_flag32": info.bank_b_flag32,
        "inferred_stager_base": info.inferred_stager_base,
        "inferred_old_base": info.inferred_old_base,
        "old_declared_size": info.old_declared_size,
        "old_tail_crc32": info.old_tail_crc32,
        "old_reconstructed_crc_valid": bool(info.old_reconstructed_crc_valid),
        "allowed_read_start": info.allowed_read_start,
        "allowed_read_length": info.allowed_read_length,
        "target_ieee": target_ieee.lower(),
        "chunk_size": chunk_size,
    }


def _binding_sha256(payload: dict[str, Any]) -> str:
    canonical = json.dumps(
        payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(canonical).hexdigest()


class PendingReadLedger:
    """Exactly one outstanding READ with strictly increasing sequence values."""

    def __init__(
        self,
        *,
        session_id: int,
        total_len: int,
        chunk_size: int,
        last_issued_seq: int = -1,
    ):
        self.session_id = session_id & 0xFFFFFFFF
        self.total_len = total_len
        self.chunk_size = chunk_size
        if last_issued_seq < -1 or last_issued_seq > 0xFFFFFFFF:
            raise ValueError("invalid persisted sequence state")
        self.last_issued_seq = last_issued_seq
        self.pending: "proto.ReadRequest | None" = None

    def next_sequence(self) -> int:
        if self.last_issued_seq >= 0xFFFFFFFF:
            raise ValueError("READ sequence space exhausted")
        return self.last_issued_seq + 1

    def _claim_sequence(self, seq: int) -> None:
        if seq < 0 or seq > 0xFFFFFFFF:
            raise ValueError("READ sequence outside uint32 range")
        if seq <= self.last_issued_seq:
            raise ValueError("READ sequence must be strictly increasing")
        self.last_issued_seq = seq

    def begin(self, *, seq: int, offset: int, length: int) -> "proto.ReadRequest":
        if self.pending is not None:
            raise ValueError("a READ request is already outstanding")
        if offset < 0 or offset >= self.total_len or offset % self.chunk_size:
            raise ValueError("READ offset outside guarded image geometry")
        expected_len = min(self.chunk_size, self.total_len - offset)
        if expected_len <= 0 or length != expected_len:
            raise ValueError("READ length does not match guarded chunk geometry")
        self._claim_sequence(seq)
        req = proto.ReadRequest(self.session_id, seq, offset, length)
        req.encode()
        self.pending = req
        return req

    def retry(self, *, seq: int) -> "proto.ReadRequest":
        """Replace the in-flight sequence; a late old response becomes stale."""
        if self.pending is None:
            raise ValueError("no pending READ to retry")
        old = self.pending
        self._claim_sequence(seq)
        req = proto.ReadRequest(old.session_id, seq, old.offset, old.length)
        req.encode()
        self.pending = req
        return req

    def assert_matches(self, frame: "proto.DataFrame") -> None:
        if self.pending is None:
            raise ValueError("DATA response received with no pending READ")
        req = self.pending
        got = (frame.session_id, frame.seq, frame.offset, len(frame.data))
        want = (req.session_id, req.seq, req.offset, req.length)
        if got != want:
            raise ValueError(
                "DATA response does not match pending READ: "
                f"got={got!r} expected={want!r}"
            )

    def complete(self, frame: "proto.DataFrame") -> None:
        self.assert_matches(frame)
        self.pending = None

    def abandon(self) -> None:
        self.pending = None


class GuardedPersistentDump:
    """Live-safe orchestration around the lower-level persistent dump store."""

    def __init__(
        self,
        inner: "host.PersistentDump",
        *,
        info: "proto.StagerInfo",
        target_ieee: str,
        manifest: dict[str, Any],
    ):
        self.inner = inner
        self.info = info
        self.target_ieee = target_ieee.lower()
        self.manifest = manifest
        last_seq = int(manifest.get("last_issued_seq", -1))
        self.ledger = PendingReadLedger(
            session_id=info.session_id,
            total_len=info.allowed_read_length,
            chunk_size=inner.chunk_size,
            last_issued_seq=last_seq,
        )

    @classmethod
    def create(
        cls,
        state_dir: Path,
        *,
        info: "proto.StagerInfo",
        target_ieee: str,
        chunk_size: int = proto.MAX_CHUNK,
    ) -> "GuardedPersistentDump":
        binding = _binding_payload(
            info, target_ieee=target_ieee, chunk_size=chunk_size
        )
        inner = host.PersistentDump.create(
            state_dir,
            session_id=info.session_id,
            total_len=info.allowed_read_length,
            chunk_size=chunk_size,
            target_ieee=target_ieee.lower(),
        )
        manifest = {
            "format_version": GUARD_VERSION,
            "binding": binding,
            "binding_sha256": _binding_sha256(binding),
            "last_issued_seq": -1,
        }
        _atomic_json(state_dir / GUARD_JSON, manifest)
        return cls(
            inner,
            info=info,
            target_ieee=target_ieee,
            manifest=manifest,
        )

    @classmethod
    def open(
        cls,
        state_dir: Path,
        *,
        info: "proto.StagerInfo",
        target_ieee: str,
    ) -> "GuardedPersistentDump":
        inner = host.PersistentDump.open(state_dir)
        manifest = json.loads((state_dir / GUARD_JSON).read_text(encoding="utf-8"))
        if int(manifest.get("format_version", -1)) != GUARD_VERSION:
            raise ValueError("unsupported guarded-session state version")
        expected = _binding_payload(
            info, target_ieee=target_ieee, chunk_size=inner.chunk_size
        )
        expected_sha = _binding_sha256(expected)
        if manifest.get("binding") != expected:
            raise ValueError("persisted dump is bound to different INFO/session geometry")
        if manifest.get("binding_sha256") != expected_sha:
            raise ValueError("persisted session binding checksum mismatch")
        try:
            last_seq = int(manifest["last_issued_seq"])
        except (KeyError, TypeError, ValueError) as exc:
            raise ValueError("invalid persisted sequence state") from exc
        if last_seq < -1 or last_seq > 0xFFFFFFFF:
            raise ValueError("invalid persisted sequence state")
        if int(inner.meta.get("protocol_version", -1)) != info.protocol_version:
            raise ValueError("session.json protocol version does not match INFO")
        if inner.session_id != info.session_id:
            raise ValueError("session.json session id does not match INFO")
        if inner.total_len != info.allowed_read_length:
            raise ValueError("session.json dump length does not match INFO")
        if str(inner.meta.get("target_ieee", "")).lower() != target_ieee.lower():
            raise ValueError("session.json target IEEE mismatch")
        return cls(
            inner,
            info=info,
            target_ieee=target_ieee,
            manifest=manifest,
        )

    def _persist_sequence_state(self) -> None:
        self.manifest["last_issued_seq"] = self.ledger.last_issued_seq
        _atomic_json(self.inner.state_dir / GUARD_JSON, self.manifest)

    def _commit_issued_request(self, issue_fn):
        old_pending = self.ledger.pending
        old_last = self.ledger.last_issued_seq
        req = issue_fn()
        try:
            self._persist_sequence_state()
        except Exception:
            self.ledger.pending = old_pending
            self.ledger.last_issued_seq = old_last
            raise
        return req

    def next_request(self, *, seq: int | None = None) -> "proto.ReadRequest":
        missing = self.inner.missing_offsets()
        if not missing:
            raise ValueError("dump already complete")
        offset = missing[0]
        length = min(self.inner.chunk_size, self.inner.total_len - offset)
        chosen = self.ledger.next_sequence() if seq is None else seq
        return self._commit_issued_request(
            lambda: self.ledger.begin(seq=chosen, offset=offset, length=length)
        )

    def retry(self, *, seq: int | None = None) -> "proto.ReadRequest":
        chosen = self.ledger.next_sequence() if seq is None else seq
        return self._commit_issued_request(lambda: self.ledger.retry(seq=chosen))

    def ingest_response(self, payload: bytes) -> dict[str, Any]:
        frame = proto.DataFrame.decode(payload)
        self.ledger.assert_matches(frame)
        result = self.inner.ingest(payload)
        self.ledger.complete(frame)
        return result

    def missing_offsets(self) -> list[int]:
        return self.inner.missing_offsets()

    def finalize(self) -> dict[str, Any]:
        if self.ledger.pending is not None:
            raise ValueError("cannot finalize with a READ still outstanding")
        return self.inner.finalize()


__all__ = [
    "GUARD_JSON",
    "GUARD_VERSION",
    "PendingReadLedger",
    "GuardedPersistentDump",
]
