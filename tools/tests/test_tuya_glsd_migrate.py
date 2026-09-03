import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

MOD_PATH = Path(__file__).resolve().parents[1] / 'tuya_glsd_migrate.py'
spec = importlib.util.spec_from_file_location('tuya_glsd_migrate', MOD_PATH)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class TestTuyaGlsdMigrate(unittest.TestCase):
    def test_parse_ota(self):
        header = bytearray(56)
        struct.pack_into('<IHHHHHIH', header, 0,
                         mod.OTA_MAGIC, 0x0100, 56, 0,
                         0x124F, 0x1416, 0x26013001, 2)
        header[20:20+9] = b'GLEDOPTO\x00'
        payload = bytearray(64)
        struct.pack_into('<I', payload, 4, 0x26013001)
        struct.pack_into('<I', payload, 8, 0x544C4E4B)
        struct.pack_into('<H', payload, 0x12, 0x124F)
        struct.pack_into('<H', payload, 0x14, 0x1416)
        struct.pack_into('<I', payload, 0x18, len(payload))
        data = header + payload
        struct.pack_into('<I', data, 52, len(data))
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'fw.ota'
            p.write_bytes(data)
            info = mod.parse_ota(p)
        self.assertTrue(info['is_zigbee_ota'])
        self.assertEqual(info['manufacturer_code_hex'], '0x124F')
        self.assertEqual(info['image_type_hex'], '0x1416')
        self.assertEqual(info['file_version_hex'], '0x26013001')
        self.assertTrue(info['size_matches_header'])
        self.assertEqual(info['telink_payload']['boot_marker_0x08_hex'], '0x544C4E4B')

    def test_collect_urls_filters_non_firmware_urls(self):
        obj = {
            'device': {'icon': 'https://example.com/icon.png'},
            'firmware': {'url': 'https://cdn.example.com/fw.bin'},
            'upgrade_infos': [{'fw_url': 'https://cdn.example.com/fw2.bin'}],
        }
        urls = mod.collect_urls(obj)
        vals = [u for _, u in urls]
        self.assertIn('https://cdn.example.com/fw.bin', vals)
        self.assertIn('https://cdn.example.com/fw2.bin', vals)
        self.assertNotIn('https://example.com/icon.png', vals)

    def test_reporting_payload_accepts_z2m_database_field_names(self):
        payload = mod.reporting_to_payload('LivingRoomCircleLightDimmer', {
            'cluster': 8, 'attrId': 0, 'minRepIntval': 5, 'maxRepIntval': 65000, 'repChange': 1,
        })
        self.assertEqual(payload['minimum_report_interval'], 5)
        self.assertEqual(payload['maximum_report_interval'], 65000)
        self.assertEqual(payload['reportable_change'], 1)

    def test_compare_state_detects_missing_group_and_binding(self):
        snapshot = {
            'target': {'ieee': mod.TARGET_IEEE},
            'endpoint_state': {
                'bindings': [
                    {'cluster': 'genOnOff', 'type': 'endpoint', 'ieee': '0x00124b0000000001', 'endpoint': 1, 'group': None}
                ],
                'configured_reporting': [
                    {'cluster': 'genOnOff', 'attrId': 0, 'minRepIntval': 0, 'maxRepIntval': 65000, 'repChange': 1}
                ],
            },
            'groups': [{'type': 'Group', 'groupID': 110, 'members': []}],
        }
        device = {
            'type': 'Router', 'ieeeAddr': mod.TARGET_IEEE,
            'endpoints': {'11': {'binds': [], 'configuredReportings': []}},
            'interviewCompleted': True,
        }
        diff = mod.compare_state(snapshot, device, [])
        self.assertEqual(diff['missing_group_ids'], [110])
        self.assertEqual(len(diff['missing_bindings']), 1)
        self.assertEqual(len(diff['missing_reporting']), 1)
        self.assertTrue(diff['same_ieee'])


if __name__ == '__main__':
    unittest.main()
