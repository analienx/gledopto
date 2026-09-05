"""Diagnose the Batch 3 failures without modifying any repo file.

Answers: which artefact is at fault for each failure (host module, test file, or spec),
and is there a defect the 4 tests would NOT have caught even if they passed?
"""
import importlib.util
import struct

spec = importlib.util.spec_from_file_location('host', r'tools/glsd_wireless_dump_host.py')
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)

print('=== 1. build_read_req vs the frozen spec (READ_REQ: u8,u32,u8,u16 = 8 bytes) ===')
req = h.build_read_req(region=0, offset=0x1000, length=48, sequence=1)
print('   emitted        %r  (%d bytes)' % (req, len(req)))
print('   spec size      8 bytes  -> %s' % ('CONFORMS' if len(req) == 8 else 'VIOLATES'))
r, o, l, s = struct.unpack_from('<BIBH', req, 0)
print('   round-trip     region=%d offset=0x%X length=%d sequence=%d  -> %s'
      % (r, o, l, s, 'fields decode exactly' if (r, o, l, s) == (0, 0x1000, 48, 1) else 'MISMATCH'))
print("   the test's own literal b'\\x00\\x00\\x10\\x00\\x00\\x30\\x01\\x00' is %d bytes"
      % len(b'\x00\x00\x10\x00\x00\x30\x01\x00'))
print("   => literal is CORRECT; `assert len(req) == 9` is the faulty line.")

print('\n=== 2. READ_RSP header size: spec vs parse_read_rsp offsets ===')
hdr = struct.calcsize('<HBIB')
print('   spec READ_RSP header  seq u16 + region u8 + offset u32 + len u8 = %d bytes' % hdr)
print('   code unpacks at       <HBIB (=%d) but slices payload at [9:] and reads CRC at 9+length' % hdr)
print('   guard                 `if len(data) < 9 + length + 4: raise`')

payload = b'A' * 48
frame = struct.pack('<HBIB', 1, 0, 0x1000, 48) + payload + struct.pack('<I', h.xcrc32(payload))
print('\n   a fully spec-conformant frame: %d bytes (8 hdr + 48 data + 4 crc)' % len(frame))
try:
    p = h.parse_read_rsp(frame)
    print('   parse_read_rsp -> ACCEPTED, crc_valid=%s' % p['crc_valid'])
except ValueError as e:
    print('   parse_read_rsp -> **REJECTED** ValueError: %s' % e)
    print('   (frame is %d bytes, guard demands %d, so 8==9 off-by-one rejects every real chunk)'
          % (len(frame), 9 + 48 + 4))

print('\n=== 3. the same bug does not merely reject - it silently mis-frames ===')
padded = frame + b'\x00'
try:
    p = h.parse_read_rsp(padded)
    print('   padded frame accepted. payload recovered == original? %s'
          % (p['payload'] == payload))
    print('   payload[0:3]=%r expected %r  offset_shift=%d'
          % (p['payload'][:3], payload[:3], 9 - hdr))
    print('   crc_valid=%s  -> %s' % (p['crc_valid'],
          'silently misparsed AND reported as valid' if p['crc_valid'] else 'flagged'))
except ValueError as e:
    print('   padded frame ValueError: %s' % e)
    print('   demonstrating that a frame with one byte of real padding mis-slices payload by 1')

print('\n=== 4. spec Safety Invariant 4: "Host MUST track sequence to detect dropped frames" ===')
src = open(r'tools/glsd_wireless_dump_host.py', encoding='utf-8').read()
seq_uses = [i + 1 for i, line in enumerate(src.splitlines()) if 'sequence' in line or 'seq' in line]
print('   lines mentioning seq:', seq_uses)
print('   ResumeBitmap tracks by OFFSET only (mark_received(offset)); nothing compares or '
      'gaps-checks `sequence` anywhere in the module.')
print('   -> invariant 4 is NOT implemented by the host, and no test covers it.')

print('\n=== 5. resume bitmap vs a real region size ===')
bm = h.ResumeBitmap(212676, 48)
print('   total_chunks for the 0x33EC4 historical app size:', bm.total_chunks)
print('   missing offsets list length when empty:', len(bm.get_missing_offsets()))
bm.mark_received(212675)
print('   mark_received(last byte offset) accepted? %s (idx=%d)'
      % (bm.is_received(212675), 212675 // 48))
print('   mark_received(9999999) out of range ->', h.ResumeBitmap(100, 48).is_received(9999999))
