#!/usr/bin/env python3
"""Targeted ED bench diagnostics. Runtime state and credentials stay outside git."""
from __future__ import annotations

import argparse
import atexit
import datetime as dt
import json
import os
from pathlib import Path
import queue
import re
import subprocess
import sys
import threading
import time
import tempfile
import uuid
from urllib.parse import urlparse
from glsd301p_health_decode import decode_health_snapshot

TARGET = "0xa4c13850cfcdb3a4"
BUILD = "GLSD-ED-009"
VERSION = 0x7F0A0001
ROOT = Path(__file__).resolve().parents[1]
OTA_NAMES = {0: "upgradeServerId", 2: "currentFileVersion", 6: "imageUpgradeStatus"}
STARTUP_CASES={'on':(1,False),'off':(0,True),'toggle':(2,True),
               'previous-on':(255,True),'previous-off':(255,False)}


def diagnostic_cache_safe(info):
    """Match Z2M's full-property regex semantics; diagnostics must stay visible
    for correlation but must not become persistent production state."""
    opts = (info or {}).get('config', {}).get('devices', {}).get(TARGET, {})
    probe = 'bench_read_0123456789abcdef0123456789abcdef'
    try:
        cached = any(re.search('^' + p + '$', probe) for p in opts.get('filtered_cache', []))
        hidden = any(re.search('^' + p + '$', probe) for p in opts.get('filtered_attributes', []))
        return cached and not hidden
    except (re.error, TypeError):
        return False


def requested_keys(cluster, attributes):
    return {OTA_NAMES.get(a, str(a)) if cluster == "genOta" else str(a)
            for a in attributes}


def outside_repo(path: str) -> Path:
    resolved = Path(path).expanduser().resolve()
    if resolved == ROOT or ROOT in resolved.parents:
        raise ValueError("profile and workdir must be outside the repository")
    return resolved


def validate_profile(p: dict) -> None:
    build=p.get('build','')
    if p.get("ieee", "").lower() != TARGET or not re.fullmatch(r'GLSD-ED-\d{3}',build) or int(build[-3:])<8:
        raise ValueError("profile must pin the target and supported ED build")
    if p.get("endpoint") != 11 or p.get("role") != "EndDevice":
        raise ValueError("profile must specify endpoint 11 and EndDevice")
    version=p.get('file_version')
    known={'GLSD-ED-008':0x7F090001,'GLSD-ED-009':0x7F0A0001}
    if type(version) is not int or not 0<version<=0xffffffff or (build in known and version!=known[build]):
        raise ValueError("profile must pin the matching file_version")
    if not isinstance(p.get("mqtt_config"), str):
        raise ValueError("mqtt_config must name a private Zigbee2MQTT YAML file")
    outside_repo(p["mqtt_config"])


def utc() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


class Journal:
    def __init__(self, directory: Path, profile=None):
        self.directory = directory
        directory.mkdir(parents=True, exist_ok=True)
        self.path = directory / "events.jsonl"
        self.expected_build=(profile or {}).get('build',BUILD)
        self.expected_version=(profile or {}).get('file_version',VERSION)
        if profile:
            binding={k:profile[k] for k in ('ieee','build','file_version','endpoint','role')}
            receipt=directory/'profile-binding.json'
            if receipt.exists():
                if json.loads(receipt.read_text())!=binding:raise ValueError('workdir belongs to another target/build profile')
            elif self.path.exists():
                raise ValueError('legacy journal lacks profile binding; preserve it and use a fresh workdir')
            else:
                with receipt.open('x') as f:json.dump(binding,f,sort_keys=True)

    def put(self, phase: str, case: str, status: str, **details):
        event = {"time": utc(), "phase": phase, "case": case,
                 "status": status, **details}
        with self.path.open("a+b") as f:
            f.seek(0, os.SEEK_END)
            if f.tell():
                f.seek(-1, os.SEEK_END)
                if f.read(1) != b"\n":
                    f.write(b"\n")
            f.write((json.dumps(event, sort_keys=True) + "\n").encode("utf-8"))
        print(f"[{event['time']}] {phase} / {case}: {status} "
              f"{details.get('reason', '')}", flush=True)
        return event

    def report(self):
        events = []
        if self.path.exists():
            for line in self.path.read_text(encoding="utf-8").splitlines():
                try:
                    events.append(json.loads(line))
                except json.JSONDecodeError:
                    events.append({"status": "UNVERIFIED", "reason": "interrupted journal record"})
        latest = {}
        for event in events:
            if "phase" in event and "case" in event:
                key = (event["phase"], event["case"], event.get("stage"))
                latest[key] = event
        required = [("baseline", "build", None), ("baseline", "version", None), ("baseline", "health", None)]
        required += [("attributes", name, None) for name in
                     ("server-name", "server-id", "version-name", "version-id", "status-name", "status-id", "combined-names", "combined-ids")]
        required += [("ota", str(n), None) for n in range(1, 6)]
        required += [("power-cycle", f"cycle-{n}", "after") for n in range(1, 4)]
        required += [("soak", f"checkpoint-{n}", None) for n in range(1, 5)]
        if int(self.expected_build[-3:])>=9:
            required += [('post-ota','converter',None),('post-ota','supply',None)]
            required += [('startup',name,'after') for name in STARTUP_CASES]
        cases = []
        for phase, case, stage in required:
            event = latest.get((phase, case, stage))
            status = event.get("status", "UNVERIFIED") if event else "UNVERIFIED"
            if event and phase == "baseline" and case == "build" and event.get("response", {}).get("swBuildId") != self.expected_build:
                status = "FAIL"
            if event and phase == 'baseline' and case == 'version' and event.get('response',{}).get('currentFileVersion')!=self.expected_version:
                status='FAIL'
            if event and phase == "baseline" and case == "health" and not event.get("health"):
                status = "UNVERIFIED"
            if event and phase == "attributes" and status == "PASS":
                response = event.get("response", {})
                if "currentFileVersion" in response and response["currentFileVersion"] != self.expected_version:
                    status = "FAIL"
                if "imageUpgradeStatus" in response and response["imageUpgradeStatus"] != 0:
                    status = "FAIL"
            cases.append({"phase": phase, "case": case, "stage": stage, "status": status})
        for name, number, attribute in (("server-name", "server-id", "upgradeServerId"),
                                        ("version-name", "version-id", "currentFileVersion"),
                                        ("status-name", "status-id", "imageUpgradeStatus")):
            left, right = latest.get(("attributes", name, None)), latest.get(("attributes", number, None))
            if left and right and left.get("status") == right.get("status") == "PASS" and left["response"].get(attribute) != right["response"].get(attribute):
                cases.append({"phase": "attributes", "case": f"{name}-equivalence", "status": "FAIL"})
        if any(e.get("phase") == "ota" or str(e.get("case", "")).endswith("-ota") or
               (e.get("phase") == "soak" and str(e.get("case", "")).startswith("ota-"))
               for e in events if e.get("status") == "FAIL"):
            cases.append({"phase": "diagnosis", "case": "intermittent-ota-failure", "status": "UNVERIFIED",
                          "reason": "at least one OTA check failed; retry success does not establish its cause"})
        if any(e.get("reason") == "interrupted journal record" for e in events):
            cases.append({"phase": "journal", "case": "interrupted-record", "status": "UNVERIFIED",
                          "reason": "an incomplete journal record requires review"})
        statuses = {case["status"] for case in cases}
        overall = "FAIL" if "FAIL" in statuses else "UNVERIFIED" if "UNVERIFIED" in statuses else "PASS"
        result = {"target": TARGET, "build":self.expected_build,"file_version":self.expected_version,"generated_at": utc(), "events": events,
                  "cases": cases, "overall": overall,
                  "limitations": ["No OTA transfer, mains load or physical lamp test"]}
        output = self.directory / "report.json"
        tmp = output.with_suffix(".tmp")
        tmp.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        os.replace(tmp, output)
        print(f"Report: {output}", flush=True)
        return result


class TargetLock:
    """OS-released lock prevents this host starting overlapping bench runs."""
    def __init__(self):
        path = Path(tempfile.gettempdir()) / "glsd301p-ed008-bench.lock"
        self.file = path.open("a+b")
        self.file.seek(0, os.SEEK_END)
        if self.file.tell() == 0:
            self.file.write(b"0")
            self.file.flush()
        self.file.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            self.file.close()
            raise RuntimeError("another ED-008 bench process is active on this host") from exc
        atexit.register(self.close)

    def close(self):
        if self.file.closed:
            return
        if os.name == "nt":
            import msvcrt
            self.file.seek(0)
            msvcrt.locking(self.file.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(self.file.fileno(), fcntl.LOCK_UN)
        self.file.close()


class Connection:
    def __init__(self, p: dict):
        import paho.mqtt.client as mqtt
        import yaml
        cfg = yaml.safe_load(outside_repo(p["mqtt_config"]).read_text(encoding="utf-8"))
        cfg = cfg["mqtt"] if "mqtt" in cfg else cfg
        server = urlparse(cfg["server"])
        if server.scheme not in ("mqtt", "mqtts") or not server.hostname:
            raise ValueError("MQTT server must use mqtt:// or mqtts://")
        self.base = cfg.get("base_topic", "zigbee2mqtt").strip("/")
        self.events = queue.Queue()
        self.ready = threading.Event()
        self.subscribed = threading.Event()
        self.info = None
        self.devices = None
        self.target_topic = None
        self.expected_build=p['build'];self.expected_version=p['file_version']
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
        if cfg.get("user"):
            self.client.username_pw_set(cfg["user"], cfg.get("password"))
        if server.scheme == "mqtts":
            self.client.tls_set()
        def on_connect(client, userdata, flags, reason, properties):
            if reason.is_failure:
                self.events.put(("error", str(reason), False))
                self.ready.set()
                return
            client.subscribe([(self.base + "/bridge/logging", 1),
                              (self.base + "/bridge/response/device/ota_update/check", 1),
                              (self.base + "/bridge/info", 1),
                              (self.base + "/bridge/devices", 1)])
            self.ready.set()
        def on_subscribe(client, userdata, mid, reasons, properties):
            if any(reason.is_failure for reason in reasons):
                self.events.put(("error", "MQTT subscription rejected", False))
            else:
                self.subscribed.set()
        def on_message(client, userdata, message):
            try:
                value = json.loads(message.payload.decode("utf-8"))
            except (UnicodeError, json.JSONDecodeError):
                return
            if message.topic == self.base + "/bridge/info" and isinstance(value, dict):
                self.info = value
            if message.topic == self.base + '/bridge/devices' and isinstance(value,list):
                self.devices=value
                rows=[d for d in value if d.get('ieee_address')==TARGET]
                self.target_topic=None
                if len(rows)==1 and rows[0].get('type')=='EndDevice' and '11' in rows[0].get('endpoints',{}):
                    self.target_topic=self.base+'/'+rows[0]['friendly_name']
                    client.subscribe(self.target_topic,1)
            self.events.put((message.topic, value, message.retain))
        self.client.on_connect = on_connect
        self.client.on_subscribe = on_subscribe
        self.client.on_message = on_message
        self.client.connect(server.hostname, server.port or (8883 if server.scheme == "mqtts" else 1883), 10)
        self.client.loop_start()
        if not self.ready.wait(10) or not self.subscribed.wait(10):
            self.client.loop_stop()
            self.client.disconnect()
            raise RuntimeError("MQTT connection or subscription did not become ready")

    def close(self):
        self.client.disconnect()
        self.client.loop_stop()

    def publish(self, suffix, value):
        result = self.client.publish(self.base + suffix, json.dumps(value), qos=1, retain=False)
        result.wait_for_publish(timeout=10)
        if not result.is_published():
            raise RuntimeError("MQTT publish not acknowledged")

    def drain(self):
        while True:
            try:
                self.events.get_nowait()
            except queue.Empty:
                return

    def read(self, cluster, attributes, direction=0, timeout=16):
        deadline = time.monotonic() + min(timeout, 2)
        while (self.info is None or self.target_topic is None) and time.monotonic() < deadline:
            time.sleep(.02)
        if self.target_topic is None:
            return {'status':'FAIL','reason':'bridge devices does not identify the pinned IEEE/EndDevice/endpoint 11'}
        if not diagnostic_cache_safe(self.info):
            return {'status': 'FAIL', 'reason': 'Configure target filtered_cache: ["bench_read_.*"] before bench reads; do not filter these attributes from live responses'}
        self.drain()
        property_name = "bench_read_" + uuid.uuid4().hex
        request = {"read": {"cluster": cluster, "attributes": attributes,
                            "options": {"direction": direction},
                            "state_property": property_name}}
        start = time.monotonic()
        self.publish("/" + TARGET + "/set", request)
        deadline = start + timeout
        fallback = None
        while time.monotonic() < deadline:
            try:
                topic, value, retained = self.events.get(timeout=max(0.01, deadline-time.monotonic()))
            except queue.Empty:
                break
            if retained or not isinstance(value, dict):
                continue
            elapsed = round(time.monotonic() - start, 3)
            if topic == self.target_topic and property_name in value:
                payload = value[property_name]
                if isinstance(payload, dict) and requested_keys(cluster, attributes).issubset(payload):
                    return {"status": "PASS", "request": request, "response": payload,
                            "elapsed_seconds": elapsed}
                return {"status": "FAIL", "request": request, "response": payload,
                        "elapsed_seconds": elapsed, "reason": "response is missing requested attributes"}
            if not topic.endswith("/bridge/logging"):
                continue
            msg = str(value.get("message", ""))
            if TARGET in msg and cluster in msg and "failed" in msg.lower():
                match = re.search(re.escape(cluster) + r"\.read\((\[.*?\]),", msg)
                try:
                    same_request = bool(match) and json.loads(match.group(1)) == attributes
                except json.JSONDecodeError:
                    same_request = False
                if same_request:
                    return {"status": "FAIL", "request": request, "response": msg,
                            "elapsed_seconds": elapsed, "reason": "target read failed"}
            if f"Read result of '{cluster}':" in msg:
                try:
                    payload = json.loads(msg.split(f"Read result of '{cluster}':", 1)[1].strip())
                except (ValueError, IndexError):
                    continue
                if not isinstance(payload, dict) or not requested_keys(cluster, attributes).issubset(payload):
                    continue
                # This line has no IEEE. Keep it only as supporting evidence while
                # waiting for the unique property on the exact target's topic.
                fallback = msg
        return {"status": "UNVERIFIED", "request": request,
                "uncorrelated_log": fallback,
                "elapsed_seconds": round(time.monotonic()-start, 3),
                "reason": "no correlatable response before timeout"}

    def ota(self, timeout=90):
        self.drain()
        token = "glsd-bench-" + uuid.uuid4().hex
        start = time.monotonic()
        self.publish("/bridge/request/device/ota_update/check", {"id": TARGET, "transaction": token})
        deadline = start + timeout
        while time.monotonic() < deadline:
            try:
                topic, value, retained = self.events.get(timeout=max(0.01, min(10, deadline-time.monotonic())))
            except queue.Empty:
                if time.monotonic() < deadline:
                    print(f"[{utc()}] OTA check still waiting ({time.monotonic()-start:.0f}/{timeout}s)", flush=True)
                    continue
                break
            if retained or not isinstance(value, dict) or value.get("transaction") != token:
                continue
            if not topic.endswith("/bridge/response/device/ota_update/check"):
                continue
            data = value.get("data") or {}
            valid = value.get("status") == "ok" and data.get("id", "").lower() == TARGET
            reason = "" if valid else str(value.get("error") or "OTA response rejected or target mismatch")
            return {"status": "PASS" if valid else "FAIL", "transaction": token,
                    "response": value, "elapsed_seconds": round(time.monotonic()-start, 3),
                    "reason": reason}
        return {"status": "FAIL", "transaction": token,
                "elapsed_seconds": round(time.monotonic()-start, 3),
                "reason": "no transaction-matched OTA response"}


def record(j, phase, case, result):
    result = dict(result)
    status = result.pop("status")
    return j.put(phase, case, status, **result)


def read_case(conn, journal, phase, case, cluster, attrs, direction=0):
    print(f"[{utc()}] {phase} / {case}: reading {cluster} {attrs}", flush=True)
    result = conn.read(cluster, attrs, direction)
    if result.get('status')=='PASS':
        response=result.get('response',{})
        if ('swBuildId' in response and response['swBuildId']!=conn.expected_build) or ('currentFileVersion' in response and response['currentFileVersion']!=conn.expected_version):
            result['status']='FAIL';result['reason']='fresh identity does not match profile build/version'
    record(journal, phase, case, result)
    return result


def health_case(conn, journal, phase, case):
    print(f"[{utc()}] {phase} / {case}: reading health", flush=True)
    result = conn.read("genBasic", [0xff10])
    if result["status"] == "PASS":
        try:
            raw = result["response"]["65296"]
            if isinstance(raw, dict) and "data" in raw:
                data = raw["data"]
            elif isinstance(raw, dict):
                data = [raw[str(i)] for i in range(48)]
                if len(raw) != 48:
                    raise ValueError("health byte object has unexpected keys")
            else:
                data = raw
            health = decode_health_snapshot(bytes(data))
            result["health"] = health
            if health["network"] != "joined":
                result["status"] = "FAIL"
                result["reason"] = "health snapshot is not joined"
            elif not health["flags"]["runtime_ready"] or health["flags"]["fault_latched"] or health["exc_code"]:
                result["status"] = "FAIL"
                result["reason"] = "runtime not ready or firmware fault is latched"
        except (KeyError, TypeError, ValueError) as exc:
            result["status"] = "FAIL"
            result["reason"] = f"cannot decode health: {exc}"
    record(journal, phase, case, result)
    return result


SUPPLY_ATTR = 0xff11
SUPPLY_SAVE_THRESHOLD_MV = 2200


def decode_supply_snapshot(data: bytes) -> dict:
    """Basic:0xFF11 v1: version, samples, first/last/min mV, refused saves."""
    if len(data) != 10 or data[0] != 1:
        raise ValueError("supply snapshot must be version 1 and 10 bytes")
    u16 = lambda offset: data[offset] | (data[offset + 1] << 8)
    return {"version": 1, "samples": data[1], "first_mv": u16(2), "last_mv": u16(4),
            "min_mv": u16(6), "refused_saves": u16(8)}


def supply_case(conn, journal, phase, case):
    """Read-only PB3 supply snapshot; boot never depends on it, saves do."""
    print(f"[{utc()}] {phase} / {case}: reading supply", flush=True)
    result = conn.read("genBasic", [SUPPLY_ATTR])
    if result.get("status") == "PASS":
        try:
            raw = result["response"][str(SUPPLY_ATTR)]
            if isinstance(raw, dict) and "data" in raw:
                data = raw["data"]
            elif isinstance(raw, dict):
                if len(raw) != 10:
                    raise ValueError("supply byte object has unexpected keys")
                data = [raw[str(i)] for i in range(10)]
            else:
                data = raw
            supply = decode_supply_snapshot(bytes(data))
            result["supply"] = supply
            if supply["samples"] == 0:
                result["status"] = "FAIL"
                result["reason"] = "no PB3 supply sample recorded since boot"
            elif (supply["first_mv"] < SUPPLY_SAVE_THRESHOLD_MV or
                  supply["min_mv"] < SUPPLY_SAVE_THRESHOLD_MV or supply["refused_saves"]):
                result["status"] = "FAIL"
                result["reason"] = "PB3 supply reading below 2200 mV: startup saves are refused"
        except (KeyError, TypeError, ValueError) as exc:
            result["status"] = "FAIL"
            result["reason"] = f"cannot decode supply: {exc}"
    record(journal, phase, case, result)
    return result


def converter_ready(devices, build):
    rows=[d for d in (devices or []) if d.get('ieee_address')==TARGET]
    if len(rows)!=1:return False,'pinned IEEE missing or duplicated in bridge devices'
    d=rows[0]
    if d.get('software_build_id')!=build:return False,'Zigbee2MQTT build metadata is stale; refresh target Basic identity/interview through the approved post-OTA procedure'
    if d.get('type')!='EndDevice' or '11' not in d.get('endpoints',{}):return False,'device role/endpoint mismatch'
    definition=d.get('definition') or {}
    items=list(definition.get('exposes') or [])
    while items:
        e=items.pop();items.extend(e.get('features') or [])
        if e.get('name')=='power_on_behavior' and {'off','on','toggle','previous'}.issubset(e.get('values',[])):
            return True,''
    return False,'power_on_behavior is absent; refresh metadata and activate the reviewed family converter before acceptance'


def post_ota(conn,journal):
    """Reads only: never transfers firmware, changes converter, or interviews."""
    build=read_case(conn,journal,'post-ota','fresh-build','genBasic',['swBuildId'])
    version=read_case(conn,journal,'post-ota','fresh-version','genOta',['currentFileVersion'],1)
    ok,reason=converter_ready(conn.devices,conn.expected_build)
    ok=ok and build['status']==version['status']=='PASS'
    journal.put('post-ota','converter','PASS' if ok else 'FAIL',
                reason=reason or ('' if ok else 'fresh build/version read failed'),
                expected_build=conn.expected_build,expected_version=conn.expected_version)
    supply=supply_case(conn,journal,'post-ota','supply')
    return ok and supply.get('status')=='PASS'


def startup_snapshot(conn,journal,name):
    build=read_case(conn,journal,'startup',name+'-build','genBasic',['swBuildId'])
    version=read_case(conn,journal,'startup',name+'-version','genOta',['currentFileVersion'],1)
    on=read_case(conn,journal,'startup',name+'-onoff','genOnOff',['onOff','startUpOnOff'])
    level=read_case(conn,journal,'startup',name+'-level','genLevelCtrl',['currentLevel'])
    health=health_case(conn,journal,'startup',name+'-health')
    if any(r['status']!='PASS' for r in (build,version,on,level,health)):
        raise ValueError('fresh startup identity/state/health read failed')
    response=on['response'];brightness=level['response'].get('currentLevel')
    if response.get('onOff') not in (0,1) or response.get('startUpOnOff') not in (0,1,2,255) or type(brightness) is not int or not 0<=brightness<=254:
        raise ValueError('invalid startup attribute values')
    return {'on':bool(response['onOff']),'policy':response['startUpOnOff'],
            'level':brightness,'health':health['health']}


def startup_check(conn,journal,name,stage):
    """Observe operator-configured startup behavior; never writes or cuts power."""
    try:
        if int(conn.expected_build[-3:])<9:raise ValueError('startup acceptance requires ED-009 or newer')
        if not isinstance(conn.info,dict) or conn.info.get('permit_join') is not False:
            raise ValueError('joining must already be closed')
        if stage=='before':
            expected_policy,initial_on=STARTUP_CASES[name]
            first=startup_snapshot(conn,journal,name+'-prepare')
            if first['level']<2:raise ValueError('set a valid nonzero brightness before preparing the startup case')
            if first['policy']!=expected_policy or first['on']!=initial_on:
                raise ValueError('operator must select the requested power_on_behavior and initial on/off state first')
            print(f'[{utc()}] Startup {name}: waiting 3 seconds for stable persistence before manual power cut',flush=True)
            time.sleep(3)
            snapshot=startup_snapshot(conn,journal,name+'-settled')
            if any(first[k]!=snapshot[k] for k in ('policy','on','level')):
                raise ValueError('state changed during settling; do not cut power')
            journal.put('startup',name,'PASS',stage=stage,snapshot=snapshot,
                        reason='stable snapshot recorded; operator may now perform the isolated manual power cycle')
            return True
        events=[json.loads(l) for l in journal.path.read_text().splitlines() if l.strip()] if journal.path.exists() else []
        before=next((e for e in reversed(events) if e.get('phase')=='startup' and e.get('case')==name and e.get('stage')=='before'),None)
        if not before or before.get('status')!='PASS':raise ValueError('no passing settled before snapshot')
        old=before['snapshot'];current=startup_snapshot(conn,journal,name+'-restored')
        desired=old['policy']==1 or (old['policy']==2 and not old['on']) or (old['policy']==255 and old['on'])
        if current['policy']!=old['policy'] or current['on']!=desired or current['level']!=old['level']:
            raise ValueError('startup policy, restored on/off or brightness mismatch')
        if current['health']['uptime_ms']>=old['health']['uptime_ms']:
            raise ValueError('fresh uptime does not prove a restart')
        journal.put('startup',name,'PASS',stage=stage,snapshot=current,
                    reason='fresh build/version, restarted uptime, retained policy/state/brightness and joined health')
        return True
    except (ValueError,KeyError,json.JSONDecodeError) as e:
        journal.put('startup',name,'FAIL',stage=stage,reason=str(e))
        return False


def check_health_delta(previous, current):
    if current["uptime_ms"] <= previous["uptime_ms"]:
        return "uptime did not advance"
    for key in ("uart_deadline_faults", "timer_reg_faults", "parent_losses", "rejoin_failures"):
        if current[key] > previous[key]:
            return f"{key} increased"
    if current["flags"]["fault_latched"] or current["exc_code"]:
        return "firmware fault flag or exception is set"
    return ""


def ota_case(conn, journal, phase, case):
    for attempt in (1, 2):
        print(f"[{utc()}] {phase} / {case}: checking OTA availability, attempt {attempt}/2 (up to 90s)", flush=True)
        result = conn.ota()
        result["attempt"] = attempt
        record(journal, phase, case, result)
        if result["status"] == "PASS":
            return True
    return False


def latest_status(journal, phase, case, stage=None):
    if not journal.path.exists():
        return None
    found = None
    for line in journal.path.read_text(encoding="utf-8").splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if event.get("phase") == phase and event.get("case") == case and event.get("stage") == stage:
            found = event.get("status")
    return found


def guided(args, journal):
    """Run resumable phases; only the operator can perform a power cycle."""
    common = [sys.executable, str(Path(__file__).resolve()), "--profile", args.profile,
              "--workdir", args.workdir]

    def run(phase, *extra):
        command = common + ["--phase", phase, *extra]
        return subprocess.run(command, check=False).returncode == 0

    def require(phase, names, stage=None):
        return all(latest_status(journal, phase, name, stage) == "PASS" for name in names)

    if not require("baseline", ["build", "version", "health"]):
        if not run("baseline") or not require("baseline", ["build", "version", "health"]):
            return 1
    attributes = ["server-name", "server-id", "version-name", "version-id", "status-name", "status-id", "combined-names", "combined-ids"]
    if not require("attributes", attributes):
        if not run("attributes") or not require("attributes", attributes):
            return 1
    gate = journal.report()
    if any(case["status"] != "PASS" for case in gate["cases"] if case["phase"] in ("baseline", "attributes")):
        print("Baseline or OTA attributes do not match the pinned image; stopping.", flush=True)
        return 1
    if not require("ota", [str(n) for n in range(1, 6)]):
        if not run("ota") or not require("ota", [str(n) for n in range(1, 6)]):
            return 1
    for cycle in range(1, 4):
        case = f"cycle-{cycle}"
        if require("power-cycle", [case], "after"):
            print(f"{case} already passed; skipping", flush=True)
            continue
        if not require("power-cycle", [case], "before"):
            if not run("power-cycle", "--cycle", str(cycle), "--stage", "before") or not require("power-cycle", [case], "before"):
                return 1
        print("Disconnect SWS at the adapter. Turn off isolated target power, verify the rail is discharged, "
              "wait ten seconds, then restore 3.3 V.", flush=True)
        try:
            answer = input(f"Type DONE after completing power cycle {cycle}; press Enter to pause: ").strip()
        except EOFError:
            answer = ""
        if answer != "DONE":
            print("Paused before post-cycle checks; completed evidence is preserved.", flush=True)
            return 2
        if not run("power-cycle", "--cycle", str(cycle), "--stage", "after") or not require("power-cycle", [case], "after"):
            return 1
    if not args.skip_soak and not require("soak", [f"checkpoint-{n}" for n in range(1, 5)]):
        if not run("soak") or not require("soak", [f"checkpoint-{n}" for n in range(1, 5)]):
            return 1
    journal.report()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--workdir", required=True)
    parser.add_argument("--phase", required=True,
                        choices=("guided", "baseline", "attributes", "ota", "power-cycle", "soak", "post-ota", "startup", "report"))
    parser.add_argument("--cycle", type=int, choices=(1, 2, 3))
    parser.add_argument("--stage", choices=("before", "after"))
    parser.add_argument("--skip-soak", action="store_true", help="guided mode: leave the soak unverified")
    parser.add_argument('--startup-case',choices=tuple(STARTUP_CASES))
    args = parser.parse_args()
    profile = json.loads(outside_repo(args.profile).read_text(encoding="utf-8-sig"))
    validate_profile(profile)
    j = Journal(outside_repo(args.workdir),profile)
    if args.phase == "guided":
        return guided(args, j)
    if args.phase == "report":
        j.report()
        return 0
    lock = TargetLock()
    conn = Connection(profile)
    try:
        if args.phase == "baseline":
            read_case(conn, j, args.phase, "build", "genBasic", ["swBuildId"])
            read_case(conn, j, args.phase, "version", "genOta", ['currentFileVersion'],1)
            health_case(conn, j, args.phase, "health")
        elif args.phase == 'post-ota':
            post_ota(conn,j)
        elif args.phase == 'startup':
            if not args.startup_case or not args.stage:parser.error('startup requires --startup-case and --stage before|after')
            startup_check(conn,j,args.startup_case,args.stage)
        elif args.phase == "attributes":
            matrix = [("server-old-name", ["upgradeServerID"]),
                      ("server-name", ["upgradeServerId"]), ("server-id", [0]),
                      ("version-name", ["currentFileVersion"]), ("version-id", [2]),
                      ("status-name", ["imageUpgradeStatus"]), ("status-id", [6]),
                      ("combined-names", ["upgradeServerId", "currentFileVersion", "imageUpgradeStatus"]),
                      ("combined-ids", [0, 2, 6])]
            for name, attrs in matrix:
                read_case(conn, j, args.phase, name, "genOta", attrs, 1)
        elif args.phase == "ota":
            for n in range(1, 6):
                if not ota_case(conn, j, args.phase, str(n)):
                    break
                if n != 5:
                    time.sleep(15)
        elif args.phase == "power-cycle":
            if args.cycle is None or args.stage is None:
                parser.error("power-cycle requires --cycle 1..3 and --stage before|after")
            case = f"cycle-{args.cycle}"
            time.sleep(2)  # retained bridge info arrives after subscription
            if not isinstance(conn.info, dict) or conn.info.get("permit_join") is not False:
                j.put(args.phase, case, "UNVERIFIED", stage=args.stage,
                      reason="bridge info does not confirm joining is closed")
            else:
                prior = []
                if j.path.exists():
                    for line in j.path.read_text(encoding="utf-8").splitlines():
                        try:
                            prior.append(json.loads(line))
                        except json.JSONDecodeError:
                            pass
                if args.stage == "before":
                    if any(e.get("case") == case and e.get("stage") == "after" and e.get("status") == "PASS" for e in prior):
                        j.put(args.phase, case, "UNVERIFIED", stage="before", reason="cycle already completed; use a fresh workdir for a new run")
                    elif args.cycle > 1 and not any(e.get("case") == f"cycle-{args.cycle-1}" and e.get("stage") == "after" and e.get("status") == "PASS" for e in prior):
                        j.put(args.phase, case, "UNVERIFIED", stage="before", reason="previous cycle has not passed")
                    else:
                        build = read_case(conn, j, args.phase, case + "-before-build", "genBasic", ["swBuildId"])
                        health = health_case(conn, j, args.phase, case + "-before-health")
                        valid = build.get("status") == "PASS" and build.get("response", {}).get("swBuildId") == conn.expected_build and health.get("status") == "PASS"
                        j.put(args.phase, case, "PASS" if valid else "FAIL", stage="before",
                              uptime_ms=health.get("health", {}).get("uptime_ms"),
                              reason="ready for manual power removal" if valid else "baseline read or health failed")
                else:
                    before = next((e for e in reversed(prior) if e.get("case") == case and e.get("stage") == "before" and e.get("status") == "PASS"), None)
                    if any(e.get("case") == case and e.get("stage") == "after" and e.get("status") == "PASS" for e in prior):
                        print(f"{case} already passed; keeping the original evidence", flush=True)
                    elif before is None:
                        j.put(args.phase, case, "UNVERIFIED", stage="after", reason="no passing before snapshot")
                    else:
                        build = read_case(conn, j, args.phase, case + "-after-build", "genBasic", ["swBuildId"])
                        if build.get("status") != "PASS":
                            time.sleep(5)
                            build = read_case(conn, j, args.phase, case + "-after-build-retry", "genBasic", ["swBuildId"])
                        health = health_case(conn, j, args.phase, case + "-after-health")
                        if health.get("status") != "PASS":
                            time.sleep(5)
                            health = health_case(conn, j, args.phase, case + "-after-health-retry")
                        time.sleep(2)
                        follow = health_case(conn, j, args.phase, case + "-after-health-followup")
                        first_uptime = health.get("health", {}).get("uptime_ms")
                        second_uptime = follow.get("health", {}).get("uptime_ms")
                        valid = (build.get("status") == "PASS" and build.get("response", {}).get("swBuildId") == conn.expected_build and
                                 health.get("status") == follow.get("status") == "PASS" and
                                 isinstance(first_uptime, int) and isinstance(second_uptime, int) and
                                 first_uptime < before["uptime_ms"] and second_uptime > first_uptime and
                                 not check_health_delta(health["health"], follow["health"]))
                        if valid:
                            valid = ota_case(conn, j, args.phase, case + "-ota")
                        j.put(args.phase, case, "PASS" if valid else "FAIL", stage="after",
                              reason="fresh build, joined health, rebooted uptime and OTA check" if valid else "power-cycle verification failed")
        elif args.phase == "soak":
            initial = health_case(conn, j, args.phase, "health-start")
            previous = initial.get("health") if initial.get("status") == "PASS" else None
            for n in range(1, 5):
                if previous is None:
                    j.put(args.phase, f"checkpoint-{n}", "FAIL", reason="no healthy initial snapshot")
                    break
                print(f"Waiting five minutes for idle checkpoint {n}/4", flush=True)
                time.sleep(300)
                build = read_case(conn, j, args.phase, f"build-{n}", "genBasic", ["swBuildId"])
                health = health_case(conn, j, args.phase, f"health-{n}")
                current = health.get("health")
                reason = ("build mismatch or failed read" if build.get("status") != "PASS" or build.get("response", {}).get("swBuildId") != conn.expected_build else
                          "health unavailable" if health.get("status") != "PASS" else check_health_delta(previous, current))
                if reason:
                    j.put(args.phase, f"checkpoint-{n}", "FAIL", reason=reason)
                    break
                if not ota_case(conn, j, args.phase, f"ota-{n}"):
                    j.put(args.phase, f"checkpoint-{n}", "FAIL", reason="OTA availability failed twice")
                    break
                j.put(args.phase, f"checkpoint-{n}", "PASS", reason="identity, health and OTA check passed")
                previous = current
    finally:
        conn.close()
        result=j.report()
        lock.close()
    latest={}
    for e in result['events']:
        if e.get('phase')==args.phase:latest[(e.get('case'),e.get('stage'))]=e.get('status')
    return 0 if latest and all(s=='PASS' for s in latest.values()) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("Interrupted; completed journal entries are preserved.", file=sys.stderr)
        sys.exit(130)
