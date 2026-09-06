# Batch 6 exact-head verification — 2026-09-05

Frozen head: `26faaaa03b64cdd2b5bdd3a98fd0abf41554e80a` (worktree detached, clean, no branch).
Remote advanced during verification to `f38a44103cddd880bd482c52fa40a369771b76f9` — NOT tested (per instruction).
CI run 34015548874 = SUCCESS on 26faaaa (head_sha matches); PR merge SHA 8a680a6 is what GH Actions checks out — this verification tested the branch head itself.

## 1. Freeze/cleanliness

```text
git rev-parse HEAD          = 26faaaa03b64cdd2b5bdd3a98fd0abf41554e80a
git branch --show-current   = (empty; detached)
git status --short          = clean (plus repo-local core.autocrlf=false to fix CRLF checkout
                              of tracked .sh scripts — a checkout artifact, not an implementation edit)
NEWER_REMOTE_SHA            = f38a44103cddd880bd482c52fa40a369771b76f9 (reported, not tested)
```

## 2. Host/Python/node regression (Windows Python 3.14.4, node v24.18.0)

```text
test_wireless_dump.py          12 tests OK (skipped=2 — GCC-only, executed under WSL below)
test_wireless_dump_guard.py    11 tests OK
test_transport_adapter.py       1 test OK (skipped=1 — native, executed under WSL below)
test_z2m_dump_integration.py    3 tests OK
test_glsd_stager_index.py       3 tests OK
test_telink_app_finalize.py    11 tests OK
node --check extension.mjs      OK
node test_glsd_wireless_dump_contract.mjs   glsd_wireless_dump_contract: PASS
WSL (gcc 15.2.0, python3 3.14.4): all six suites OK, NO skips (native C/cross-language executed):
12 OK / 11 OK / 1 OK / 3 OK / 3 OK / 11 OK
```

## 3. TC32/TLSR8258 proof on the exact head (isolated WSL)

```text
toolchain  = tc32_gcc_v2.0.tar.bz2, sha256 33b854be…b430 MATCHED (official Telink OSS)
SDK        = telink_zigbee_sdk tag V3.7.2.0 = commit d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e
             (verified via GitHub tag ref — identical to CI's resolved commit)
bash tools/build_glsd_tc32_objects.sh     → GLSD_TC32_OBJECT_COMPILE = PASS_6_OF_6
bash tools/build_glsd_tc32_link_probe.sh  → GLSD_TC32_FULL_LINK_PROBE = PASS_BOTH_BANKS
```

Bank A manifest (independently confirmed vs expected): base 0x00000, raw 156692, final 156708,
end-excl 0x26424, slot end 0x34000, .text VMA 0x00001670, xcrc32 0x9eb539eb,
TELINK_PREAMBLE PASS, TELINK_XCRC32 PASS.
Bank B: base 0x40000, end-excl 0x66424, slot end 0x74000, .text VMA 0x00041670, xcrc32 0x1cd95e73 — PASS.
BANK_TEXT_VMA_DELTA = 0x40000. RESERVED_FLASH_GEOMETRY = PASS.

**Final-image reproducibility (sha256 vs CI):**
bank A final.bin `0e0db4b38e37aaf7d45ba3614311b575aba6bf06f67480470f8f6529b59fa52b` — **MATCH**
bank B final.bin `b55d671bd3a53bce6d6abe8c2303b33b1f5543497cdbf78d8441616c66ebe9e4` — **MATCH**
(no binaries committed; DEPLOYABLE=NO respected)

## 4. Independent linker/safety audit (executor's own nm/map, not the script's PASS string)

```text
FINAL_OTP_SYMBOL_SCAN                   = NONE (confirmed: no *_otp symbols in final nm output;
                                          prelink refs to flash_erase_otp/read_otp/write_otp exist only
                                          in vendor wrapper objects flash_mid011460c8/136085/1360eb and
                                          are section-GC discarded)
PRIVATE_EXTRACTION_MUTATION_IMPORT_SCAN = NONE (glsd_* objects import no flash-write/erase/NV symbols)
APPLICATION_POWER_STAGE_AND_RESET_SCAN  = NONE (no PWM/power-stage/reset helpers in application objects)
```

**Executor caveat (independent finding, reported for accuracy):** the script's scan names are narrower
than they may read. An independent `tc32-elf-nm` of BOTH final ELFs still shows defined symbols
`flash_write`, `flash_writeWithCheck`, `flash_write_page`, `flash_erase`, `flash_erase_sector`,
`flash_write_status_mid*`, `nv_write_item`, `nv_resetToFactoryNew`, `touchLinkKeyMaster`,
`zdoTouchLinkCb`, `zcl_identify_identifyQuery` (111 flash_mid map references retained). These are
retained because `flash_read` pulls `flash_common.c`, which references every vendor mid's
lock/unlock helpers, and because the Zigbee router stack legitimately uses NV writes. They are NOT
reachable from the private extraction protocol (glsd_* code calls none of them), and their presence
is inherent to any router-stack build — but "NONE" must be read as scoped to the three named scans
above, not as "the binary contains no mutation-capable symbols".

## 5. Disabled-feature closure

- Identify: real Telink ZCL identify implementation retained (BDB requirement) — intentional; `zcl_identify_identifyQuery` present.
- Touchlink: `glsd_telink_disabled_feature_glue.c` provides inert no-op hooks (`touchlink_keyModeSet`, `touchlink_lqiThresholdSet`, `zcl_touchlink_register` stub) satisfying libzb_router; no commissioning behavior reachable from the stager app.
- Green Power: `zclGpAttr_gpSharedSecKey[SEC_KEY_LEN] = {0}` (inert/zero), `deviceInfoRsp = 0` (inert hook); GP announce callback rejects.
- No unexpected commissioning/steering/GP/reset/mutation path found reachable from the application (see caveat in §4 re: stack-inherent NV symbols).

## 6. Raw-to-final Telink image mechanics

Link probe output: raw image `rawMagic=0000` (transient 00 00 state at +0x06 confirmed), finalizer
produces `magic=5d02` preamble, bytes 156692 → 156708 (declared size = finalized length), xcrc32
recomputed (`0x9eb539eb` A / `0x1cd95e73` B) with the verified Telink convention.
`test_telink_app_finalize.py`: 11 tests OK (malformed/truncated/already-invalid inputs fail closed).
No Zigbee OTA container produced (MECHANICS_ONLY=YES, DEPLOYABLE=NO).

## 7. OTA recovery behavior (source audit)

`glsd_telink_stager_app.c:116`: "We intentionally never call ota_queryStart()" — grep of the whole
stager tree finds no `ota_queryStart`/periodic-query call; OTA client is initialized via
`ota_init(OTA_TYPE_CLIENT, …)` and reacts only to `ota_imageNotifyHandler()` (Image-Notify → Query
Next Image → download → `ota_mcuReboot()` on completion event). The private extraction protocol
stays separate and read-only.

