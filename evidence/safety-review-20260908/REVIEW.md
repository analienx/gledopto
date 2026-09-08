# Gledopto safety review — 2026-09-08

Reviewed PR #6 at `eb963bd40af680119b61dbbb480e8441ec577b03`, as pinned by the [handoff](https://github.com/analienx/gledopto/issues/1#issuecomment-5586806411).
Recommendation: do not deploy this candidate. The review identified concrete build and output-control defects.

## 1. Critical: incompatible SDK/application structure layouts

Location: [tools/build_glsd301p_ed_tc32.sh:122](https://github.com/analienx/gledopto/blob/eb963bd40af680119b61dbbb480e8441ec577b03/tools/build_glsd301p_ed_tc32.sh#L122).

The build adds `-fpack-struct` only to SDK source files. Application files pass `sdk_source=0`. Both sides exchange SDK structures, including the target's `g_cluster_list` with SDK `zcl_register()`. Using the hash-pinned TC32 compiler, `zcl_specClusterInfo_t` has these layouts:

| Property | Application | SDK |
|---|---:|---:|
| Entry size | 20 bytes | 18 bytes |
| Attribute-table pointer offset | 8 | 6 |
| Registration-function pointer offset | 12 | 10 |
| Application-callback pointer offset | 16 | 14 |

The SDK therefore reads pointers and advances through entries incorrectly. This can prevent Zigbee initialization and OTA recovery from becoming reachable. A successful link does not validate this interface.

Correction: use a consistent ABI across every translation unit exchanging these structures, including the runtime core when its types are shared with the target. Add a pinned-compiler structure-size/offset gate on both sides, then rebuild and re-attest all bytes. Do not fix only one structure or change packing only in the target without checking its core interfaces.

## 2. High: physical PUSH OFF is undone by an active Level transition

Location: [glsd301p_telink_target.c:478](https://github.com/analienx/gledopto/blob/eb963bd40af680119b61dbbb480e8441ec577b03/firmware/glsd301p-ed/glsd301p_telink_target.c#L478).

The physical-input callback applies PUSH state but does not cancel the current Level transition. A subsequent transition tick with On/Off semantics sets output true again. The guard accepts it because the runtime remains armed.

An offline check using unchanged target function bodies and the real runtime core produced:

```text
PUSH release: on=0, wire=0
Next transition tick: on=1, wire=110
```

Correction: cancel superseded Level transitions when a physical PUSH action takes control, including local OFF. Test both target transitions and continuous moves, and verify local dimming also supersedes old remote activity.

## 3. High: reserved input level becomes full brightness before validation

Location: [glsd301p_telink_target.c:335](https://github.com/analienx/gledopto/blob/eb963bd40af680119b61dbbb480e8441ec577b03/firmware/glsd301p-ed/glsd301p_telink_target.c#L335).

The target clamps an incoming `0xFF` Level target to `0xFE` before calling the output guard. Thus the guard never sees the invalid value it was designed to reject. The pinned SDK Level dispatcher passes the value to the callback without rejecting it.

The offline target check produced `on=1, wire=254`. Correction: validate incoming command values before normalization and return an error without energizing output; preserve the existing fail-OFF policy for invalid runtime state. Add a callback-level test so clamping cannot bypass this gate again.

## 4. High: blocking UART breaks the assumed 1 ms input cadence

Location: [glsd301p_telink_target.c:478](https://github.com/analienx/gledopto/blob/eb963bd40af680119b61dbbb480e8441ec577b03/firmware/glsd301p-ed/glsd301p_telink_target.c#L478) and [pinned SDK drv_uart.c:231](https://github.com/telink-semi/telink_zigbee_sdk/blob/d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e/tl_zigbee_sdk/proj/drivers/drv_uart.c#L231).

Once PB4 qualifies high, each input poll immediately sends a six-byte UART frame. At 9600 baud, 8N1, that requires 6.25 ms of wire time. The driver waits synchronously for the previous transfer before starting another. Sustained PB4 therefore prevents the combined PC2/PB4 callback from maintaining its nominal 1 ms sampling interval. PC2 decoding counts samples as milliseconds; it can miss pulse patterns or substantially alter PUSH timing. The driver's waits also have no deadline.

Correction: separate input sampling from UART transmission, use bounded nonblocking transport, and define explicit OFF priority and pending-output behavior. Validate PC2 pulse decoding while PB4 remains high and while UART is busy. This is a source/timing finding; no physical timing measurement was performed.

## Validation and limits

- Confirmed PR head remained the pinned SHA at the end of review.
- Both required existing GitHub runs were SUCCESS for that SHA: [host/boundary](https://github.com/analienx/gledopto/actions/runs/34235608521), [final readiness](https://github.com/analienx/gledopto/actions/runs/34235608484).
- Re-ran all five existing host C suites: PASS.
- Added an isolated target-interaction regression harness: two expected safety failures reproduced. GPIO, UART and timer APIs are simulated; target function bodies and runtime-core sources are unchanged.
- TC32 archive SHA-256 verified against `33b854be3e3db3dba4b4dacdda2cd4ea1c94dfd4d562864a095956de7991b430`.
- Reviewed the output guards, target callbacks, build flags, OTA preflight/release tooling, CI configuration, and relevant pinned public SDK code. No vendor firmware or vendor disassembly was used in this review.
- No production firmware, device settings, GitHub comments, branches, or PRs were changed. Review files were added only under this isolated local directory.
- No fresh complete firmware image was rebuilt; no live tuple, mains behavior, rejoin, OTA transfer, or wireless recovery was tested. The repository-wide clean-room checker was not rerun against this partial review snapshot; its existing CI result was checked.
- No Gledopto checkout was found in the workspace directory listing. Review used a source snapshot of the pinned GitHub revision; uncommitted work in another checkout was not examined.

Additional maintenance: root guidance, device README/status, and PR description contain superseded readiness claims. Reconcile these after fixing the candidate so future reviewers do not confuse historical NO_GO/partial-protocol statements with current evidence.

## Local evidence

- `snapshot.json`: pinned source content; `source-git-blobs.json`: expected Git blob hashes.
- `run_review.py`, `harness-prefix.c`, `harness-main.c`: reproducible host checks.
- `target-safety-regression.c`: generated unchanged target-function harness.
- `test-results.json`: five passing suites and two failing safety checks.
- `abi_type_probe.c`, `check_abi.py`, `application-layout.s`, `sdk-layout.s`: minimal pinned-compiler layout evidence.
- `check_real_abi.py`, `real-header-probe.c`: full SDK-header/layout and actual target/SDK translation-unit compilation.

## Final evidence verification

The full pinned SDK headers reproduced the same application/SDK layout mismatch (20/18-byte entries; registration callback at offset 12/10). Both the actual target translation unit and SDK `zcl.c` compiled successfully to assembly with their respective current production flags. See `real-abi-results.json`, `target-exact.s`, and `zcl-exact.s`. This confirms the defect is present in the real include/configuration context, not just the minimal type probe.

All snapshotted project source files listed in `source-git-blobs.json` matched their GitHub blob hashes and the local extracted copies matched those same bytes. See `integrity-results.json`. No firmware implementation changes were made; fixes and fresh same-head CI remain required before reconsidering deployment.