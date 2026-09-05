# Batch 3 — synthetic host-protocol tests: **3 failed, 1 passed**

**Task:** analienx/gledopto#1, Supervisor comment `2026-09-05T09:50:20Z`, Batch 3.
**Commit under test:** `607c777a5838356b08b10ae117d123e9ca891a3e`
**Environment:** Windows (win32), cmd.exe, Python 3.14.4, pytest 9.1.1 (already installed —
`pip install pytest` was not run, so no dependency change was made to this machine).

**Headline: the requested confirmation that "all 4 tests passed" cannot be given.**
The suite is 3 failed / 1 passed. Two of the three failures are trivial test bugs, but the third
failure class hides a **real defect in the host module** that would break every transfer against
a conformant stager, and there is a **fourth defect the four tests would not have caught even
when green**.

Raw evidence: [`pytest-output.txt`](pytest-output.txt) ·
[`frame-offset-probe.py`](frame-offset-probe.py) /
[`frame-offset-probe-output.txt`](frame-offset-probe-output.txt) ·
[`diagnostics.py`](diagnostics.py) / [`diagnostics-output.txt`](diagnostics-output.txt)

```text
tools/tests/test_host_protocol.py::test_build_read_req         FAILED
tools/tests/test_host_protocol.py::test_parse_read_rsp_valid   FAILED
tools/tests/test_host_protocol.py::test_parse_read_rsp_bad_crc FAILED
tools/tests/test_host_protocol.py::test_resume_bitmap          PASSED

whole suite: 3 failed, 8 passed in 0.17s
```

## Fault attribution — checked, not assumed

Important: **`build_read_req` is correct and I was wrong about it on first reading.** I initially
suspected it of packing `length` as u16 against a spec u8. Running the code disproved that — see
[Correction below](#correction-i-initially-misread-the-packer). Report the failure, then the
diagnosis, because the naive diagnosis is wrong here.

### Failure 1 — `test_build_read_req`: the test contradicts itself, the code is fine

```text
assert len(req) == 9
E   AssertionError: assert 8 == 9
E    +  where 8 = len(b'\x00\x00\x10\x00\x000\x01\x00')
```

Per `docs/WIRELESS_DUMP_PROTOCOL.md`, READ_REQ is `region u8 + offset u32 + length u8 +
sequence u16` = **8 bytes**, and the emitted value is exactly that:

```text
emitted    b'\x00\x00\x10\x00\x000\x01\x00'  (8 bytes)   -> CONFORMS
round-trip region=0 offset=0x1000 length=48 sequence=1  -> fields decode exactly
```

The test's *own* expected literal on the next line is 8 bytes and is **correct**. So the file
asserts both `len == 9` and `== <an 8-byte literal>`, which can never both hold.

**Minimal fix (test only):** line 8 → `assert len(req) == 8`.

### Failures 2 and 3 — `NameError: name 'struct' is not defined`

`tools/tests/test_host_protocol.py` never imports `struct`, yet both READ_RSP tests build frames
with `struct.pack`. Verbatim traceback at [`pytest-output.txt`](pytest-output.txt).

This is **not** Windows-specific — it fails identically on any platform; there is no
platform-conditional import involved. Flagging that because the Batch 3 constraint permitted
edits for a *Windows* import error, and this is not one.

**However, adding the import would not make these tests pass.** The next line is blocked by a
genuine host bug:

### The defect hiding behind the `NameError` — `parse_read_rsp` is off by one

The READ_RSP header is **8** bytes (`sequence u16, region u8, offset u32, length u8`) — by spec
*and* by the module's own unpack `struct.unpack_from("<HBIB", data, 0)`, which consumes exactly 8.
But the guard and both slices use **9**:

```python
if len(data) < 9:                                  # header is 8
...
if len(data) < 9 + length + 4:                     # -> demands 61 for a 60-byte conformant frame
...
payload    = data[9:9+length]                      # one byte late
stored_crc = struct.unpack_from("<I", data, 9+length)[0]
```

Proven against a fully conformant frame ([`frame-offset-probe-output.txt`](frame-offset-probe-output.txt)):

```text
--- A) exactly spec-conformant frame, 60 bytes ---
    REJECTED: READ_RSP truncated payload/CRC   <- host cannot consume a conformant chunk

--- B) same frame plus one padding byte (passes the guard) ---
    payload sent  : 00 01 02 03 04 05 06 07 ...
    payload parsed: 01 02 03 04 05 06 07 08 ...
    parsed payload is the sent payload shifted by 1 byte: True
    crc_valid reported: False

--- C) what the offsets should be ---
    payload at [8:56] == sent? True
    crc at [56:] = 0xFADFDE8E, xcrc32(sent)=0xFADFDE8E, match=True
```

Two consequences, in order of severity:

- **Interop:** against a spec-conformant stager, the host rejects **100 % of chunks** — every
  READ_RSP comes back as "truncated". The dump cannot begin.
- **Data integrity:** when a frame *does* clear the guard, the recovered bytes are the real
  payload shifted by one. The saving grace is that the CRC is then computed over the wrong window,
  so `crc_valid` reports `False` — it **fails loudly rather than silently corrupting**. That is
  genuinely good design in the existing code and worth preserving. But it means a "CRC mismatch on
  every frame" symptom in the field would actually be this off-by-one, not radio corruption, so
  fix it before any real capture or the diagnostic signal will be misleading.

**Minimal fix (host only):** change the three literal `9`s in `parse_read_rsp` to `8` — or better,
name the constant once:

```python
READ_RSP_HEADER = struct.calcsize("<HBIB")   # 8, and keeps spec + code anchored together
```

so the unpack format string and the slice offsets cannot drift apart again. `build_read_req`
should get the same treatment (`READ_REQ_HEADER = struct.calcsize("<BIBH")`).

## A fourth defect: the spec's own invariant is unimplemented and untested

`docs/WIRELESS_DUMP_PROTOCOL.md` Safety Invariant 4 states:

> Host MUST track `sequence` to detect dropped frames.

`parse_read_rsp` *returns* `sequence`, but **nothing in the module ever compares, orders or
gap-checks it**. [`diagnostics-output.txt`](diagnostics-output.txt) shows the only uses are the
struct pack/unpack; `ResumeBitmap` tracks by **offset** only. So:

- A duplicated chunk that happens to carry a stale sequence number is accepted silently.
- Out-of-order arrival is invisible; only the offset bitmap notices, and it cannot distinguish
  "retransmit of a good chunk" from "two different chunks claiming the same offset".
- `mark_received()` **silently no-ops on out-of-range offsets** rather than raising, so a stager
  returning a nonsense offset produces an incomplete dump that resumes "successfully" forever.

This is not academic at our scale: the historical application region is `0x33EC4` bytes, which at
the frozen 48-byte chunk size is **4431 request/response pairs per bank**. Over a congested 2.4 GHz
Zigbee network, dropped and reordered frames are an expectation, not an edge case — which is
exactly why the invariant was written.

None of the four tests covers it. Recommendation: before Batch 3 can be called proven, add
(a) a sequence-monotonicity/duplicate check with an explicit expected-sequence cursor, and (b) a
test asserting an out-of-range `mark_received` raises instead of no-opping.

## Smaller observations, for your call

- `build_read_req` permits `length` up to 64 (matching the spec's "max 64"), while
  `CHUNK_SIZE = 48` and the research-branch stager gates READ to **1..48**. The host will happily
  build a 64-byte request that the stager is specified to refuse. Harmless now, but the host-side
  limit should match the stager-side limit, or the discrepancy will read as a device bug.
- `ResumeBitmap.load()` restores `received` from JSON without validating `len(map) ==
  total_chunks`, so a state file written for a different region size would be accepted and
  silently mis-indexed.
- `REGION_INACTIVE_APP = 0` is defined but never enforced — the host will build a
  `region=4` (nv) read request without objection. Invariant 1 puts that duty on the stager, so
  this is defensible, but a host-side default-deny is cheap defence-in-depth.

## Constraint compliance

- **No repo Python file was modified**, in this batch or by this diagnosis. Every probe lives in
  gitignored `.local/`; the files committed here are outputs and read-only probes.
- No live device interaction. No Tuya migration. No flash, OTA, or reset.
- `pytest` was **not** installed — it was already present at 9.1.1, so step 2 was a no-op.
- The two tracebacks that the constraint asked me to report verbatim are in
  [`pytest-output.txt`](pytest-output.txt), and both are plain platform-independent bugs rather
  than Windows import breakage, which is why I did not treat them as an authorisation to edit.

### Correction: I initially misread the packer

My first reading of `build_read_req` was that it packed `length` as `H` where the spec says `u8`,
i.e. that the *host* was at fault for failure 1. `<BIBH` is B-I-B-H — the `H` belongs to
`sequence`, not `length` — and the module is spec-conformant at 8 bytes. I am recording the
mistake because the wrong diagnosis would have pointed the fix at the production module instead of
at the test assertion, which is the more dangerous of the two edits to make in the wrong place.
