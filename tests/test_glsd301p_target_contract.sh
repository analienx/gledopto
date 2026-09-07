#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/tests/test_glsd301p_target_contract.c"
INC="$ROOT/firmware/glsd301p-ed"
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
