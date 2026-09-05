# v2 defect note — `glsd_ota_forensics.py` can certify an unvalidated image

**Found by:** Executor, Batch 2 step 1 (tool upgrade review), before running v2 on any control sample.
**Severity:** the tool returns a clean-looking `VERIFIED_PLAIN_TELINK_OTA` + `NO_CONTAINER_AUTH_DETECTED`
for images whose integrity was **never checked**. It fails open.
**Reachability on real data:** **not triggered by any of the 218 real public images measured in
this session** — see "Real-world reachability" below. The defect is latent, reachable only via
crafted or truncated input, and does **not** invalidate any Batch 1 or Batch 2 verdict.
**Status:** implementation fix left to the Supervisor — Batch 2 is run-only and issue #1 forbids
the Executor inventing protocol/implementation changes.

## Root cause

`analyze()` gates only on one literal:

```python
crc_res = validate_app_crc(app, telink["declared_size"])
...
if crc_res["status"] == "FAIL":
    result["container_verdict"] = "INVALID_TELINK_CRC"
    return result
result["container_verdict"] = "VERIFIED_PLAIN_TELINK_OTA"   # reached for ANY non-"FAIL" status
```

but `validate_app_crc()` has a **third** return state besides `PASS`/`FAIL`:

```python
if declared_size < 4 or declared_size > len(app):
    return {"status": "size_out_of_range"}          # no stored/computed keys at all
```

`"size_out_of_range"` is not `"FAIL"`, so control falls through to the verified verdict with no
CRC ever having been computed against real data. The trust signal and the validation result are
decoupled.

## It is also a regression against v1

v1 carried an explicit equality gate that v2 dropped:

```python
declared_size = struct.unpack_from("<I", app, DECLARED_SIZE_OFFSET)[0]
if declared_size != length:                          # v1 — GONE in v2
    result["container_verdict"] = "INVALID_TELINK_DECLARED_SIZE"
    return result
```

Restoring that check alone closes every case below, because each mutant's inner `declared_size`
disagrees with its sub-element length.

## Reproduction

`v2-crc-gate-probe.py` (same directory) imports the tool by path and feeds it mutants derived
from the real artifact. Mutants are written to `.local/` and deleted; no binary lands in the repo.
Run from the repository root. Machine-readable result: [`v2-crc-gate-probe.json`](v2-crc-gate-probe.json).

Baseline: real artifact → `VERIFIED_PLAIN_TELINK_OTA` (correct).

```text
CASE                         INJECTED_DECLSZ PAYLOAD_CORRUPT        CRC_STATUS             VERDICT
--------------------------------------------------------------------------------------------------------------
A_declared_size_ffffffff     0xFFFFFFFF     False                  size_out_of_range      VERIFIED_PLAIN_TELINK_OTA
B_declared_size_zero         0x0            False                  size_out_of_range      VERIFIED_PLAIN_TELINK_OTA
C_declared_size_two          0x2            False                  size_out_of_range      VERIFIED_PLAIN_TELINK_OTA
D_declared_size_midrange     0x3E8          False                  FAIL                   INVALID_TELINK_CRC
E_payload_corrupt_crc_stale  -              True                   FAIL                   INVALID_TELINK_CRC
F_declared_size_plus_one     0x33EC5        False                  size_out_of_range      VERIFIED_PLAIN_TELINK_OTA
--------------------------------------------------------------------------------------------------------------
cases still certified VERIFIED_PLAIN_TELINK_OTA: A, B, C, F
```

Four of six crafted images are accepted. `A` is the worst: an image declaring a 4 GiB
`declared_size` — structurally impossible for a 512 KiB Telink part — is certified as a verified
plain Telink image with no container authentication. `F` is the quiet one: a single-byte
discrepancy against the sub-element length, which v1 rejected outright.

`D` and `E` behave correctly and show the CRC path itself is sound: `E` flips one payload byte deep
inside the image without touching the stored CRC and is caught as `INVALID_TELINK_CRC`. So real
bit-corruption detection works; the hole is specifically that an *unverifiable* size is treated as
*verified*.

## Effect on the Batch 1 result

**None.** The historical `GL-C-009P(MINI)` artifact has `declared_size == 212676 ==` sub-element
length, so `validate_app_crc` returned a genuine `PASS` with `stored == computed == 0xBC753C12`.
Its `crc_validation.status` is `"PASS"`, not `"size_out_of_range"`. Batch 1's Gate-0 result stands
unchanged and is independently reproduced by [`verify_independent.py`](verify_independent.py).

## Real-world reachability — measured, and it revises my first reading

My initial framing was that this defect undermines the Batch 2 control verdict. **Measured
against real data, it does not, and I am correcting that claim.**

Running v2 over **218 real public Zigbee OTA images** materialised from `Koenkk/zigbee-OTA`
(see [`../control-telink/CORPUS-BASELINE.md`](../control-telink/CORPUS-BASELINE.md)):

- 203 images certified `VERIFIED_PLAIN_TELINK_OTA`;
- **all 203 had `crc_validation.status == "PASS"`** — a genuine checksum comparison;
- **0 were certified through the `size_out_of_range` fail-open path**;
- an independent bit-serial parse confirmed marker + CRC on 203/203.

So every real image in the corpus reaches the CRC check with a sane `declared_size`, and the
hole is reachable only via crafted, truncated, or internally inconsistent input. It is still a
real defect — the tool is intended to vet images we are about to *construct*, where a botched
builder could easily emit an out-of-range `+0x18`, and it would be blessed silently — but it is
a hardening bug, not a live correctness failure, and neither the Batch 1 Gate-0 result nor the
Batch 2 control result needs to be retracted because of it.

## Suggested fix (Supervisor's call)

```python
if crc_res["status"] != "PASS":
    result["container_verdict"] = "INVALID_TELINK_CRC" if crc_res["status"] == "FAIL" \
        else "INVALID_TELINK_DECLARED_SIZE"
    return result
```

plus restore v1's `declared_size == length` equality — **which the corpus now supports as a
generic Telink invariant rather than a GLEDOPTO quirk**: `+0x18 == containerLen` held on
204/204 plain-sub-element images measured, including the non-GLEDOPTO control. Restoring it is
therefore safe for this family, and it independently closes all four mutant cases.

Do **not** restore the inner-identity cross-match as a hard gate. See
[`../control-telink/SUMMARY.md`](../control-telink/SUMMARY.md) — the control shows Telink's own
SDK leaves `+0x12`/`+0x14`/`+0x02` zero-filled, so outer==inner identity is a GLEDOPTO build
habit, not an SDK guarantee.

Consider also bounding `declared_size` to the 512 KiB geometry (`<= 0x40000`) so an impossible
size is rejected on hardware grounds rather than falling through. Consumers of this verdict —
the acceptance-probe builder and the dump-stager host state machine — should treat anything
other than `PASS` as unverified.

