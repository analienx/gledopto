#!/usr/bin/env python3
"""Reversible GL-SD-301P migration helper for Tuya OTA extraction.

This utility is intentionally read-only with respect to Tuya firmware updates:
it never implements the Tuya POST endpoint that starts an OTA upgrade.

Primary workflow:
  1. snapshot-z2m   - preserve the production device's Zigbee2MQTT state
  2. tuya-watch     - wait for the physically-reset device to appear in Tuya,
                      query firmware metadata, download any vendor URL, hash and parse it
  3. restore-check  - after physical reset/rejoin to Z2M, compare live state to snapshot
  4. restore-plan   - generate MQTT commands for any missing groups/binds/reporting

Optional:
  --pcap-interface starts tshark during tuya-watch for encrypted traffic evidence.
  This does NOT decrypt modern Tuya TLS; cloud API extraction is the primary method.
"""
from __future__ import annotations

import argparse
import base64
import contextlib
import dataclasses
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import time
from typing import Any, Iterable
from urllib.parse import urlparse
from urllib.request import Request, urlopen

TARGET_IEEE = "0xa4c13850cfcdb3a4"
TARGET_FRIENDLY = "LivingRoomCircleLightDimmer"
TARGET_MODEL = "GL-SD-301P"
TARGET_ENDPOINT = 11
DEFAULT_Z2M_DIR = "/config/zigbee2mqtt"
DEFAULT_SSH = "ha"

TUYA_ENDPOINTS = {
    "eu": "https://openapi.tuyaeu.com",
    "us": "https://openapi.tuyaus.com",
    "cn": "https://openapi.tuyacn.com",
    "in": "https://openapi.tuyain.com",
}

OTA_MAGIC = 0x0BEEF11E
URL_KEYS = {"url", "fw_url", "download_url", "firmware_url", "file_url"}
SENSITIVE_KEYS = {
    "local_key", "localKey", "access_token", "refresh_token", "token", "secret",
    "access_key", "client_secret", "password", "passwd", "network_key", "networkKey",
}


class ToolError(RuntimeError):
    pass


def utc_stamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def dump_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")


def sanitize(value: Any) -> Any:
    if isinstance(value, dict):
        out = {}
        for k, v in value.items():
            if str(k) in SENSITIVE_KEYS:
                out[k] = "<redacted>"
            else:
                out[k] = sanitize(v)
        return out
    if isinstance(value, list):
        return [sanitize(v) for v in value]
    return value


def ssh_cat(alias: str, remote_path: str) -> bytes:
    cmd = ["ssh", alias, "cat", remote_path]
    cp = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if cp.returncode != 0:
        raise ToolError(f"ssh cat failed for {remote_path}: {cp.stderr.decode(errors='replace').strip()}")
    return cp.stdout


def ssh_exists(alias: str, remote_path: str) -> bool:
    cp = subprocess.run(["ssh", alias, "test", "-f", remote_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return cp.returncode == 0


def parse_database_db(data: bytes) -> list[dict[str, Any]]:
    text = data.decode("utf-8", errors="replace")
    records: list[dict[str, Any]] = []
    # Current Z2M database.db is newline-delimited JSON, but accept a JSON array too.
    stripped = text.strip()
    if not stripped:
        return records
    if stripped.startswith("["):
        obj = json.loads(stripped)
        if isinstance(obj, list):
            return [x for x in obj if isinstance(x, dict)]
    for lineno, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ToolError(f"database.db line {lineno} is not JSON: {exc}") from exc
        if isinstance(obj, dict):
            records.append(obj)
    return records


def ieee_of(rec: dict[str, Any]) -> str | None:
    for k in ("ieeeAddr", "ieee_address", "ieee"):
        v = rec.get(k)
        if isinstance(v, str):
            return v.lower()
    return None


def endpoint_map(rec: dict[str, Any]) -> dict[str, Any]:
    eps = rec.get("endpoints") or rec.get("endpoints_by_id") or {}
    return eps if isinstance(eps, dict) else {}


def find_device_record(records: list[dict[str, Any]], ieee: str) -> dict[str, Any]:
    ieee = ieee.lower()
    matches = [r for r in records if ieee_of(r) == ieee]
    if not matches:
        raise ToolError(f"Device {ieee} not found in database.db")
    # Prefer Device record when multiple records contain same IEEE.
    for r in matches:
        if str(r.get("type", "")).lower() in {"router", "enddevice", "end_device", "device"}:
            return r
    return matches[0]


def target_groups_from_db(records: list[dict[str, Any]], ieee: str) -> list[dict[str, Any]]:
    ieee = ieee.lower()
    out: list[dict[str, Any]] = []
    for rec in records:
        if str(rec.get("type", "")).lower() != "group":
            continue
        members = rec.get("members") or []
        for m in members:
            if isinstance(m, dict) and str(m.get("deviceIeeeAddr", m.get("ieee_address", ""))).lower() == ieee:
                out.append(rec)
                break
    return out


def device_index(records: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    out: dict[str, dict[str, Any]] = {}
    for rec in records:
        ieee = ieee_of(rec)
        if ieee:
            out[ieee] = rec
    return out


def normalize_binding(binding: dict[str, Any]) -> dict[str, Any]:
    cluster = binding.get("cluster")
    # Legacy database format uses fields at top level; bridge/devices uses target object.
    if "target" in binding and isinstance(binding["target"], dict):
        t = binding["target"]
        return {
            "cluster": cluster,
            "type": t.get("type"),
            "ieee": str(t.get("ieee_address", "")).lower() or None,
            "endpoint": t.get("endpoint"),
            "group": t.get("id"),
        }
    return {
        "cluster": cluster,
        "type": binding.get("type"),
        "ieee": str(binding.get("deviceIeeeAddress", binding.get("ieee_address", ""))).lower() or None,
        "endpoint": binding.get("endpointID", binding.get("endpoint")),
        "group": binding.get("groupID", binding.get("group")),
    }


def extract_endpoint_state(rec: dict[str, Any]) -> dict[str, Any]:
    eps = endpoint_map(rec)
    ep = eps.get(str(TARGET_ENDPOINT), eps.get(TARGET_ENDPOINT, {}))
    if not isinstance(ep, dict):
        ep = {}
    binds = sorted(
        [normalize_binding(b) for b in (ep.get("binds") or ep.get("bindings") or []) if isinstance(b, dict)],
        key=lambda x: json.dumps(x, sort_keys=True),
    )
    reports = ep.get("configuredReportings") or ep.get("configured_reportings") or []
    return {"endpoint": TARGET_ENDPOINT, "bindings": binds, "configured_reporting": reports}


def yaml_load_if_available(data: bytes) -> dict[str, Any] | None:
    try:
        import yaml  # type: ignore
    except Exception:
        return None
    obj = yaml.safe_load(data.decode("utf-8", errors="replace"))
    return obj if isinstance(obj, dict) else {}


def relevant_config(cfg: dict[str, Any] | None, ieee: str, friendly: str) -> dict[str, Any]:
    if cfg is None:
        return {"yaml_parser": "unavailable"}
    out: dict[str, Any] = {}
    mqtt = cfg.get("mqtt")
    if isinstance(mqtt, dict):
        out["mqtt"] = {k: v for k, v in mqtt.items() if k in {"base_topic", "server", "version"}}
    devices = cfg.get("devices")
    if isinstance(devices, dict):
        for key in (ieee, friendly):
            if key in devices:
                out.setdefault("devices", {})[key] = devices[key]
    groups = cfg.get("groups")
    if isinstance(groups, dict):
        # Preserve all group names/IDs because restoration needs ID->friendly mapping.
        out["groups"] = groups
    return sanitize(out)


def snapshot_z2m(args: argparse.Namespace) -> Path:
    stamp = utc_stamp()
    outdir = Path(args.out or f"glsd-tuya-session-{stamp}").resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    z2m_dir = args.z2m_dir.rstrip("/")
    db_raw = ssh_cat(args.ssh, f"{z2m_dir}/database.db")
    records = parse_database_db(db_raw)
    device = find_device_record(records, args.ieee)
    groups = target_groups_from_db(records, args.ieee)
    index = device_index(records)

    cfg_raw = b""
    cfg = None
    cfg_path = f"{z2m_dir}/configuration.yaml"
    if ssh_exists(args.ssh, cfg_path):
        cfg_raw = ssh_cat(args.ssh, cfg_path)
        cfg = yaml_load_if_available(cfg_raw)

    endpoint = extract_endpoint_state(device)
    # Keep only the mappings needed to resolve binding targets.
    target_map: dict[str, Any] = {}
    for b in endpoint["bindings"]:
        target_ieee = b.get("ieee")
        if target_ieee and target_ieee in index:
            r = index[target_ieee]
            target_map[target_ieee] = {
                "type": r.get("type"),
                "modelId": r.get("modelId", r.get("model_id")),
                "manufName": r.get("manufName", r.get("manufacturer_name")),
                "friendly_name": (cfg or {}).get("devices", {}).get(target_ieee, {}).get("friendly_name") if isinstance((cfg or {}).get("devices"), dict) else None,
            }
    # Coordinator is commonly not in configuration.yaml; normalize friendly name.
    for k, v in target_map.items():
        if str(v.get("type", "")).lower() == "coordinator" and not v.get("friendly_name"):
            v["friendly_name"] = "Coordinator"

    snapshot = {
        "schema": 1,
        "created_utc": stamp,
        "target": {"ieee": args.ieee.lower(), "friendly_name": args.friendly, "model": TARGET_MODEL, "endpoint": TARGET_ENDPOINT},
        "device_record": sanitize(device),
        "endpoint_state": sanitize(endpoint),
        "groups": sanitize(groups),
        "binding_target_map": sanitize(target_map),
        "configuration": relevant_config(cfg, args.ieee, args.friendly),
        "warnings": [
            "This is a pre-migration snapshot. Do not treat cached Z2M state as proof that device-side tables survive a factory reset.",
            "Raw configuration.yaml is deliberately not saved because it can contain MQTT/network secrets.",
        ],
    }
    dump_json(outdir / "z2m_snapshot.json", snapshot)
    (outdir / "README.txt").write_text(
        "GL-SD Tuya extraction session\n"
        f"Created: {stamp}\n"
        f"Target: {args.friendly} {args.ieee}\n"
        "Next: physically reset the dimmer and join it to the Tuya gateway, then run tuya-watch.\n",
        encoding="utf-8",
    )
    print(f"[OK] Z2M snapshot written to {outdir / 'z2m_snapshot.json'}")
    print(f"[OK] Bindings: {len(endpoint['bindings'])}; groups containing target: {len(groups)}")
    return outdir


def get_tuya_openapi(region: str):
    try:
        from tuya_connector import TuyaOpenAPI  # type: ignore
    except Exception as exc:
        raise ToolError("Missing dependency: pip install tuya-connector-python") from exc
    access_id = os.environ.get("TUYA_ACCESS_ID")
    access_key = os.environ.get("TUYA_ACCESS_KEY")
    if not access_id or not access_key:
        raise ToolError("Set TUYA_ACCESS_ID and TUYA_ACCESS_KEY environment variables. Never paste them into GitHub/chat.")
    endpoint = os.environ.get("TUYA_ENDPOINT") or TUYA_ENDPOINTS.get(region)
    if not endpoint:
        raise ToolError(f"Unknown region {region!r}; use eu/us/cn/in or set TUYA_ENDPOINT")
    api = TuyaOpenAPI(endpoint, access_id, access_key)
    ok = api.connect()
    if isinstance(ok, dict) and ok.get("success") is False:
        raise ToolError(f"Tuya authentication failed: {sanitize(ok)}")
    return api


def tuya_get(api: Any, path: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    resp = api.get(path, params or {})
    if not isinstance(resp, dict):
        raise ToolError(f"Unexpected Tuya response for {path}: {type(resp).__name__}")
    return resp


def iter_device_list(api: Any) -> Iterable[dict[str, Any]]:
    last_row_key = None
    for _ in range(20):
        params: dict[str, Any] = {"page_size": 100}
        if last_row_key:
            params["last_row_key"] = last_row_key
        resp = tuya_get(api, "/v1.3/iot-03/devices", params)
        result = resp.get("result") or {}
        if not resp.get("success", False):
            raise ToolError(f"Tuya device list failed: {sanitize(resp)}")
        items = result.get("list") if isinstance(result, dict) else None
        if not isinstance(items, list):
            items = []
        for item in items:
            if isinstance(item, dict):
                yield item
        if not result.get("has_more"):
            break
        last_row_key = result.get("last_row_key")
        if not last_row_key:
            break


def device_score(d: dict[str, Any]) -> int:
    hay = " ".join(str(d.get(k, "")) for k in ("name", "model", "product_name", "category_name")).lower()
    score = 0
    if "gl-sd-301p" in hay: score += 100
    if "gl-sd-001p" in hay: score += 80
    if "gl-sd-003p" in hay: score += 70
    if "gledopto" in hay: score += 50
    if d.get("sub") is True: score += 10
    if d.get("online") is True: score += 5
    return score


def list_tuya_candidates(api: Any) -> list[dict[str, Any]]:
    devices = list(iter_device_list(api))
    devices.sort(key=device_score, reverse=True)
    return devices


def choose_candidate(devices: list[dict[str, Any]], explicit: str | None) -> dict[str, Any] | None:
    if explicit and explicit != "auto":
        for d in devices:
            if d.get("id") == explicit:
                return d
        return {"id": explicit, "name": "<explicit device id>"}
    scored = [(device_score(d), d) for d in devices]
    good = [d for s, d in scored if s >= 50]
    if len(good) == 1:
        return good[0]
    if good:
        # Exact 301P wins if unique.
        exact = [d for d in good if "gl-sd-301p" in " ".join(str(d.get(k, "")) for k in ("name", "model", "product_name")).lower()]
        if len(exact) == 1:
            return exact[0]
    return None


def collect_urls(value: Any, path: str = "$", out: list[tuple[str, str]] | None = None) -> list[tuple[str, str]]:
    if out is None:
        out = []
    if isinstance(value, dict):
        for k, v in value.items():
            p = f"{path}.{k}"
            key_l = str(k).lower()
            if isinstance(v, str) and v.startswith(("http://", "https://")) and (key_l in URL_KEYS or "firmware" in p.lower() or "upgrade" in p.lower()):
                out.append((p, v))
            collect_urls(v, p, out)
    elif isinstance(value, list):
        for i, v in enumerate(value):
            collect_urls(v, f"{path}[{i}]", out)
    elif isinstance(value, str) and value.startswith(("http://", "https://")) and ("firmware" in path.lower() or "upgrade" in path.lower()):
        out.append((path, value))
    # de-duplicate preserving order
    seen = set()
    dedup = []
    for p, u in out:
        if u not in seen:
            seen.add(u); dedup.append((p, u))
    return dedup


def safe_filename_from_url(url: str, index: int) -> str:
    name = Path(urlparse(url).path).name
    name = re.sub(r"[^A-Za-z0-9._()-]+", "_", name)
    if not name or len(name) > 160:
        name = f"vendor_firmware_{index}.bin"
    return name


def download_url(url: str, dest: Path) -> dict[str, Any]:
    req = Request(url, headers={"User-Agent": "gledopto-tuya-extractor/1.0"})
    h_md5 = hashlib.md5()
    h_sha256 = hashlib.sha256()
    h_sha512 = hashlib.sha512()
    size = 0
    with urlopen(req, timeout=45) as r, dest.open("wb") as f:
        while True:
            chunk = r.read(1024 * 128)
            if not chunk:
                break
            f.write(chunk); size += len(chunk)
            h_md5.update(chunk); h_sha256.update(chunk); h_sha512.update(chunk)
    return {"path": str(dest), "size": size, "md5": h_md5.hexdigest(), "sha256": h_sha256.hexdigest(), "sha512": h_sha512.hexdigest()}


def parse_ota(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < 56:
        return {"is_zigbee_ota": False, "reason": "file shorter than 56-byte OTA header"}
    try:
        magic, header_version, header_length, field_control, manufacturer, image_type, file_version, stack_version = struct.unpack_from("<IHHHHHIH", data, 0)
    except struct.error as exc:
        return {"is_zigbee_ota": False, "reason": str(exc)}
    header_string = data[20:52].split(b"\x00", 1)[0].decode("ascii", errors="replace")
    total_image_size = struct.unpack_from("<I", data, 52)[0]
    out = {
        "is_zigbee_ota": magic == OTA_MAGIC,
        "magic": f"0x{magic:08X}",
        "header_version": f"0x{header_version:04X}",
        "header_length": header_length,
        "field_control": f"0x{field_control:04X}",
        "manufacturer_code": manufacturer,
        "manufacturer_code_hex": f"0x{manufacturer:04X}",
        "image_type": image_type,
        "image_type_hex": f"0x{image_type:04X}",
        "file_version": file_version,
        "file_version_hex": f"0x{file_version:08X}",
        "stack_version": stack_version,
        "header_string": header_string,
        "total_image_size": total_image_size,
        "actual_file_size": len(data),
        "size_matches_header": total_image_size == len(data),
    }
    # The GLEDOPTO/Telink images of interest have a 56-byte Zigbee OTA header.
    if magic == OTA_MAGIC and 56 <= header_length <= len(data):
        out["payload_size"] = len(data) - header_length
        if len(data) >= header_length + 0x1C:
            payload = data[header_length:]
            # Telink app convention: version @ +0x4, mfg/image @ +0x12/+0x14, app size @ +0x18.
            try:
                out["telink_payload"] = {
                    "file_version_hex": f"0x{struct.unpack_from('<I', payload, 0x4)[0]:08X}",
                    "boot_marker_0x08_hex": f"0x{struct.unpack_from('<I', payload, 0x8)[0]:08X}",
                    "manufacturer_code_hex": f"0x{struct.unpack_from('<H', payload, 0x12)[0]:04X}",
                    "image_type_hex": f"0x{struct.unpack_from('<H', payload, 0x14)[0]:04X}",
                    "app_size": struct.unpack_from('<I', payload, 0x18)[0],
                }
            except struct.error:
                pass
    return out


def start_tshark(interface: str | None, outdir: Path) -> subprocess.Popen[bytes] | None:
    if not interface:
        return None
    tshark = shutil.which("tshark")
    if not tshark:
        print("[WARN] --pcap-interface requested but tshark is not installed; continuing without PCAP")
        return None
    path = outdir / "tuya_gateway_capture.pcapng"
    print(f"[INFO] Starting tshark capture on {interface!r} -> {path}")
    return subprocess.Popen([tshark, "-i", interface, "-w", str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def stop_process(proc: subprocess.Popen[bytes] | None) -> None:
    if not proc:
        return
    with contextlib.suppress(Exception):
        proc.terminate(); proc.wait(timeout=5)
    if proc.poll() is None:
        with contextlib.suppress(Exception):
            proc.kill()


def query_firmware(api: Any, device_id: str) -> dict[str, Any]:
    responses = {
        "device_v1_1": tuya_get(api, f"/v1.1/iot-03/devices/{device_id}"),
        "firmware_v2": tuya_get(api, f"/v2.0/cloud/thing/{device_id}/firmware"),
        "upgrade_infos_legacy": tuya_get(api, f"/v1.0/iot-03/devices/{device_id}/upgrade-infos"),
    }
    return responses


def tuya_watch(args: argparse.Namespace) -> Path:
    api = get_tuya_openapi(args.region)
    outdir = Path(args.out or f"glsd-tuya-extract-{utc_stamp()}").resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    proc = start_tshark(args.pcap_interface, outdir)
    deadline = time.monotonic() + args.timeout
    last_summary = None
    try:
        while True:
            if args.device_id and args.device_id != "auto":
                devices = []
                cand = {"id": args.device_id, "name": "<explicit device id>"}
            else:
                devices = list_tuya_candidates(api)
                dump_json(outdir / "tuya_devices.sanitized.json", sanitize(devices))
                cand = choose_candidate(devices, args.device_id)
            summary = [(d.get("id"), d.get("name"), d.get("model"), d.get("online"), device_score(d)) for d in devices[:10]]
            if devices and summary != last_summary:
                print("[INFO] Top Tuya candidates:")
                for did, name, model, online, score in summary:
                    print(f"  score={score:3d} online={online!s:5s} id={did} name={name!r} model={model!r}")
                last_summary = summary
            if cand:
                device_id = str(cand.get("id"))
                print(f"[OK] Selected Tuya device {device_id}: {cand.get('name')!r} model={cand.get('model')!r}")
                responses = query_firmware(api, device_id)
                sanitized = sanitize(responses)
                dump_json(outdir / "tuya_firmware_metadata.sanitized.json", sanitized)
                urls = collect_urls(responses)
                dump_json(outdir / "firmware_urls.json", [{"json_path": p, "url": u} for p, u in urls])
                if not urls:
                    print("[WARN] Firmware metadata returned but no HTTP(S) URL was exposed.")
                    print(f"[INFO] Saved responses to {outdir / 'tuya_firmware_metadata.sanitized.json'}")
                    return outdir
                print(f"[OK] Found {len(urls)} unique URL(s); downloading immediately (signed URLs may expire).")
                downloads = []
                for i, (jpath, url) in enumerate(urls, 1):
                    name = safe_filename_from_url(url, i)
                    dest = outdir / name
                    try:
                        info = download_url(url, dest)
                        info["source_json_path"] = jpath
                        info["source_url"] = url
                        info["ota"] = parse_ota(dest)
                        downloads.append(info)
                        print(f"[OK] {dest.name}: {info['size']} bytes SHA256={info['sha256']}")
                        if info["ota"].get("is_zigbee_ota"):
                            o = info["ota"]
                            print(f"     Zigbee OTA mfg={o['manufacturer_code_hex']} image={o['image_type_hex']} version={o['file_version_hex']}")
                    except Exception as exc:
                        downloads.append({"source_json_path": jpath, "source_url": url, "error": repr(exc)})
                        print(f"[WARN] Download failed for URL from {jpath}: {exc}")
                dump_json(outdir / "downloads.json", downloads)
                return outdir
            if not args.watch:
                raise ToolError("No unambiguous GLEDOPTO/GL-SD Tuya device found. Pair it first or pass --device-id.")
            if time.monotonic() >= deadline:
                raise ToolError(f"Timed out after {args.timeout}s waiting for GL-SD device in Tuya Cloud")
            print(f"[WAIT] No unambiguous GL-SD device yet. Physically reset/pair it to Tuya; polling again in {args.interval}s...")
            time.sleep(args.interval)
    finally:
        stop_process(proc)


def snapshot_from_path(path: str) -> dict[str, Any]:
    p = Path(path)
    if p.is_dir():
        p = p / "z2m_snapshot.json"
    return json.loads(p.read_text(encoding="utf-8"))


def live_z2m_state(ssh_alias: str, z2m_dir: str, ieee: str) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    records = parse_database_db(ssh_cat(ssh_alias, f"{z2m_dir.rstrip('/')}/database.db"))
    return find_device_record(records, ieee), target_groups_from_db(records, ieee), records


def canonical(obj: Any) -> str:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def group_key(g: dict[str, Any]) -> Any:
    return g.get("groupID", g.get("id"))


def compare_state(snapshot: dict[str, Any], device: dict[str, Any], groups: list[dict[str, Any]]) -> dict[str, Any]:
    before_ep = snapshot["endpoint_state"]
    after_ep = extract_endpoint_state(device)
    before_binds = {canonical(x) for x in before_ep.get("bindings", [])}
    after_binds = {canonical(x) for x in after_ep.get("bindings", [])}
    before_groups = {group_key(g) for g in snapshot.get("groups", [])}
    after_groups = {group_key(g) for g in groups}
    before_reports = {canonical(x) for x in before_ep.get("configured_reporting", [])}
    after_reports = {canonical(x) for x in after_ep.get("configured_reporting", [])}
    return {
        "same_ieee": ieee_of(device) == snapshot["target"]["ieee"].lower(),
        "interview_completed": device.get("interviewCompleted", device.get("interview_completed", device.get("interview_state"))),
        "missing_bindings": [json.loads(x) for x in sorted(before_binds - after_binds)],
        "extra_bindings": [json.loads(x) for x in sorted(after_binds - before_binds)],
        "missing_group_ids": sorted(x for x in before_groups - after_groups if x is not None),
        "extra_group_ids": sorted(x for x in after_groups - before_groups if x is not None),
        "missing_reporting": [json.loads(x) for x in sorted(before_reports - after_reports)],
        "extra_reporting": [json.loads(x) for x in sorted(after_reports - before_reports)],
        "after_endpoint_state": sanitize(after_ep),
    }


def restore_check(args: argparse.Namespace) -> dict[str, Any]:
    snap = snapshot_from_path(args.snapshot)
    dev, groups, records = live_z2m_state(args.ssh, args.z2m_dir, snap["target"]["ieee"])
    diff = compare_state(snap, dev, groups)
    out = Path(args.out or Path(args.snapshot).parent) / "restore_diff.json"
    dump_json(out, diff)
    print(f"[OK] Restore comparison: {out}")
    print(json.dumps({k: v for k, v in diff.items() if k != "after_endpoint_state"}, indent=2))
    return diff


def resolve_target_name(snapshot: dict[str, Any], bind: dict[str, Any]) -> str | None:
    if bind.get("type") == "group":
        gid = bind.get("group")
        # Look for a configured friendly name matching group ID.
        cfg_groups = snapshot.get("configuration", {}).get("groups") or {}
        if isinstance(cfg_groups, dict):
            for name, val in cfg_groups.items():
                if isinstance(val, dict) and val.get("ID", val.get("id", val.get("groupID"))) == gid:
                    return str(name)
        return None
    ieee = bind.get("ieee")
    if not ieee:
        return None
    m = snapshot.get("binding_target_map", {}).get(ieee) or {}
    return m.get("friendly_name") or ("Coordinator" if str(m.get("type", "")).lower() == "coordinator" else ieee)


def reporting_to_payload(friendly: str, report: dict[str, Any]) -> dict[str, Any] | None:
    cluster = report.get("cluster")
    attr = report.get("attribute") or report.get("attrId")
    if not cluster or attr is None:
        return None
    return {
        "id": friendly,
        "endpoint": TARGET_ENDPOINT,
        "cluster": cluster,
        "attribute": attr,
        "minimum_report_interval": report.get("minRepIntval", report.get("minimumReportInterval", report.get("minimum_report_interval", 0))),
        "maximum_report_interval": report.get("maxRepIntval", report.get("maximumReportInterval", report.get("maximum_report_interval", 65000))),
        "reportable_change": report.get("repChange", report.get("reportableChange", report.get("reportable_change", 1))),
    }


def ps_quote_json(obj: Any) -> str:
    s = json.dumps(obj, separators=(",", ":"))
    return s.replace("'", "''")


def restore_plan(args: argparse.Namespace) -> Path:
    snap = snapshot_from_path(args.snapshot)
    dev, groups, records = live_z2m_state(args.ssh, args.z2m_dir, snap["target"]["ieee"])
    diff = compare_state(snap, dev, groups)
    friendly = snap["target"]["friendly_name"]
    commands: list[dict[str, Any]] = []

    # Groups: MQTT requires group friendly name, not numeric ID.
    cfg_groups = snap.get("configuration", {}).get("groups") or {}
    for gid in diff["missing_group_ids"]:
        group_name = None
        if isinstance(cfg_groups, dict):
            for name, val in cfg_groups.items():
                if isinstance(val, dict) and val.get("ID", val.get("id", val.get("groupID"))) == gid:
                    group_name = str(name); break
        if group_name:
            commands.append({"topic": "zigbee2mqtt/bridge/request/group/members/add", "payload": {"group": group_name, "device": f"{friendly}/{TARGET_ENDPOINT}"}, "reason": f"restore group {gid}"})
        else:
            commands.append({"manual": True, "reason": f"Group ID {gid} is missing but its friendly name could not be resolved; restore via Z2M frontend using snapshot."})

    # Coalesce bindings by target + endpoint, allowing Z2M to receive explicit cluster list.
    bind_buckets: dict[tuple[Any, Any, Any], list[str]] = {}
    unresolved: list[dict[str, Any]] = []
    for b in diff["missing_bindings"]:
        to = resolve_target_name(snap, b)
        if not to:
            unresolved.append(b); continue
        key = (to, b.get("endpoint"), b.get("type"))
        bind_buckets.setdefault(key, []).append(str(b.get("cluster")))
    for (to, target_ep, typ), clusters in bind_buckets.items():
        payload: dict[str, Any] = {"from": friendly, "from_endpoint": TARGET_ENDPOINT, "to": to, "clusters": sorted(set(clusters))}
        if typ == "endpoint" and target_ep is not None and to != "Coordinator":
            payload["to_endpoint"] = target_ep
        commands.append({"topic": "zigbee2mqtt/bridge/request/device/bind", "payload": payload, "reason": "restore binding(s)"})
    for b in unresolved:
        commands.append({"manual": True, "reason": f"Binding target unresolved: {b}"})

    for r in diff["missing_reporting"]:
        payload = reporting_to_payload(friendly, r)
        if payload:
            commands.append({"topic": "zigbee2mqtt/bridge/request/device/reporting/configure", "payload": payload, "reason": "restore reporting"})
        else:
            commands.append({"manual": True, "reason": f"Reporting entry could not be converted: {r}"})

    outdir = Path(args.out or Path(args.snapshot).parent).resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    dump_json(outdir / "restore_plan.json", {"diff": diff, "commands": commands})
    ps_lines = [
        "$ErrorActionPreference = 'Stop'",
        "# Generated only; review before running. Requires mosquitto_pub and broker arguments.",
        "$mqttHost = '<MQTT_HOST>'",
        "$mqttUser = '<MQTT_USER>'",
        "$mqttPassword = '<MQTT_PASSWORD>'",
        "",
    ]
    for c in commands:
        if c.get("manual"):
            ps_lines.append(f"# MANUAL: {c['reason']}")
            continue
        payload = ps_quote_json(c["payload"])
        ps_lines.append(f"# {c['reason']}")
        ps_lines.append(f"mosquitto_pub -h $mqttHost -u $mqttUser -P $mqttPassword -t '{c['topic']}' -m '{payload}'")
    (outdir / "restore_commands.REVIEW_BEFORE_RUN.ps1").write_text("\n".join(ps_lines) + "\n", encoding="utf-8")
    print(f"[OK] Restore plan: {outdir / 'restore_plan.json'}")
    print(f"[OK] Reviewable PowerShell: {outdir / 'restore_commands.REVIEW_BEFORE_RUN.ps1'}")
    return outdir


def guided(args: argparse.Namespace) -> Path:
    """Interactive end-to-end operator workflow; physical resets remain manual."""
    session = Path(args.out or f"glsd-tuya-guided-{utc_stamp()}").resolve()
    session.mkdir(parents=True, exist_ok=True)
    print("[PHASE 1] Capturing pre-migration Zigbee2MQTT state...")
    snap_args = argparse.Namespace(ssh=args.ssh, z2m_dir=args.z2m_dir, ieee=TARGET_IEEE, friendly=TARGET_FRIENDLY, out=str(session))
    snapshot_z2m(snap_args)
    print("\n[PHASE 2] Tuya extraction watch is about to start.")
    print("Physical action: factory-reset GL-SD-301P (5 quick RESET presses) and pair it to the Tuya gateway/Smart Life.")
    print("DO NOT approve/click any firmware update. The script will only issue read-only cloud GET requests.")
    input("Press ENTER to start watching Tuya Cloud... ")
    tuya_args = argparse.Namespace(region=args.region, device_id=args.device_id, watch=True, timeout=args.timeout, interval=args.interval, pcap_interface=args.pcap_interface, out=str(session / "tuya"))
    tuya_watch(tuya_args)
    print("\n[PHASE 3] Vendor metadata extraction finished.")
    print("Physical action: factory-reset GL-SD-301P again, enable Z2M permit-join, and rejoin it to the original HA/Z2M network.")
    print("Wait until Zigbee2MQTT reports interview SUCCESSFUL.")
    input("Press ENTER after the device has successfully rejoined Zigbee2MQTT... ")
    check_args = argparse.Namespace(snapshot=str(session / "z2m_snapshot.json"), ssh=args.ssh, z2m_dir=args.z2m_dir, out=str(session))
    diff = restore_check(check_args)
    if diff.get("missing_bindings") or diff.get("missing_group_ids") or diff.get("missing_reporting"):
        print("[INFO] Differences found; generating review-only restoration commands.")
        plan_args = argparse.Namespace(snapshot=str(session / "z2m_snapshot.json"), ssh=args.ssh, z2m_dir=args.z2m_dir, out=str(session))
        restore_plan(plan_args)
        print("[ACTION] Executor/Supervisor must review restore_plan.json before applying any MQTT mutations.")
    else:
        print("[OK] Device-side groups/bindings/reporting match the pre-migration snapshot.")
    print(f"[DONE] Session evidence is in {session}")
    return session


def doctor(args: argparse.Namespace) -> int:
    checks = {
        "python": sys.version.split()[0],
        "ssh": shutil.which("ssh"),
        "tshark": shutil.which("tshark"),
    }
    try:
        import tuya_connector  # type: ignore
        checks["tuya_connector"] = getattr(tuya_connector, "__version__", "installed")
    except Exception:
        checks["tuya_connector"] = None
    try:
        import yaml  # type: ignore
        checks["pyyaml"] = getattr(yaml, "__version__", "installed")
    except Exception:
        checks["pyyaml"] = None
    checks["TUYA_ACCESS_ID"] = "set" if os.environ.get("TUYA_ACCESS_ID") else "missing"
    checks["TUYA_ACCESS_KEY"] = "set" if os.environ.get("TUYA_ACCESS_KEY") else "missing"
    print(json.dumps(checks, indent=2))
    required_ok = bool(checks["ssh"] and checks["tuya_connector"] and checks["pyyaml"])
    return 0 if required_ok else 2


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter, description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)

    g = sub.add_parser("guided", help="interactive end-to-end migration/extraction/return workflow")
    g.add_argument("--ssh", default=DEFAULT_SSH)
    g.add_argument("--z2m-dir", default=DEFAULT_Z2M_DIR)
    g.add_argument("--region", choices=sorted(TUYA_ENDPOINTS), default="eu")
    g.add_argument("--device-id", default="auto", help="use explicit Tuya child ID if automatic project device listing is unavailable")
    g.add_argument("--timeout", type=int, default=900)
    g.add_argument("--interval", type=int, default=5)
    g.add_argument("--pcap-interface")
    g.add_argument("--out")
    g.set_defaults(func=guided)

    d = sub.add_parser("doctor", help="check laptop prerequisites")
    d.set_defaults(func=doctor)

    s = sub.add_parser("snapshot-z2m", help="snapshot target state from HA/Zigbee2MQTT over SSH")
    s.add_argument("--ssh", default=DEFAULT_SSH, help="OpenSSH host alias for HA (default: ha)")
    s.add_argument("--z2m-dir", default=DEFAULT_Z2M_DIR)
    s.add_argument("--ieee", default=TARGET_IEEE)
    s.add_argument("--friendly", default=TARGET_FRIENDLY)
    s.add_argument("--out")
    s.set_defaults(func=snapshot_z2m)

    t = sub.add_parser("tuya-watch", help="poll Tuya Cloud for the paired GL-SD and extract firmware metadata/URL")
    t.add_argument("--region", choices=sorted(TUYA_ENDPOINTS), default="eu")
    t.add_argument("--device-id", default="auto", help="Tuya child device ID, or auto")
    t.add_argument("--watch", action="store_true", help="keep polling while you physically reset/pair the dimmer")
    t.add_argument("--timeout", type=int, default=900)
    t.add_argument("--interval", type=int, default=5)
    t.add_argument("--pcap-interface", help="optional tshark capture interface name/number")
    t.add_argument("--out")
    t.set_defaults(func=tuya_watch)

    c = sub.add_parser("restore-check", help="compare post-rejoin Z2M state with the pre-migration snapshot")
    c.add_argument("snapshot")
    c.add_argument("--ssh", default=DEFAULT_SSH)
    c.add_argument("--z2m-dir", default=DEFAULT_Z2M_DIR)
    c.add_argument("--out")
    c.set_defaults(func=restore_check)

    r = sub.add_parser("restore-plan", help="generate reviewable MQTT restore commands for missing state")
    r.add_argument("snapshot")
    r.add_argument("--ssh", default=DEFAULT_SSH)
    r.add_argument("--z2m-dir", default=DEFAULT_Z2M_DIR)
    r.add_argument("--out")
    r.set_defaults(func=restore_plan)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        result = args.func(args)
        if isinstance(result, int):
            return result
        return 0
    except KeyboardInterrupt:
        print("\n[STOP] Interrupted; no Tuya firmware-update POST exists in this tool.", file=sys.stderr)
        return 130
    except ToolError as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
