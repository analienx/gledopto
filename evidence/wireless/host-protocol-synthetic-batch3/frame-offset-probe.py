import importlib.util
import struct

spec = importlib.util.spec_from_file_location('host', r'tools/glsd_wireless_dump_host.py')
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)

payload = bytes(range(48))          # non-uniform: a 1-byte shift is now visible
hdr = struct.calcsize('<HBIB')
print('READ_RSP header size per spec and per the code\'s own unpack: %d bytes' % hdr)
print('payload sent : %s ...' % payload[:8].hex(' '))

conformant = struct.pack('<HBIB', 1, 0, 0x1000, 48) + payload + struct.pack('<I', h.xcrc32(payload))
print('\n--- A) exactly spec-conformant frame, %d bytes ---' % len(conformant))
try:
    h.parse_read_rsp(conformant)
    print('    accepted')
except ValueError as e:
    print('    REJECTED: %s   <- host cannot consume a conformant chunk' % e)

print('\n--- B) same frame plus one padding byte (passes the guard) ---')
padded = conformant + b'\x00'
p = h.parse_read_rsp(padded)
print('    payload parsed: %s ...' % p['payload'][:8].hex(' '))
print('    equals sent? %s' % (p['payload'] == payload))
print('    first differing index: %s' % next((i for i in range(48)
                                              if p['payload'][i] != payload[i]), None))
print('    parsed payload is the sent payload shifted by 1 byte: %s'
      % (p['payload'][:47] == payload[1:48]))
print('    crc_valid reported: %s' % p['crc_valid'])

print('\n--- C) what the offsets should be ---')
manual_payload = conformant[hdr:hdr + 48]
manual_crc = struct.unpack_from('<I', conformant, hdr + 48)[0]
print('    payload at [%d:%d] == sent? %s' % (hdr, hdr + 48, manual_payload == payload))
print('    crc at [%d:] = 0x%08X, xcrc32(sent)=0x%08X, match=%s'
      % (hdr + 48, manual_crc, h.xcrc32(payload), manual_crc == h.xcrc32(payload)))
print('\n=> replacing the three 9s with 8 in parse_read_rsp makes A accepted and correct.')
