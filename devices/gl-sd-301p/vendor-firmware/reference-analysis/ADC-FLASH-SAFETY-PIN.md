# GL-SD-301P ADC flash-safety pin — PB3 proof and hardening ledger

Status: **CONFIRMED from same-model vendor firmware semantics; production target locked**

This note records the reproducible evidence for the TLSR8258 ADC pin used by Telink flash safe-voltage handling and the safeguards that prevent the independently written target from drifting away from that result.

It is a reference/evidence document. It does not authorize deployment, OTA serving, or production-device mutation.

## Reference image

Genuine same-model vendor OTA:

```text
GL-SD-301P_V20851203_20251230.ota
SHA-256 16595a38ab9783d3afc4eb58ab4ec32625249bd569c1fa6dbaf468bddc76dd72
model GL-SD-301P
vendor build 20851203
OTA fileVersion 0x28013001
manufacturerCode/imageType 0x124F/0x1416
```

The analysis is pinned to:

```text
Telink Zigbee SDK tag V3.7.2.0
SDK commit d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e
TC32 toolchain v2.0 SHA-256 33b854be3e3db3dba4b4dacdda2cd4ea1c94dfd4d562864a095956de7991b430
```

## Decisive semantic proof

The broad/fuzzy comparison experiments remain non-authoritative and are allowed to report `INCONCLUSIVE`. The decisive result comes from exact helper identification plus direct-call argument flow.

### 1. Exact helper anchor

Public Telink source for `drv_adc_mode_pin_set()` is compiled with the pinned TC32 toolchain and relocation-bearing bytes are excluded from the signature comparison.

The same-model vendor payload contains one unique match:

```text
helper              drv_adc_mode_pin_set
payload offset      73000
score               1.000000
best-score ties     1
unmasked bytes      18
```

`drv_adc_enable()` independently provides a second exact/unique nearby anchor:

```text
helper              drv_adc_enable
payload offset      73032
score               1.000000
best-score ties     1
unmasked bytes      20
```

### 2. One direct vendor caller

TC32 linked calls disassemble as `tjl`. Exactly one direct vendor call targets the exact `drv_adc_mode_pin_set` anchor:

```text
caller offset       73894
direct caller count 1
```

### 3. Compiler-derived ABI

Synthetic wrappers are compiled for the public TLSR8258 candidate ADC GPIOs. The analyzer learns the call ABI from those compiler-generated wrappers rather than hard-coding register assumptions.

Current pinned compiler result:

```text
mode argument register   r0
pin argument register    r1
VBAT mode value          1
r0 wrapper coverage      10/10
r1 wrapper coverage       9/10
```

The 9/10 pin-register coverage is intentional: TC32 emits a shorter special constant sequence for one candidate (`GPIO_PB0`). The hard gate therefore requires broad unique variation instead of assuming identical code generation for every constant.

### 4. Vendor argument flow

Symbolic evaluation of only the final basic block before the unique vendor call yields:

```text
mode value = 1
pin value  = 264 decimal = 0x0108
```

The compiler-generated GPIO map resolves `0x0108` uniquely to:

```text
GPIO_PB3
```

Therefore the same-model vendor firmware performs the semantic equivalent of:

```c
drv_adc_mode_pin_set(DRV_ADC_VBAT_MODE, GPIO_PB3);
```

This rejects the earlier `GPIO_PC5` development-board fixture hypothesis. PC5 was public Telink board precedent, not GL-SD-301P evidence.

## Evidence-strength boundary

The target currently uses these GPIO roles:

| Role | GPIO | Encoded | Evidence state |
|---|---|---:|---|
| flash safe-voltage ADC/VBAT input | PB3 | `0x0108` | **CONFIRMED — same-model semantic argument-flow proof above** |
| power-stage UART TX | PB1 | `0x0102` | HIGH — same-model UART analysis + public TLSR8258 pin capability |
| power-stage UART RX | PA0 | `0x0001` | HIGH — same-model UART analysis + public TLSR8258 pin capability; RX not required for core output |
| external PUSH sense | PC2 | `0x0204` | HIGH / functionally solved from same-model stock behavior |
| auxiliary compatibility input | PB4 | `0x0110` | pin/behavior CONFIRMED; physical/business meaning remains `UNKNOWN` |

These confidence labels are intentionally not interchangeable. In particular, PB4 must not be renamed thermal/overload/zero-cross without new evidence, and PB1/PA0 must not be described as PCB-continuity-proven merely because their software assignment is strong.

Detailed functional evidence remains in:

```text
devices/gl-sd-301p/interoperability/INTERFACE.md
devices/gl-sd-301p/interoperability/PUSH_INPUT.md
devices/gl-sd-301p/interoperability/PB4_AUX_INPUT.md
devices/gl-sd-301p/vendor-firmware/reference-analysis/POWER_STAGE_20260907.md
```

## Permanent hardening layers

The independently written target is now protected by multiple independent gates.

### Target configuration

`firmware/glsd301p-ed/app_cfg.h` fixes:

```c
#define VOLTAGE_DETECT_ADC_PIN GPIO_PB3
```

The obsolete PC5 fixture is forbidden by CI.

### Numeric architecture contract

`firmware/glsd301p-ed/glsd301p_target_contract.h` records stable encoded values for PB3, PB1, PA0, PC2 and PB4 and host tests assert that PB3 is disjoint from all application I/O roles.

### Post-SDK enum contract

`firmware/glsd301p-ed/glsd301p_telink_pin_contract.h` is compiled only after Telink GPIO definitions exist. In the real target it requires:

- `VOLTAGE_DETECT_ADC_PIN == GPIO_PB3`;
- Telink's actual GPIO enum encodings equal the independently recorded contract values;
- PB3 does not collide with PB1, PA0, PC2 or PB4.

The pinned TC32 compiler predates C11, so these are implemented as negative-size typedef assertions rather than `_Static_assert`.

### Reference/production isolation

Reference-only candidate wrappers need to compile alternate GPIO constants. Their temporary copied configuration explicitly changes `GLSD301P_TARGET_ENABLE_SDK_PIN_ASSERTS` from `1` to `0` and fails if that controlled substitution cannot be performed exactly once.

The production target keeps the switch at `1`. Reference analysis therefore cannot weaken the actual target pin contract.

### Source-role firewall

`tests/test_glsd301p_target_contract.sh` verifies the implementation-facing roles as well as numeric constants:

- UART remains PB1 TX / PA0 RX;
- PC2 remains an input used by the PUSH polling path;
- PB4 remains an input with the stock 100 kΩ pulldown and auxiliary polling path;
- application GPIO code does not reuse PB3;
- PC5 cannot return as the ADC safety pin.

### Same-model semantic re-derivation

CI runs `analyze_adc_flash_guard_argflow.py` against the genuine vendor OTA and hard-fails unless it independently obtains:

```text
confidence_gate = PASS
derived_pin      = GPIO_PB3
vendor mode      = compiler-derived VBAT mode
```

The implementation does not trust a checked-in textual declaration of PB3 by itself.

## Last validated quarantined target baseline

The first full post-switch convergence run was GitHub Actions run `34221096696`. It passed both the host firewall and pinned real-TC32 lane, including the semantic PB3 gate and final link/finalization.

Relevant target facts from that run:

```text
ADC_FLASH_SAFETY_PIN=GPIO_PB3_VENDOR_FIRMWARE_CONFIRMED
STACK_ARCHIVE=libzb_ed.a
ROUTER_ARCHIVE_LINKED=NO
ZB_ED_ROLE=1
ZB_ROUTER_ROLE=0
ZB_MAC_RX_ON_WHEN_IDLE=1
PM_ENABLE=0
ENDPOINT=11
POWER_SOURCE=MAINS
RAW_BINARY_SIZE=129780
FINAL_BINARY_SIZE=129796
TEXT_VMA=0x00001560
PHYSICAL_BANK_B_END_EXCLUSIVE=0x5fb04
TELINK_XCRC32=0x8aa521a3
```

Reproducibility hashes from that run:

```text
ELF       af46fec724093d86327f55c7ad2529612cffeb2aafead6d7ff818cf8ae9d6e83
raw bin   2a3e410def2d3ac2167114abc4473a2626b93ed6086857cdd40b3f7f7b4f0508
final bin c7d5ab565eec0f544d7d7fff77883c7894125c3615800ac7bc641f3fd1cec624
map       e4624de7b17054e75857af24dcc0e429ebb0beb3fe0366369a6a6b3dcf1314d1
```

These are reproducibility evidence only. CI does not upload the binary.

## Deployment state

The pin-discovery blocker is closed for the ADC flash-safety input, but that does **not** convert the current target into an authorized deployment artifact.

```text
PB3_ADC_FLASH_SAFETY_PIN       = CONFIRMED
PIN_CONTRACT_HARDENED          = YES
QUARANTINED_BUILD              = YES
DEPLOYABLE                     = NO
FIRST_FLASHABLE_CANARY_ALLOWED = NO
CI_ARTIFACT_UPLOAD             = NONE
LIVE_DEVICE_MUTATION           = NO_GO
LIVE_CUSTOM_OTA                = NO_GO
```

Any future deployment decision is a separate gate and must not be inferred from this hardware-interface proof.
