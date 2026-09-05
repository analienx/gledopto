"""Adversarial probe of tools/glsd_ota_forensics.py v2 verdict logic.

Feeds synthetic mutants derived from the real historical artifact (written only to .local/,
never to the evidence tree or any tracked path) to test whether a bad image can still be
certified as VERIFIED_PLAIN_TELINK_OTA.
"""
import importlib.util
import json
import os
import struct

spec = importlib.util.spec_from_file_location('forensics', r'tools/glsd_ota_forensics.py')
fx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fx)

real = open(r'.local/gl-c-009p.ota', 'rb').read()
HL = struct.unpack_from('<H', real, 6)[0]
SLEN = struct.unpack_from('<I', real, HL + 2)[0]
PAYLOAD = HL + 6                       # 62
DECLSZ_ABS = PAYLOAD + 0x18            # inner declared-size field, file offset

print('baseline: real artifact inner declared_size = %d (0x%X)'
      % (struct.unpack_from('<I', real, DECLSZ_ABS)[0],
         struct.unpack_from('<I', real, DECLSZ_ABS)[0]))
print('baseline verdict:', json.loads(json.dumps(fx.analyze(r'.local/gl-c-009p.ota')))['container_verdict'])
print('')

cases = []

def mutant(name, declsz_value=None, flip_payload_byte=False):
    b = bytearray(real)
    if declsz_value is not None:
        struct.pack_into('<I', b, DECLSZ_ABS, declsz_value)
    if flip_payload_byte:
        # corrupt one application byte well inside the image WITHOUT fixing the CRC:
        # a genuine integrity failure that must be caught.
        b[PAYLOAD + 0x1000] ^= 0xFF
    p = os.path.join('.local', 'mutant_%s.ota' % name)
    open(p, 'wb').write(bytes(b))
    r = fx.analyze(p)
    os.remove(p)
    return name, declsz_value, flip_payload_byte, r

for name, kw in [
    ("A_declared_size_ffffffff", dict(declsz_value=0xFFFFFFFF)),
    ("B_declared_size_zero", dict(declsz_value=0)),
    ("C_declared_size_two", dict(declsz_value=2)),
    ("D_declared_size_midrange", dict(declsz_value=1000)),
    ("E_payload_corrupt_crc_stale", dict(flip_payload_byte=True)),
    ("F_declared_size_plus_one", dict(declsz_value=SLEN + 1)),
]:
    n, dv, fp, r = mutant(name, **kw)
    cv = r.get('crc_validation', {})
    cases.append({
        "case": n,
        "injected_inner_declared_size": None if dv is None else "0x%X" % dv,
        "payload_byte_flipped": fp,
        "tool_crc_status": cv.get('status', 'absent'),
        "tool_verdict": r['container_verdict'],
        "tool_auth_indicator": r.get('auth_indicator', 'absent'),
    })

print('%-28s %-14s %-22s %-22s %s'
      % ('CASE', 'INJECTED_DECLSZ', 'PAYLOAD_CORRUPT', 'CRC_STATUS', 'VERDICT'))
print('-' * 110)
bad = []
for c in cases:
    print('%-28s %-14s %-22s %-22s %s'
          % (c['case'], c['injected_inner_declared_size'] or '-',
             c['payload_byte_flipped'], c['tool_crc_status'], c['tool_verdict']))
    if c['tool_verdict'] == 'VERIFIED_PLAIN_TELINK_OTA':
        bad.append(c['case'])
print('-' * 110)
print('cases still certified VERIFIED_PLAIN_TELINK_OTA:', bad if bad else 'none')
json.dump({"baseline_verdict": "VERIFIED_PLAIN_TELINK_OTA", "cases": cases,
           "certified_badly": bad},
          open(r'.local\v2_adversarial.json', 'w'), indent=2)
