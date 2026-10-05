# Independent review — 7975ff0, next phase

Reviewed head: `7975ff05a929b64d026268097a182f5877325536`.
Scope: all 20 files changed since `69831aa`, actual target/shared adapters,
foundation patch definitions against the read-only pinned SDK, hosted harnesses,
build gates and raw final Actions logs. Local work was inspection only; no local
firmware test/build/lint was run. Acceptance remains pending.

## Findings

### R24 — P2 — Foundation status-dependent response grammar remains incomplete

Locations: `tools/apply_glsd301p_sdk_patches.py:967` and `:989`,
`tests/test_glsd301p_zcl_dispatch.c:3941` and `:3519`; pinned SDK
`zigbee/zcl/zcl.c`, `zcl_writeUndividedHandler`, `zcl_writeRsp`.

The Write Response guard accepts every 3-byte multiple without inspecting
statuses. The Configure Reporting Response guard checks directions but explicitly
says status bytes are unrestricted. Consequently `00 00 00` as Write Response
and `00 00 00 00` as Configure Reporting Response reach the parser/hook as
successful long records. The new `cfgrsp_ok` fixture blesses that second payload.
Both commands permit success only as a single status byte; long records describe
failures. Mixed long streams containing a success record must also be rejected.

Read Reporting Configuration Response rejects reserved directions only in its
success branch. Failure record `86 02 00 00` passes the four-byte failure path
despite its reserved direction. Direction validation belongs before the status
branch and applies to each record.

There is also a real outbound problem in the newly exercised Undivided path:
the SDK pre-validation stores one response entry for every attempted record.
When any record fails, the response still contains SUCCESS entries for records
that were not applied. The new mixed-write test explicitly expects six bytes
and SUCCESS at byte 3. With one unknown attribute followed by valid IdentifyTime,
no value changes (correct), but the generated wire response claims a successful
record (incorrect). Return only failing records when the atomic write is refused;
all-success remains the one-byte success response. Preserve accepted-write
observation and never restart Identify on a refused Undivided operation.

Primary contract: ZCL r6 §§2.5.4, 2.5.5.1.2 and 2.5.8.1.3: Undivided keeps normal
Write Response formatting; successful per-attribute entries are omitted; whole
success uses one status byte. §§2.5.8.1.2/2.5.10 define only directions 0 and 1.
[CSA specification](https://csa-iot.org/wp-content/uploads/2019/12/07-5123-06-zigbee-cluster-library-specification.pdf).

Fix and oracle: real SDK root/handler dispatch, new whole-stream negative
controls and valid failure-only/short-success controls, hook counts, exact AF
bytes, Identify store/phase, reporting/NV side effects and pool return. Replace
the two defective fixture expectations. This is grammar/status correctness;
no new candidate out-of-bounds write is claimed.

### R25 — P2 — Bounded-work regression is disconnected from the fixed code

Locations: `src/glsd301p_identify.c:21`, `:83`, `:92`,
`src/glsd301p_identify.h` tick_steps_max contract,
`tests/test_glsd301p_zcl_dispatch.c:3546`.

The O(1) implementation itself is correct by inspection. But the advertised
tripwire is initialized to zero and never updated. Tests assert it remains at
most two. Reintroducing an elapsed-second loop without explicitly writing this
counter would still pass. The old negative control manually instrumented the
old implementation; it does not prove that the green oracle detects work in
the current implementation or a regression of it.

Fix and oracle: replace this self-reported constant with a hosted observation
that detects input-dependent catch-up work in the actual shared production
tick. Prove a deliberate slow-loop mutant fails the same work-bound assertion
without voluntarily updating a diagnostic counter. Preserve zero/max countdown,
large gaps, residual phase, wrap, saturation, UART/input service and output
neutrality. Use a deterministic bounded observation; a CI hang or elapsed-time
threshold alone is insufficient. Remove the unused production field if it is
no longer needed. This finding is an acceptance-evidence defect, not a claim
that the current arithmetic still loops.

### R26 — P2 — Host allocation evidence misses target-specific boundaries

Locations: `tools/build_glsd301p_ed_tc32.sh:334`–pool assertions,
`tests/test_glsd301p_zcl_dispatch.c:4130`/`:4147`,
`.github/workflows/cleanroom-guard.yml:295`–dispatch compilation.

The new TC32 type/offset assertions are useful and pass. The host counterpart
documents unpacked layouts. However, assertions that maximum count 255 is below
65536 and above LARGE_BUFFER, plus a total-free-buffer check, do not exercise
record allocation boundary behavior. Claiming pool thresholds hold identically
on both layouts is misleading: write storage (excluding values) is `1+7*N`
on target versus `4+8*N` on host. At N=63, target requires 442 bytes while host
requires 508, which exceeds the 504-byte largest usable buffer. Target N=71
requires 498, N=72 requires 505. Reporting configuration storage is `1+14*N`
versus `4+16*N`; target N=32 requires 449 while host needs 516. Values, response
allocations, incoming buffers and pool availability must also be accounted for.

The ordinary exhaustion tests cover empty pools, not these count/size edges.
No claim of a present overflow is made: P5 already widens all three affected
SDK allocation lengths to u16 (`apply_glsd301p_sdk_patches.py:761`–`:769`).
The initial inspection of pristine u8 code was reconciled against these actual
patch replacements and is not a candidate finding. Preserve that existing fix.

Fix and oracle: hosted target-layout behavior coverage, using compiler-verified
TC32 facts and a compatible executable harness, plus exact allocation requests,
success/refusal and cleanup at actual size/pool boundaries. Verify both normal
and depleted-pool paths for affected Write/Report/Configure/Read Configuration
flows. Record the real host/target deltas; do not call them identical. Include
negative controls that detect length narrowing or layout mismatch. Public parser
storage is executable evidence; opaque stack and physical behavior stay limits.

## Bounded question Q1 — Level admission failure ordering

`src/glsd301p_control.c:338`/`:342` and `:375`/`:380` apply upward ON before
timer registration and discard the apply result. If registration fails, they
return failure after ON has been mirrored/queued; cancellation does not undo
the output. An apply failure can similarly be hidden by later timer success.
Normal reviewed readiness/fault cases pass and transport offers normally
succeed; no live occurrence or ordinary external trigger is claimed here.

Inspect the owned timer's actual failure contract. Use a narrowly controlled
hosted failure seam to exercise the existing production branch if warranted.
If confirmed within this hardening contract, make admission transactional:
successful registration and guarded emission must precede command success;
refusal must leave no newly energizing frame or active transition. Preserve
OFF priority and avoid rollback that can restore ON after a safety fault.
Otherwise document precise reachability/invariant evidence and defer the
question explicitly. Do not silently turn this question into a proven exploit.

## Prior requirement closure

| Requirement | Review result |
|---|---|
| R17 output-neutral Identify | Overlay/saved restore/preemption deleted; rejection and ordinary commands covered. |
| R18 write receipt phase | Shared accepted-write observer and equal-value/phase/no-response/Undivided controls implemented. Outbound mixed response remains R24. |
| R19 elapsed countdown | O(1) implementation closed by source inspection; regression oracle remains R25. |
| R20 Move duration / clipped Step | Separate timing policies implemented and hosted cases pass. |
| R21 upward onset | Normal admission implemented and covered before time advance; Q1 failure ordering needs bounded disposition. |
| R22 datatype/direction/status grammar | Typed/count/nesting and normal directions fixed; response grammar remains R24. |
| R23 ABI/current ledger | Layout assertions and current checkpoint improved; target allocation coverage remains R26. |
| A24 provenance/reproducibility | Current exact-head evidence verified below; next code change needs a new final seal. |

Earlier AP/R1–R16 changes remain substantive. Do not restart the initial survey
or undo accepted source policy. Three confirmed P2 findings; no confirmed new P1.

## Independently inspected hosted evidence

All runs are successful and pinned to exact `7975ff0`:

- [Boundary](https://github.com/analienx/gledopto/actions/runs/37276712682):
  SEQ PASS, R9–R16 failed=0, R17–R23 failed=0, pristine SDK repros PASS.
- [Readiness](https://github.com/analienx/gledopto/actions/runs/37276712594)
  and [independent rebuild](https://github.com/analienx/gledopto/actions/runs/37276898596):
  TC32 ABI assertions pass; raw logs show matching ELF/raw/final/MAP SHA256 and
  SHA512 fields, matching OTA SHA256/SHA512 and build manifest SHA256.

Final BIN: `1c1315fd92a638bbb8fc42660cf8e9652410c7e43d359339a7b6e0214dd63440`.
OTA: `4fd72ef6556affff8d353c88ad76a0f3b86a5d9b7b18e288da59524ab7f3f578`.
Identity: GLSD-ED-004 / APP_BUILD05 / 0x7F050001 / 20261005.
SDK: `d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e`; P5v4 patched
`0c34d2622d50b104046f7cc69588408241ab165eb86758ab303105fe1d78cadf`.
Raw logs are private scratch inspection inputs, not material to commit.

## Continuation decision

Retain `01a10ab5-b6cb-75d2-8bd2-9c3deb8f1f52`: native goal complete/100%,
clean candidate checkout, final jobs complete, no outstanding writer/effect
reported. Transcript about 18.2 MB, status responds normally, below the updated
skill's 64 MiB operational band. Size is not active model context or billed cost.
This is a focused coupled continuation; send a new native objective/immutable
brief at the completed handoff. Do not create a competing writer or replay all
history. A later unsettled draft/ownership state must be preserved, not bypassed.

**Independent acceptance review pending.** Frozen PR #6 and quarantine unchanged;
no device, deployment, merge, release or OTA publication authority.
