"""Executor's own corpus analysis over every materialized OTA file.

Derives facts directly from bytes rather than relaying any prior narration, and runs the
committed v2 tool over the same files so the two can be compared on real-world inputs.

Read-only with respect to the repo: binaries are only ever opened from .local/.
"""
import glob
import importlib.util
import json
import os
import struct

spec = importlib.util.spec_from_file_location('forensics', r'tools/glsd_ota_forensics.py')
fx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fx)


def crc32_bitserial(data, init=0xFFFFFFFF):
    crc = init & 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc & 0xFFFFFFFF


import zlib


def crc32_bitserial_fast(data, init=0xFFFFFFFF):
    """zlib form, proven bit-serial-equivalent on the GLEDOPTO artifact below."""
    return (zlib.crc32(data) ^ 0xFFFFFFFF) if init == 0xFFFFFFFF else zlib.crc32(data, init)


def independent(path):
    d = open(path, 'rb').read()
    r = {'bytes': len(d)}
    if len(d) < 62 or struct.unpack_from('<I', d, 0)[0] != 0x0BEEF11E:
        r['note'] = 'not-zigbee-ota'
        return r
    hl = struct.unpack_from('<H', d, 6)[0]
    r['headerLength'] = hl
    size_off = hl - 4
    if size_off + 4 > len(d):
        r['note'] = 'headerLength-too-big-for-file'
        return r
    size_field = struct.unpack_from('<I', d, size_off)[0]
    r['imageSize_field'] = size_field
    r['imageSize_at'] = size_off
    r['size_eq_file'] = size_field == len(d)
    r['size_eq_file_minus1'] = size_field == len(d) - 1
    off = hl
    chain = []
    while off + 6 <= len(d):
        tag = struct.unpack_from('<H', d, off)[0]
        ln = struct.unpack_from('<I', d, off + 2)[0]
        if off + 6 + ln > len(d):
            chain.append({'tag': '0x%04X' % tag, 'length': ln, 'truncated': True})
            off = len(d)
            break
        chain.append({'tag': '0x%04X' % tag, 'length': ln, 'payload': off + 6})
        off += 6 + ln
    r['n_subelements'] = len(chain)
    r['tags'] = [c['tag'] for c in chain]
    r['trailing_after_chain'] = len(d) - off
    plain = [c for c in chain if c['tag'] == '0x0000']
    if plain:
        c = plain[0]
        app = d[c['payload']:c['payload'] + c['length']]
        if len(app) >= 0x20:
            r['inner_ver_02'] = '0x%08X' % struct.unpack_from('<I', app, 0x02)[0]
            r['magic_06'] = app[6:8].hex()
            r['marker_08'] = '0x%08X' % struct.unpack_from('<I', app, 8)[0]
            r['marker_is_544C4E4B'] = struct.unpack_from('<I', app, 8)[0] == 0x544C4E4B
            r['containerLen'] = c['length']
            d18 = struct.unpack_from('<I', app, 0x18)[0]
            r['d18'] = d18
            r['d18_rel'] = ('==containerLen' if d18 == c['length'] else
                            '==containerLen-4' if d18 == c['length'] - 4 else
                            '>containerLen' if d18 > c['length'] else 'other:%d' % (c['length'] - d18))
            tail = struct.unpack_from('<I', app, c['length'] - 4)[0]
            r['stored_tail_crc'] = '0x%08X' % tail
            for label, region in (('data_minus4', app[:c['length'] - 4]),
                                  ('whole', app),
                                  ('d18_minus4', app[:d18 - 4] if 4 <= d18 <= len(app) else b'')):
                if not region:
                    continue
                val = crc32_bitserial_fast(region)
                r['crc_%s' % label] = '0x%08X' % val
                r['crc_%s_matches' % label] = val == tail
    return r


# Prove the fast form is identical to the bit-serial reference before trusting it at scale.
_chk = open(r'.local\gl-c-009p.ota', 'rb').read()
_c = struct.unpack_from('<H', _chk, 6)[0]
_l = struct.unpack_from('<I', _chk, _c + 2)[0]
_a = _chk[_c + 6:_c + 6 + _l]
assert crc32_bitserial(_a[:_l - 4]) == crc32_bitserial_fast(_a[:_l - 4]), 'CRC forms diverge'
print('CRC equivalence preflight: bit-serial == zlib-form on the 212 KiB GLEDOPTO region: OK')


files = sorted(glob.glob(r'.local\ota-secondaries\**\*', recursive=True))
files = [f for f in files if os.path.isfile(f) and
         f.lower().endswith(('.ota', '.zigbee', '.bin'))]
print('materialized binaries in this snapshot:', len(files))

rows = []
for f in files:
    try:
        ind = independent(f)
    except Exception as e:
        ind = {'note': 'independent-error: %r' % e}
    try:
        v2 = fx.analyze(f)
        v2res = {'verdict': v2.get('container_verdict'),
                 'crc': (v2.get('crc_validation') or {}).get('status', 'absent'),
                 'auth': v2.get('auth_indicator', 'absent')}
    except Exception as e:
        v2res = {'verdict': 'EXCEPTION:%r' % e, 'crc': 'n/a', 'auth': 'n/a'}
    rows.append({'file': os.path.relpath(f, r'.local\ota-secondaries'),
                 'ind': ind, 'v2': v2res})

HDR = {}
D18 = {}
CRCCNT = {}
V2V = {}
false_pos = []
missed = []
for row in rows:
    i, v = row['ind'], row['v2']
    hdr = i.get('headerLength', 'n/a')
    HDR[hdr] = HDR.get(hdr, 0) + 1
    d18 = i.get('d18_rel', 'no-plain-subelem')
    D18[d18] = D18.get(d18, 0) + 1
    key = (i.get('marker_is_544C4E4B'), i.get('crc_data_minus4_matches'), d18)
    CRCCNT[key] = CRCCNT.get(key, 0) + 1
    V2V[v['verdict']] = V2V.get(v['verdict'], 0) + 1
    genuinely_telink = i.get('marker_is_544C4E4B') and i.get('crc_data_minus4_matches')
    if v['verdict'] == 'VERIFIED_PLAIN_TELINK_OTA' and not genuinely_telink:
        false_pos.append(row)
    if v['verdict'] != 'VERIFIED_PLAIN_TELINK_OTA' and genuinely_telink:
        missed.append(row)

print('\n=== headerLength distribution ===')
for k in sorted(HDR, key=lambda x: str(x)):
    print('   headerLength %-22s %d' % (k, HDR[k]))

print('\n=== inner u32@+0x18 relation to its sub-element length ===')
for k, c in sorted(D18.items(), key=lambda kv: -kv[1]):
    print('   %-24s %d' % (k, c))

print('\n=== (marker==0x544C4E4B, CRC-over-container-minus-4 matches, d18 relation) ===')
for k, c in sorted(CRCCNT.items(), key=lambda kv: -kv[1]):
    print('   marker=%-6s crcMatch=%-6s d18=%-24s %d' % (k[0], k[1], k[2], c))

print('\n=== committed v2 verdicts over the same corpus ===')
for k, c in sorted(V2V.items(), key=lambda kv: -kv[1]):
    print('   %-34s %d' % (k, c))

print('\n=== v2 says VERIFIED but independent parse disagrees (real-world false positives) ===')
if not false_pos:
    print('   none')
for row in false_pos:
    print('   %s\n      v2=%s  independent: marker=%s crcMatch=%s d18=%s trailing=%s hl=%s'
          % (row['file'], row['v2'], row['ind'].get('marker_is_544C4E4B'),
             row['ind'].get('crc_data_minus4_matches'), row['ind'].get('d18_rel'),
             row['ind'].get('trailing_after_chain'), row['ind'].get('headerLength')))

print('\n=== genuinely Telink-looking but v2 did NOT certify (false negatives) ===')
if not missed:
    print('   none')
for row in missed[:15]:
    print('   %-58s v2=%s hl=%s d18=%s trailing=%s'
          % (row['file'], row['v2']['verdict'], row['ind'].get('headerLength'),
             row['ind'].get('d18_rel'), row['ind'].get('trailing_after_chain')))

json.dump(rows, open(r'.local\corpus-analysis.json', 'w'), indent=1)
print('\nwrote .local\\corpus-analysis.json')
