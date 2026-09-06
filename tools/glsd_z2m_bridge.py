#!/usr/bin/env python3
"""Strict JSON RPC contract for the target-locked Zigbee2MQTT dump bridge.

This module is transport-only. It has no Zigbee or OTA code and can be tested
without a broker. The companion Z2M extension is hard-coded to the same IEEE.
"""
from __future__ import annotations

from dataclasses import dataclass
import json
import uuid

BRIDGE_PROTOCOL_VERSION = 1
TARGET_IEEE = "0xa4c13850cfcdb3a4"
REQUEST_TOPIC = "bridge/request/glsd_wireless_dump"
RESPONSE_TOPIC = "bridge/response/glsd_wireless_dump"
OPS_AND_LENGTHS = {"ping": 5, "info": 0, "read": 13, "abort": 0}


@dataclass(frozen=True)
class BridgeRequest:
    request_id: str
    op: str
    payload: bytes
    timeout_ms: int

    def to_json(self) -> str:
        if self.op not in OPS_AND_LENGTHS:
            raise ValueError(f"unsupported bridge operation {self.op!r}")
        if len(self.payload) != OPS_AND_LENGTHS[self.op]:
            raise ValueError(
                f"{self.op} payload must be exactly {OPS_AND_LENGTHS[self.op]} bytes"
            )
        if not (1000 <= self.timeout_ms <= 30000):
            raise ValueError("timeout_ms outside bridge contract")
        return json.dumps(
            {
                "protocol_version": BRIDGE_PROTOCOL_VERSION,
                "request_id": self.request_id,
                "target": TARGET_IEEE,
                "op": self.op,
                "payload_hex": self.payload.hex(),
                "timeout_ms": self.timeout_ms,
            },
            separators=(",", ":"),
            sort_keys=True,
        )


@dataclass(frozen=True)
class BridgeResponse:
    request_id: str
    op: str
    payload: bytes


def new_request(op: str, payload: bytes, *, timeout_ms: int = 10000) -> BridgeRequest:
    return BridgeRequest(uuid.uuid4().hex, op, bytes(payload), timeout_ms)


def parse_response(raw: str | bytes, *, expected: BridgeRequest) -> BridgeResponse:
    if isinstance(raw, bytes):
        raw = raw.decode("utf-8")
    try:
        obj = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError("bridge response is not valid UTF-8 JSON") from exc
    if not isinstance(obj, dict):
        raise ValueError("bridge response must be an object")
    if obj.get("protocol_version") != BRIDGE_PROTOCOL_VERSION:
        raise ValueError("bridge response protocol version mismatch")
    if str(obj.get("target", "")).lower() != TARGET_IEEE:
        raise ValueError("bridge response target mismatch")
    if obj.get("request_id") != expected.request_id:
        raise ValueError("bridge response request_id mismatch")
    if obj.get("op") != expected.op:
        raise ValueError("bridge response operation mismatch")
    if obj.get("status") != "ok":
        message = obj.get("error", "unspecified bridge error")
        raise RuntimeError(f"Z2M dump bridge rejected request: {message}")
    value = obj.get("payload_hex")
    if not isinstance(value, str) or len(value) % 2 or not all(
        c in "0123456789abcdefABCDEF" for c in value
    ):
        raise ValueError("bridge response payload_hex is malformed")
    return BridgeResponse(expected.request_id, expected.op, bytes.fromhex(value))


__all__ = [
    "BRIDGE_PROTOCOL_VERSION",
    "TARGET_IEEE",
    "REQUEST_TOPIC",
    "RESPONSE_TOPIC",
    "BridgeRequest",
    "BridgeResponse",
    "new_request",
    "parse_response",
]
