#!/usr/bin/env bash
set -euo pipefail

: "${TELINK_SDK_ROOT:?set TELINK_SDK_ROOT to tl_zigbee_sdk}"
: "${TC32_CC:?set TC32_CC to tc32-elf-gcc}"
SDK="$(cd "$TELINK_SDK_ROOT" && pwd)"
NM="$(dirname "$TC32_CC")/tc32-elf-nm"

symbols=(
  flash_erase_otp
  flash_read_otp
  flash_write_otp
  ss_apsmeSwitchKeyReq
  ss_apsmeTransportKeyReq
  tl_zbNwkBeaconPayloadUpdate
  touchlink_keyModeSet
  touchlink_lqiThresholdSet
  zcl_touchlink_register
)

archives=(
  "ED:$SDK/zigbee/lib/tc32/libzb_ed.a"
  "ROUTER:$SDK/zigbee/lib/tc32/libzb_router.a"
  "COORDINATOR:$SDK/zigbee/lib/tc32/libzb_coordinator.a"
  "DRIVER:$SDK/platform/lib/libdrivers_8258.a"
)

for entry in "${archives[@]}"; do
  role="${entry%%:*}"
  archive="${entry#*:}"
  test -f "$archive"
  echo "=== ROLE_ARCHIVE=$role FILE=$(basename "$archive") ==="
  for sym in "${symbols[@]}"; do
    hits="$($NM -A "$archive" 2>/dev/null | awk -v s="$sym" '$NF == s {print}')"
    if [[ -n "$hits" ]]; then
      while IFS= read -r hit; do
        printf '%s SYMBOL=%s %s\n' "$role" "$sym" "$hit"
      done <<< "$hits"
    else
      printf '%s SYMBOL=%s ABSENT\n' "$role" "$sym"
    fi
  done
done

echo TELINK_ROLE_SYMBOL_PROBE=PASS
