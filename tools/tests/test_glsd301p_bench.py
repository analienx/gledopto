"""Read and OTA correlation tests; run on the hosted exact-SHA CI lane."""
from pathlib import Path
import queue
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import glsd301p_bench as bench


class FakeConnection(bench.Connection):
    def __init__(self, producer):
        self.base = "zigbee2mqtt"
        self.events = queue.Queue()
        self.producer = producer
        self.info = {'config': {'devices': {bench.TARGET: {'filtered_cache': ['bench_read_.*']}}}}

    def publish(self, suffix, payload):
        self.producer(self, suffix, payload)


class BenchTests(unittest.TestCase):
    def test_missing_cache_filter_never_publishes_read(self):
        c = FakeConnection(lambda *args: self.fail('unsafe read was published'))
        c.info = {'config': {'devices': {}}}
        self.assertEqual(c.read('genBasic', ['swBuildId'], timeout=.01)['status'], 'FAIL')

    def test_cache_filter_requires_full_uuid_match_and_visible_response(self):
        make = lambda opts: {'config': {'devices': {bench.TARGET: opts}}}
        self.assertFalse(bench.diagnostic_cache_safe(make({'filtered_cache': ['^bench_read_']})))
        self.assertTrue(bench.diagnostic_cache_safe(make({'filtered_cache': ['bench_read_.*']})))
        self.assertFalse(bench.diagnostic_cache_safe(make({'filtered_cache': ['bench_read_.*'], 'filtered_attributes': ['bench_read_.*']})))

    def test_profile_rejects_other_device(self):
        with self.assertRaises(ValueError):
            bench.validate_profile({"ieee": "0x0000000000000000", "build": bench.BUILD})

    def test_named_and_numeric_attributes_use_same_keys(self):
        self.assertEqual(bench.requested_keys("genOta", [0, 2, 6]),
                         bench.requested_keys("genOta", ["upgradeServerId", "currentFileVersion", "imageUpgradeStatus"]))

    def test_unidentified_success_is_never_pass(self):
        def send(c, suffix, payload):
            c.events.put(("zigbee2mqtt/bridge/logging", {"message": "Read result of 'genBasic': {\"swBuildId\":\"GLSD-ED-008\"}"}, False))
        result = FakeConnection(send).read("genBasic", ["swBuildId"], timeout=0.1)
        self.assertEqual(result["status"], "UNVERIFIED")

    def test_unique_property_on_exact_target_is_pass(self):
        def send(c, suffix, payload):
            key = payload["read"]["state_property"]
            c.events.put(("zigbee2mqtt/" + bench.TARGET, {key: {"swBuildId": bench.BUILD}}, False))
        self.assertEqual(FakeConnection(send).read("genBasic", ["swBuildId"], timeout=.1)["status"], "PASS")

    def test_same_property_from_wrong_topic_is_ignored(self):
        def send(c, suffix, payload):
            key = payload["read"]["state_property"]
            c.events.put(("zigbee2mqtt/0xwrong", {key: {"swBuildId": bench.BUILD}}, False))
        self.assertEqual(FakeConnection(send).read("genBasic", ["swBuildId"], timeout=.01)["status"], "UNVERIFIED")

    def test_retained_property_is_ignored(self):
        def send(c, suffix, payload):
            key = payload["read"]["state_property"]
            c.events.put(("zigbee2mqtt/" + bench.TARGET, {key: {"swBuildId": bench.BUILD}}, True))
        self.assertEqual(FakeConnection(send).read("genBasic", ["swBuildId"], timeout=.01)["status"], "UNVERIFIED")

    def test_stale_queued_result_is_not_accepted(self):
        c = FakeConnection(lambda *args: None)
        c.events.put(("zigbee2mqtt/bridge/logging", {"message": "Read result of 'genBasic': {\"swBuildId\":\"GLSD-ED-008\"}"}, False))
        self.assertIn("timeout", c.read("genBasic", ["swBuildId"], timeout=0.01)["reason"])

    def test_error_from_other_target_is_ignored(self):
        def send(c, suffix, payload):
            c.events.put(("zigbee2mqtt/bridge/logging", {"message": "0x1234/11 genOta.read([\"upgradeServerID\"], {}) failed"}, False))
        self.assertEqual(FakeConnection(send).read("genOta", ["upgradeServerID"], 1, .01)["status"], "UNVERIFIED")

    def test_ota_requires_matching_transaction_and_target(self):
        def send(c, suffix, payload):
            topic = "zigbee2mqtt/bridge/response/device/ota_update/check"
            c.events.put((topic, {"transaction": "old", "status": "ok", "data": {"id": bench.TARGET}}, False))
            c.events.put((topic, {"transaction": payload["transaction"], "status": "ok", "data": {"id": "0xwrong"}}, False))
        self.assertEqual(FakeConnection(send).ota(.1)["status"], "FAIL")

    def test_ota_timeout_fails(self):
        self.assertEqual(FakeConnection(lambda *args: None).ota(.01)["status"], "FAIL")

    def test_interrupted_journal_stays_unverified(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = bench.Journal(Path(tmp))
            journal.put("baseline", "build", "PASS", response={"swBuildId": bench.BUILD})
            with journal.path.open("a", encoding="utf-8") as stream:
                stream.write('{"phase":')
            result = journal.report()
            self.assertEqual(result["overall"], "UNVERIFIED")
            self.assertEqual(result["events"][-1]["reason"], "interrupted journal record")

    def test_health_delta_rejects_restart_or_new_fault(self):
        previous = {"uptime_ms": 1000, "uart_deadline_faults": 0,
                    "timer_reg_faults": 0, "parent_losses": 0,
                    "rejoin_failures": 0, "flags": {"fault_latched": False}, "exc_code": 0}
        current = dict(previous, uptime_ms=50)
        self.assertIn("uptime", bench.check_health_delta(previous, current))
        current = dict(previous, uptime_ms=2000, timer_reg_faults=1)
        self.assertIn("timer_reg_faults", bench.check_health_delta(previous, current))

    def test_guided_resume_never_assumes_power_was_cycled(self):
        with tempfile.TemporaryDirectory() as tmp:
            journal = bench.Journal(Path(tmp))
            for case in ("build", "health"):
                journal.put("baseline", case, "PASS")
            for case in ("server-name", "server-id", "version-name", "version-id", "status-name", "status-id", "combined-names", "combined-ids"):
                journal.put("attributes", case, "PASS")
            for n in range(1, 6):
                journal.put("ota", str(n), "PASS")
            journal.put("power-cycle", "cycle-1", "PASS", stage="before")
            args = SimpleNamespace(profile="profile.json", workdir=tmp, skip_soak=True)
            with mock.patch("builtins.input", side_effect=EOFError), mock.patch("subprocess.run") as run, mock.patch.object(journal, "report", return_value={"cases": []}):
                self.assertEqual(bench.guided(args, journal), 2)
                run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
