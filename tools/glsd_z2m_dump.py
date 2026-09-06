#!/usr/bin/env python3
"""Guarded GL-SD stock-application dump over the target-locked Z2M bridge.

The CLI knows only PING, INFO, READ and ABORT. It cannot serve OTA images and
has no flash-write, erase, reset, bind/group, or network-management operation.
The live transport is optional; core orchestration is dependency-free so it can
be exercised end-to-end with a synthetic transport in CI.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import secrets
import struct
import threading
import time
from typing import Protocol

import glsd_dump_protocol as proto
import glsd_dump_session_guard as guard
import glsd_z2m_bridge as bridge

PING_REQUEST = struct.Struct("<BI")
PING_RESPONSE = struct.Struct("<BIII")


class RpcTransport(Protocol):
    def rpc(self, op: str, payload: bytes, *, timeout_ms: int = 10000) -> bytes: ...

    def close(self) -> None: ...


class MqttBridgeTransport:
    """Synchronous request/response wrapper around the Z2M external extension."""

    def __init__(
        self,
        *,
        broker: str,
        port: int,
        base_topic: str,
        username: str | None = None,
        password: str | None = None,
        tls: bool = False,
        ca: str | None = None,
        connect_timeout: float = 10.0,
    ):
        try:
            from paho.mqtt import client as mqtt
        except ImportError as exc:  # pragma: no cover - exercised only on live host
            raise RuntimeError(
                "paho-mqtt is required for live MQTT transport; install tools/requirements-wireless-dump.txt"
            ) from exc

        self._mqtt_mod = mqtt
        self._base_topic = base_topic.rstrip("/")
        self._request_topic = f"{self._base_topic}/{bridge.REQUEST_TOPIC}"
        self._response_topic = f"{self._base_topic}/{bridge.RESPONSE_TOPIC}"
        self._condition = threading.Condition()
        self._responses: dict[str, str] = {}
        self._connected = threading.Event()
        self._connect_error: str | None = None

        try:
            self._client = mqtt.Client(
                mqtt.CallbackAPIVersion.VERSION2,
                client_id=f"glsd-dump-{secrets.token_hex(4)}",
            )
        except (AttributeError, TypeError):  # paho-mqtt 1.x compatibility
            self._client = mqtt.Client(client_id=f"glsd-dump-{secrets.token_hex(4)}")

        if username:
            self._client.username_pw_set(username, password)
        if tls:
            self._client.tls_set(ca_certs=ca)

        self._client.on_connect = self._on_connect
        self._client.on_message = self._on_message
        self._client.on_disconnect = self._on_disconnect
        self._client.connect(broker, port, keepalive=30)
        self._client.loop_start()
        if not self._connected.wait(connect_timeout):
            self.close()
            raise TimeoutError("timed out connecting to MQTT broker")
        if self._connect_error:
            self.close()
            raise RuntimeError(self._connect_error)

    def _on_connect(self, client, _userdata, _flags, reason_code, *_extra):
        try:
            rc = int(reason_code)
        except (TypeError, ValueError):
            rc = getattr(reason_code, "value", 1)
        if rc != 0:
            self._connect_error = f"MQTT connection rejected with code {reason_code}"
        else:
            client.subscribe(self._response_topic, qos=0)
        self._connected.set()

    def _on_disconnect(self, _client, _userdata, *args):
        # Do not turn a normal explicit close into an error. In-flight waits will
        # time out and report the transport failure without silently retrying a
        # request whose delivery state is unknown.
        if args:
            pass

    def _on_message(self, _client, _userdata, message):
        if message.topic != self._response_topic:
            return
        try:
            text = message.payload.decode("utf-8")
            import json

            obj = json.loads(text)
            request_id = obj.get("request_id") if isinstance(obj, dict) else None
        except (UnicodeDecodeError, ValueError):
            return
        if not isinstance(request_id, str):
            return
        with self._condition:
            self._responses[request_id] = text
            self._condition.notify_all()

    def rpc(self, op: str, payload: bytes, *, timeout_ms: int = 10000) -> bytes:
        request = bridge.new_request(op, payload, timeout_ms=timeout_ms)
        text = request.to_json()
        with self._condition:
            self._responses.pop(request.request_id, None)
        info = self._client.publish(self._request_topic, text, qos=0, retain=False)
        if getattr(info, "rc", 0) != 0:
            raise RuntimeError(f"MQTT publish failed with rc={info.rc}")

        deadline = time.monotonic() + (timeout_ms / 1000.0) + 2.0
        with self._condition:
            while request.request_id not in self._responses:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"timed out waiting for Z2M bridge {op} response")
                self._condition.wait(remaining)
            raw = self._responses.pop(request.request_id)
        return bridge.parse_response(raw, expected=request).payload

    def close(self) -> None:
        client = getattr(self, "_client", None)
        if client is not None:
            try:
                client.disconnect()
            finally:
                client.loop_stop()


class DumpRunner:
    def __init__(
        self,
        transport: RpcTransport,
        *,
        target_ieee: str = bridge.TARGET_IEEE,
        request_timeout_ms: int = 10000,
        retries: int = 3,
    ):
        if target_ieee.lower() != bridge.TARGET_IEEE:
            raise ValueError(f"this build is locked to {bridge.TARGET_IEEE}")
        if retries < 0 or retries > 20:
            raise ValueError("retries outside safe range 0..20")
        self.transport = transport
        self.target_ieee = bridge.TARGET_IEEE
        self.request_timeout_ms = request_timeout_ms
        self.retries = retries

    def probe(self) -> "proto.StagerInfo":
        nonce = secrets.randbits(32)
        ping_raw = self.transport.rpc(
            "ping",
            PING_REQUEST.pack(proto.PROTOCOL_VERSION, nonce),
            timeout_ms=self.request_timeout_ms,
        )
        if len(ping_raw) != PING_RESPONSE.size:
            raise ValueError("invalid PING response length")
        version, echoed, build_id, ping_session = PING_RESPONSE.unpack(ping_raw)
        if version != proto.PROTOCOL_VERSION or echoed != nonce:
            raise ValueError("PING freshness/version validation failed")

        info_raw = self.transport.rpc("info", b"", timeout_ms=self.request_timeout_ms)
        info = proto.StagerInfo.decode(info_raw)
        if info.session_id != ping_session:
            raise ValueError("PING and INFO session IDs differ")
        if info.stager_build_id != build_id:
            raise ValueError("PING and INFO stager build IDs differ")
        return info

    def _open_or_create(self, state_dir: Path, info: "proto.StagerInfo"):
        if (state_dir / guard.GUARD_JSON).exists():
            return guard.GuardedPersistentDump.open(
                state_dir, info=info, target_ieee=self.target_ieee
            )
        return guard.GuardedPersistentDump.create(
            state_dir, info=info, target_ieee=self.target_ieee
        )

    def dump(self, state_dir: Path) -> dict:
        info = self.probe()
        session = self._open_or_create(state_dir, info)

        while session.missing_offsets():
            request = session.next_request()
            attempt = 0
            while True:
                try:
                    data = self.transport.rpc(
                        "read",
                        request.encode(),
                        timeout_ms=self.request_timeout_ms,
                    )
                    break
                except (TimeoutError, ConnectionError, OSError, RuntimeError):
                    if attempt >= self.retries:
                        raise
                    attempt += 1
                    request = session.retry()
            # Protocol/identity/integrity mismatches are deliberately not hidden
            # behind retries. They are fail-closed evidence of the wrong response.
            session.ingest_response(data)

        return session.finalize()


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Target-locked GL-SD guarded wireless dump")
    p.add_argument("state_dir", type=Path)
    p.add_argument("--broker", required=True)
    p.add_argument("--port", type=int, default=1883)
    p.add_argument("--base-topic", default="zigbee2mqtt")
    p.add_argument("--username")
    p.add_argument(
        "--password-env",
        default="MQTT_PASSWORD",
        help="environment variable holding MQTT password; value is never printed",
    )
    p.add_argument("--tls", action="store_true")
    p.add_argument("--ca")
    p.add_argument("--request-timeout-ms", type=int, default=10000)
    p.add_argument("--retries", type=int, default=3)
    p.add_argument("--target", default=bridge.TARGET_IEEE)
    return p


def main(argv: list[str] | None = None) -> int:
    ns = _build_parser().parse_args(argv)
    if ns.target.lower() != bridge.TARGET_IEEE:
        raise SystemExit(f"ERROR: this tool is locked to target {bridge.TARGET_IEEE}")
    password = os.environ.get(ns.password_env) if ns.username else None
    transport = MqttBridgeTransport(
        broker=ns.broker,
        port=ns.port,
        base_topic=ns.base_topic,
        username=ns.username,
        password=password,
        tls=ns.tls,
        ca=ns.ca,
    )
    try:
        result = DumpRunner(
            transport,
            target_ieee=ns.target,
            request_timeout_ms=ns.request_timeout_ms,
            retries=ns.retries,
        ).dump(ns.state_dir)
    finally:
        transport.close()

    import json

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
