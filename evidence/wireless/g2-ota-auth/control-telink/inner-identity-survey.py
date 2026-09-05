"""Is the inner Telink identity (mfg/type/version) reliably populated across publishers?

Tests whether Batch 1's Gate-0 item 3 ("outer identity == inner identity") is a container
invariant or a per-build choice, across marker-positive images from independent publishers.
Also shows that cryptographic sub-elements can sit INSIDE the sub-element chain while
trailing_bytes == 0, which bounds how "no container auth" may be argued.

Read-only; opens binaries from gitignored .local/ only.
"""
import importlib.util
import glob
import json
import struct

spec = importlib.util.spec_from_file_location('forensics', r'tools/glsd_ota_forensics.py')
fx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fx)

SECOND = r'.local\ota-secondaries\full'

WANT = [
    ('pvvx', r'pvvx__1141-020a-01393001-Z03MMC.zigbee', 'pvvx Z03MMC'),
    ('Tuya', r'Tuya__1662545193-oem_zg_tl8258_plug_OTA_3.0.0.bin', 'Tuya OEM TL8258 plug 3.0.0'),
    ('Innr', r'Innr__1166-0109*.ota', 'Innr bb262'),
    ('ThirdReality', r'ThirdReality__SmartPlug_Zigbee_PROD_OTA_V101*.ota', 'ThirdReality SmartPlug V101'),
    ('Yandex', r'Yandex__132f-0212*.zigbee', 'Yandex YNDX-00530'),
    ('Candeo', r'Candeo__C-ZB-LC20v2*.ota', 'Candeo C-ZB-LC20v2'),
    ('Gledopto', r'Gledopto__GL-C-002P_V20551203*.ota', 'GLEDOPTO GL-C-002P'),
]

rows = []
print('%-30s | %-26s | %-26s %s' % ('IMAGE', 'OUTER mfg/type/ver', 'INNER mfg/type/ver', 'RESULT'))
print('-' * 104)
for _, pat, label in WANT:
    hits = sorted(glob.glob(SECOND + '\\' + pat))
    if not hits:
        print('%-30s NOT FOUND (%s)' % (label, pat))
        continue
    d = open(hits[0], 'rb').read()
    hl = struct.unpack_from('<H', d, 6)[0]
    om, ot, ov = (struct.unpack_from('<H', d, 10)[0], struct.unpack_from('<H', d, 12)[0],
                  struct.unpack_from('<I', d, 14)[0])
    p = hl + 6
    im, it, iv = (struct.unpack_from('<H', d, p + 0x12)[0],
                  struct.unpack_from('<H', d, p + 0x14)[0],
                  struct.unpack_from('<I', d, p + 0x02)[0])
    same = (om == im and ot == it and ov == iv)
    allzero = (im == 0 and it == 0 and iv == 0)
    res = 'inner == outer' if same else ('inner ALL-ZERO' if allzero else 'inner DIFFERS')
    print('%-30s | 0x%04X 0x%04X 0x%08X | 0x%04X 0x%04X 0x%08X %s'
          % (label[:30], om, ot, ov, im, it, iv, res))
    rows.append({'image': label, 'path': hits[0].split('ota-secondaries')[-1],
                 'outer': {'mfg': '0x%04X' % om, 'type': '0x%04X' % ot, 'ver': '0x%08X' % ov},
                 'inner': {'mfg': '0x%04X' % im, 'type': '0x%04X' % it, 'ver': '0x%08X' % iv},
                 'inner_eq_outer': same, 'inner_all_zero': allzero, 'result': res})

pop = sum(1 for r in rows if r['inner_eq_outer'])
zero = sum(1 for r in rows if r['inner_all_zero'])
diff = sum(1 for r in rows if not r['inner_eq_outer'] and not r['inner_all_zero'])
print('\npublishers sampled: %d -> inner==outer %d, inner all-zero %d, inner differs %d'
      % (len(rows), pop, zero, diff))

# A real container where crypto material lives INSIDE the chain, trailing_bytes == 0.
print('\n=== crypto sub-elements with trailing_bytes == 0 (Develco smoke sensor) ===')
dv = sorted(glob.glob(r'.local\ota-secondaries\bonus__Develco*'))
develco = None
if dv:
    r = fx.analyze(dv[0])
    develco = {'path': dv[0].split('ota-secondaries')[-1],
               'verdict': r['container_verdict'],
               'auth_indicator': r.get('auth_indicator'),
               'trailing_bytes': r.get('trailing_bytes'),
               'subelements': r.get('subelements')}
    print('   verdict=%s auth=%s trailing_bytes=%d' %
          (develco['verdict'], develco['auth_indicator'], develco['trailing_bytes']))
    for e in develco['subelements']:
        print('      tag %-8s length %-8d offset %s' % (e['tag'], e['length'], e.get('offset')))

json.dump({'purpose': 'inner identity reliability + in-chain crypto bounding',
           'samples': rows, 'tally': {'eq_outer': pop, 'all_zero': zero, 'differs': diff},
           'develco_multi_subelement': develco,
           'conclusion': ('Inner identity is build-dependent: some publishers populate it, one '
                          'zeroes it, one carries a different inner version. Not a container '
                          'invariant; must not be hard-gated. Separately, trailing_bytes == 0 '
                          'does NOT prove absence of authentication - crypto sub-elements can '
                          'sit inside the chain, as the Develco container shows.')},
          open(r'evidence\wireless\g2-ota-auth\control-telink\inner-identity-survey.json', 'w'),
          indent=2)
print('\nwrote evidence/.../inner-identity-survey.json')
