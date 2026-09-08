#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/tests/test_glsd301p_target_contract.c"
INC="$ROOT/firmware/glsd301p-ed"
CFG="$INC/app_cfg.h"
TARGET_SRC="$INC/glsd301p_telink_target.c"
CC="${CC:-cc}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

base_defs=(
  -DMCU_CORE_8258=1
  -DEND_DEVICE=1
  -DROUTER=0
  -DCOORDINATOR=0
  -DZB_ED_ROLE=1
  -DZB_ROUTER_ROLE=0
  -DZB_COORDINATOR_ROLE=0
  -DPM_ENABLE=0
  -DZB_MAC_RX_ON_WHEN_IDLE=1
  -DZCL_ON_OFF_SUPPORT=1
  -DZCL_LEVEL_CTRL_SUPPORT=1
  -DZCL_GROUP_SUPPORT=1
  -DAPS_GROUP_TABLE_NUM=8
  -DGLSD301P_ENDPOINT=0x0B
)

compile_ok() {
  "$CC" -std=c11 -Wall -Wextra -Werror -pedantic -I"$INC" \
    "${base_defs[@]}" "$SRC" -o "$TMP/ok"
  "$TMP/ok"
}

expect_fail() {
  local name="$1"
  shift
  local filtered=("${base_defs[@]}")

  # Replace every overridden -D key rather than stacking duplicate definitions.
  # A negative case therefore passes only when our contract itself rejects it.
  for override in "$@"; do
    local key="${override%%=*}"
    local next=()
    for item in "${filtered[@]}"; do
      if [[ "$item" != "$key="* ]]; then
        next+=("$item")
      fi
    done
    filtered=("${next[@]}")
  done
  filtered+=("$@")

  if "$CC" -std=c11 -Wall -Wextra -Werror -pedantic -I"$INC" \
      "${filtered[@]}" "$SRC" -o "$TMP/$name" >"$TMP/$name.log" 2>&1; then
    echo "ERROR: architecture firewall accepted forbidden case: $name" >&2
    exit 1
  fi
  if ! grep -q 'GL-SD-301P' "$TMP/$name.log"; then
    echo "ERROR: forbidden case $name failed for a non-contract reason" >&2
    cat "$TMP/$name.log" >&2
    exit 1
  fi
  echo "TARGET_CONTRACT_REJECT_${name}=PASS"
}

require_source() {
  local pattern="$1" label="$2"
  grep -Eq "$pattern" "$TARGET_SRC" || {
    echo "ERROR: target source pin role missing/drifted: $label" >&2
    exit 1
  }
}

# The actual target configuration is part of the contract, not just the numeric
# host fixture. This blocks a future SDK-board-example regression back to PC5.
grep -Eq '^#define[[:space:]]+VOLTAGE_DETECT_ADC_PIN[[:space:]]+GPIO_PB3[[:space:]]*$' "$CFG" || {
  echo 'ERROR: GL-SD-301P ADC flash-safety pin must remain GPIO_PB3' >&2
  exit 1
}
if grep -Eq '^#define[[:space:]]+VOLTAGE_DETECT_ADC_PIN[[:space:]]+GPIO_PC5([[:space:]]|$)' "$CFG"; then
  echo 'ERROR: obsolete PC5 ADC fixture returned to production target' >&2
  exit 1
fi

# The real target must never be allowed to opt out of the post-SDK enum and
# collision assertions. Only temporary reference-analysis copies may flip this
# switch to zero while compiling alternate GPIO candidates.
grep -Eq '^#define[[:space:]]+GLSD301P_TARGET_ENABLE_SDK_PIN_ASSERTS[[:space:]]+1[[:space:]]*$' "$CFG" || {
  echo 'ERROR: production post-SDK pin assertions must remain enabled' >&2
  exit 1
}
if grep -Eq '^#define[[:space:]]+GLSD301P_TARGET_ENABLE_SDK_PIN_ASSERTS[[:space:]]+0([[:space:]]|$)' "$CFG"; then
  echo 'ERROR: reference-only pin-assertion bypass leaked into production config' >&2
  exit 1
fi

# Lock implementation pin roles separately from evidence confidence. PB3 is
# vendor-firmware-confirmed by the semantic reference gate. PB1/PA0 are HIGH,
# PC2 is HIGH/functional, and PB4's pin/behavior is confirmed while its physical
# business meaning intentionally remains unknown.
require_source 'drv_uart_pin_set\(UART_TX_PB1,[[:space:]]*UART_RX_PA0\)' 'UART PB1 TX / PA0 RX'
require_source 'drv_gpio_read\(GPIO_PC2\)' 'PC2 PUSH read'
require_source 'drv_gpio_func_set\(GPIO_PC2\)' 'PC2 PUSH GPIO mode'
require_source 'drv_gpio_output_en\(GPIO_PC2,[[:space:]]*false\)' 'PC2 PUSH output disabled'
require_source 'drv_gpio_input_en\(GPIO_PC2,[[:space:]]*true\)' 'PC2 PUSH input enabled'
require_source 'drv_gpio_read\(GPIO_PB4\)' 'PB4 auxiliary read'
require_source 'drv_gpio_func_set\(GPIO_PB4\)' 'PB4 auxiliary GPIO mode'
require_source 'drv_gpio_output_en\(GPIO_PB4,[[:space:]]*false\)' 'PB4 auxiliary output disabled'
require_source 'drv_gpio_input_en\(GPIO_PB4,[[:space:]]*true\)' 'PB4 auxiliary input enabled'
require_source 'drv_gpio_up_down_resistor\(GPIO_PB4,[[:space:]]*PM_PIN_PULLDOWN_100K\)' 'PB4 100k pulldown'

# Safety-sweep regressions: physical PUSH owns the local control plane, reserved
# Level 0xFF is rejected before normalization, and runtime UART is nonblocking.
require_source 'glsd301p_runtime_core_poll_push_ex\(' 'PUSH takeover-aware runtime API'
require_source 'if[[:space:]]*\(push_took_control\)[[:space:]]*\{' 'PUSH takeover branch'
require_source 'glsd_cancel_level_transition\(\);' 'PUSH cancels remote level transition'
require_source 'cmd->level[[:space:]]*==[[:space:]]*GLSD301P_ZCL_LEVEL_UNKNOWN' 'reserved Level 0xFF rejection'
require_source 'uart_tx_is_busy\(\)' 'nonblocking UART busy probe'
require_source 'uart_dma_send\(g_uart_tx_dma\)' 'nonblocking UART DMA start'

[[ "$(grep -c 'drv_uart_tx_start' "$TARGET_SRC")" -eq 1 ]] || {
  echo 'ERROR: blocking UART wrapper must remain boot-OFF-only' >&2
  exit 1
}
if grep -Eq 'while[[:space:]]*\([^)]*(uart|UART)' "$TARGET_SRC"; then
  echo 'ERROR: runtime UART polling loop reintroduced' >&2
  exit 1
fi
echo 'TARGET_PUSH_TAKEOVER_CANCELS_REMOTE_LEVEL=PASS'
echo 'TARGET_RESERVED_LEVEL_FF_REJECTED=PASS'
echo 'TARGET_UART_RUNTIME_NONBLOCKING=PASS'

if grep -q 'GPIO_PB3' "$TARGET_SRC"; then
  echo 'ERROR: PB3 is reserved for ADC flash-safety handling and must not be reused by application GPIO code' >&2
  exit 1
fi

echo 'TARGET_ADC_FLASH_SAFETY_PIN_GPIO_PB3=PASS'
echo 'TARGET_PRODUCTION_SDK_PIN_ASSERTIONS=ENABLED'
echo 'TARGET_UART_PIN_ROLE_PB1_PA0=PASS'
echo 'TARGET_PUSH_PIN_ROLE_PC2=PASS'
echo 'TARGET_AUX_PIN_ROLE_PB4=PASS'
echo 'TARGET_ADC_PB3_APPLICATION_REUSE=NONE'

compile_ok
echo 'TARGET_CONTRACT_VALID_END_DEVICE=PASS'

expect_fail ROUTER -DROUTER=1 -DZB_ROUTER_ROLE=1
expect_fail COORDINATOR -DCOORDINATOR=1 -DZB_COORDINATOR_ROLE=1
expect_fail NOT_END_DEVICE -DEND_DEVICE=0 -DZB_ED_ROLE=0
expect_fail PM_ENABLED -DPM_ENABLE=1
expect_fail SLEEPY_RX -DZB_MAC_RX_ON_WHEN_IDLE=0
expect_fail NO_GROUP_RX -DZCL_GROUP_SUPPORT=0
expect_fail NO_ONOFF -DZCL_ON_OFF_SUPPORT=0
expect_fail NO_LEVEL -DZCL_LEVEL_CTRL_SUPPORT=0
expect_fail WRONG_ENDPOINT -DGLSD301P_ENDPOINT=1
expect_fail WRONG_MCU -DMCU_CORE_8258=0

echo 'GLSD301P_TARGET_ARCHITECTURE_FIREWALL=PASS'
