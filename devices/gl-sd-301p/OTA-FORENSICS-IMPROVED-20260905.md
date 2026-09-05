# GL-SD-301P — Improved OTA Forensics and Wireless Extraction State

Date: 2026-09-05
Status: SUPERVISOR-IMPROVED / SINGLE-SOURCE UPDATE
Repository: `analienx/gledopto`
Control issue: `#1`

## 1. What this improvement fixes
- Corrects the 62-byte overhead interpretation (56B OTA header + 6B sub-element header).
- Separates container proof from acceptance policy.
- Pins the 512K flash map to `release/V3.7.1.2`.
- Makes rollback logic exact (`ota_mcuReboot` sequence).
- Confirms 48-byte chunk size from SDK source.

## 2. Updated verified facts
- Container verdict: VERIFIED_PLAIN_TELINK_OTA (for historical GL-C-009P).
- Cryptographic container verdict: NO_AES_WRAPPER_DETECTED, NO_TRAILING_SIGNATURE_BLOCK_DETECTED.
- GL-SD OTA acceptance verdict: UNKNOWN.
- Production OTA verdict: NO-GO.

## 3. Improved forensic parse logic
Parse order:
1. Zigbee OTA base header (usually 56 bytes).
2. Sub-element header (tag u16, length u32).
3. Telink application payload.

## 4. Gates
- G1: Artifact hash
- G2: Container validation
- G3: SDK source alignment
- G4: Target-specific firmware validation
- G5: Bootloader mode confirmation
- G6: Rollback proof
- G7: Sacrificial canary
- G8: Production canary (NO-GO until G1-G7 pass)
