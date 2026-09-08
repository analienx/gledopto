#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="$ROOT/firmware/glsd301p-ed"
CORE="$ROOT/src"
FINALIZER="$ROOT/tools/telink_app_finalize.py"
: "${TELINK_SDK_ROOT:?set TELINK_SDK_ROOT to tl_zigbee_sdk}"
SDK="$(cd "$TELINK_SDK_ROOT" && pwd)"
TC32_CC="${TC32_CC:-tc32-elf-gcc}"
BINDIR="$(dirname "$TC32_CC")"
TC32_LD="${TC32_LD:-$BINDIR/tc32-elf-ld}"
TC32_NM="${TC32_NM:-$BINDIR/tc32-elf-nm}"
TC32_SIZE="${TC32_SIZE:-$BINDIR/tc32-elf-size}"
TC32_OBJCOPY="${TC32_OBJCOPY:-$BINDIR/tc32-elf-objcopy}"
TC32_OBJDUMP="${TC32_OBJDUMP:-$BINDIR/tc32-elf-objdump}"
OUT_DIR="${OUT_DIR:-${TMPDIR:-/tmp}/glsd301p-ed-tc32}"
DIR="$OUT_DIR/target"
APP_SLOT_SIZE=0x34000
BANK_B_BASE=0x40000
BANK_B_SLOT_END=0x74000
MAC_REGION_START=0x76000
FILE_VERSION=0x7F020001

[[ -f "$SDK/platform/boot/8258/boot_8258.link" ]] || { echo 'ERROR: pinned TLSR8258 SDK fixture incomplete' >&2; exit 2; }
[[ -f "$SDK/zigbee/lib/tc32/libzb_ed.a" ]] || { echo 'ERROR: libzb_ed.a missing' >&2; exit 2; }
[[ -f "$SDK/zigbee/lib/tc32/libzb_router.a" ]] || { echo 'ERROR: SDK provenance check expects router archive to exist but never be linked' >&2; exit 2; }
[[ -f "$FINALIZER" ]] || { echo 'ERROR: Telink finalizer missing' >&2; exit 2; }

roots=("$SDK/proj" "$SDK/platform" "$SDK/zigbee" "$SDK/apps/common")
includes=(-I"$TARGET" -I"$CORE" -I"$SDK/proj")
while IFS= read -r -d '' d; do includes+=("-I$d"); done < <(find "${roots[@]}" -type d -print0 | sort -zu)

defs=(
  -DMCU_CORE_8258=1
  -DEND_DEVICE=1
  -DROUTER=0
  -DCOORDINATOR=0
  -DMCU_STARTUP_8258=1
)
telink_first=(-D_SIZE_T -D_SIZE_T_ -D__SIZE_T -D__SIZE_T__)
cflags=(-O2 -ffunction-sections -fdata-sections -fshort-enums -finline-small-functions -std=gnu99 -funsigned-char -fshort-wchar -fms-extensions -fpack-struct -nostartfiles -nostdlib)
asflags=(-fomit-frame-pointer -fshort-enums -fdata-sections -ffunction-sections)

sdk_sources=(
  platform/boot/8258/cstartup_8258.S
  platform/boot/link_cfg.S
  platform/services/b85m/irq_handler.c
  platform/tc32/div_mod.S
  platform/chip_8258/flash.c
  platform/chip_8258/flash/flash_common.c
  platform/chip_8258/flash/flash_mid1060c8.c
  platform/chip_8258/flash/flash_mid1360c8.c
  platform/chip_8258/flash/flash_mid011460c8.c
  platform/chip_8258/flash/flash_mid134051.c
  platform/chip_8258/flash/flash_mid136085.c
  platform/chip_8258/flash/flash_mid1360eb.c
  platform/chip_8258/flash/flash_mid14325e.c
  platform/chip_8258/flash/flash_mid1460c8.c
  platform/chip_8258/flash/flash_mid13325e.c
  platform/chip_8258/adc.c
  proj/common/list.c
  proj/common/mempool.c
  proj/common/tlPrintf.c
  proj/common/string.c
  proj/common/utility.c
  proj/drivers/drv_gpio.c
  proj/drivers/drv_adc.c
  proj/drivers/drv_nv.c
  proj/drivers/drv_pm.c
  proj/drivers/drv_putchar.c
  proj/drivers/drv_timer.c
  proj/drivers/drv_uart.c
  proj/drivers/drv_calibration.c
  proj/drivers/drv_flash.c
  proj/drivers/drv_hw.c
  proj/drivers/drv_security.c
  proj/os/ev.c
  proj/os/ev_buffer.c
  proj/os/ev_poll.c
  proj/os/ev_queue.c
  proj/os/ev_timer.c
  proj/os/ev_rtc.c
  zigbee/bdb/bdb.c
  zigbee/aps/aps_group.c
  zigbee/mac/mac_phy.c
  zigbee/mac/mac_pib.c
  zigbee/zdo/zdp.c
  zigbee/zcl/zcl.c
  zigbee/zcl/zcl_nv.c
  zigbee/zcl/zcl_reporting.c
  zigbee/zcl/general/zcl_basic.c
  zigbee/zcl/general/zcl_identify.c
  zigbee/zcl/general/zcl_group.c
  zigbee/zcl/general/zcl_onoff.c
  zigbee/zcl/general/zcl_level.c
  zigbee/zcl/ota_upgrading/zcl_ota.c
  zigbee/zcl/ota_upgrading/zcl_ota_attr.c
  zigbee/common/zb_config.c
  zigbee/af/zb_af.c
  zigbee/ss/ss_nv.c
  zigbee/ota/ota.c
  zigbee/ota/otaEpCfg.c
  apps/common/main.c
)

app_sources=(
  "$CORE/glsd301p_uart_frame.c"
  "$CORE/glsd301p_uart_transport.c"
  "$CORE/glsd301p_power_stage_policy.c"
  "$CORE/glsd301p_output_guard.c"
  "$CORE/glsd301p_push_input.c"
  "$CORE/glsd301p_pb4_compat.c"
  "$CORE/glsd301p_runtime_core.c"
  "$TARGET/glsd301p_telink_inert_glue.c"
  "$TARGET/glsd301p_telink_link_sentinels.c"
  "$TARGET/glsd301p_telink_target.c"
)

compile_one() {
  local source="$1" obj="$2" sdk_source="$3"
  local f=("${cflags[@]}")
  : "$sdk_source"  # retained to keep SDK/app call sites explicit for ABI probes
  mkdir -p "$(dirname "$obj")"
  case "$source" in
    *.S) "$TC32_CC" "${asflags[@]}" "${defs[@]}" "${includes[@]}" -c "$source" -o "$obj" ;;
    *glsd301p_telink_*.c)
      "$TC32_CC" "${f[@]}" "${defs[@]}" "${telink_first[@]}" "${includes[@]}" -c "$source" -o "$obj" ;;
    *) "$TC32_CC" "${f[@]}" "${defs[@]}" "${includes[@]}" -c "$source" -o "$obj" ;;
  esac
}

# Source-level architecture/call-surface gates before invoking a compiler.
grep -q '#define GLSD301P_ENDPOINT[[:space:]]*0x0B' "$TARGET/app_cfg.h"
grep -q '#define VOLTAGE_DETECT_ADC_PIN[[:space:]]*GPIO_PB3' "$TARGET/app_cfg.h"
! grep -q '#define VOLTAGE_DETECT_ADC_PIN[[:space:]]*GPIO_PC5' "$TARGET/app_cfg.h"
grep -q '#define ZB_MAC_RX_ON_WHEN_IDLE[[:space:]]*1' "$TARGET/stack_cfg.h"
grep -q '#define TOUCHLINK_SUPPORT[[:space:]]*0' "$TARGET/app_cfg.h"
grep -q '#define ZCL_ZLL_COMMISSIONING_SUPPORT[[:space:]]*0' "$TARGET/app_cfg.h"
grep -q 'POWER_MODE_RECEIVER_SYNCHRONIZED_WHEN_ON_IDLE' "$TARGET/glsd301p_telink_target.c"
grep -q 'POWER_SRC_MAINS_POWER' "$TARGET/glsd301p_telink_target.c"
grep -q 'UART_TX_PB1, UART_RX_PA0' "$TARGET/glsd301p_telink_target.c"
[[ "$(grep -c 'drv_uart_tx_start' "$TARGET/glsd301p_telink_target.c")" -eq 1 ]] || {
  echo 'ERROR: target must retain exactly one blocking UART call for boot OFF only' >&2; exit 1;
}
[[ "$(grep -Ec '^[[:space:]]*if[[:space:]]*\(uart_dma_send\(g_uart_tx_dma\)\)' "$TARGET/glsd301p_telink_target.c")" -eq 1 ]] || {
  echo 'ERROR: runtime UART must contain exactly one nonblocking DMA start' >&2; exit 1;
}
if grep -Eq 'while[[:space:]]*\([^)]*(uart|UART)' "$TARGET/glsd301p_telink_target.c"; then
  echo 'ERROR: target UART runtime contains a polling loop' >&2; exit 1
fi
if grep -E 'GLSD301P_CONTROL_FAMILY_OPERATION' \
    "$TARGET/glsd301p_telink_target.c" "$CORE/glsd301p_runtime_core.c" \
    "$CORE/glsd301p_output_guard.c" "$CORE/glsd301p_power_stage_policy.c"; then
  echo 'ERROR: family-0x02 reached core target runtime' >&2
  exit 1
fi
if grep -q 'glsd301p_control_frame_encode' "$TARGET/glsd301p_telink_target.c"; then
  echo 'ERROR: Telink application bypasses guarded output APIs' >&2
  exit 1
fi

# Disabled Touchlink closure is allowed only in the dedicated inert glue.
grep -q '^u8 deviceInfoRsp = 0u;$' "$TARGET/glsd301p_telink_inert_glue.c"
for hook in touchlink_keyModeSet touchlink_lqiThresholdSet zcl_touchlink_register; do
  grep -q "$hook" "$TARGET/glsd301p_telink_inert_glue.c" || {
    echo "ERROR: missing inert Touchlink closure: $hook" >&2; exit 1;
  }
done
if grep -E 'gpDevice|zclGp|flash_.*otp|ss_apsme|tl_zbNwkBeaconPayloadUpdate' "$TARGET/glsd301p_telink_inert_glue.c"; then
  echo 'ERROR: inert Touchlink glue contains non-Touchlink closure' >&2
  exit 1
fi

# These are link-resolution sentinels only. Any final-ELF reachability is fatal.
gc_only_sentinels=(
  flash_erase_otp
  flash_read_otp
  flash_write_otp
  ss_apsmeSwitchKeyReq
  ss_apsmeTransportKeyReq
  tl_zbNwkBeaconPayloadUpdate
)
for sym in "${gc_only_sentinels[@]}"; do
  grep -q "$sym" "$TARGET/glsd301p_telink_link_sentinels.c" || {
    echo "ERROR: missing GC-only linker sentinel: $sym" >&2; exit 1;
  }
done

rm -rf "$DIR"
mkdir -p "$DIR/obj/sdk" "$DIR/obj/app" "$DIR/obj/abi"
objects=()

# Compile the real pinned SDK type through both translation-unit flag contexts.
# zcl_register() consumes this application-owned array, so a successful link is
# insufficient unless the layouts are byte-for-byte identical.
abi_probe_body='
#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#define ABI_ASSERT(name, expr) typedef char name[(expr) ? 1 : -1]
ABI_ASSERT(glsd_zcl_spec_size, sizeof(zcl_specClusterInfo_t) == 18u);
ABI_ASSERT(glsd_zcl_spec_attr, __builtin_offsetof(zcl_specClusterInfo_t, attrTbl) == 6u);
ABI_ASSERT(glsd_zcl_spec_reg, __builtin_offsetof(zcl_specClusterInfo_t, clusterRegisterFunc) == 10u);
ABI_ASSERT(glsd_zcl_spec_cb, __builtin_offsetof(zcl_specClusterInfo_t, clusterAppCb) == 14u);
int glsd301p_abi_probe(void) { return (int)sizeof(zcl_specClusterInfo_t); }
'
printf '%s' "$abi_probe_body" > "$DIR/sdk_abi_probe.c"
printf '%s' "$abi_probe_body" > "$DIR/glsd301p_telink_abi_probe.c"
compile_one "$DIR/sdk_abi_probe.c" "$DIR/obj/abi/sdk-context.o" 1
compile_one "$DIR/glsd301p_telink_abi_probe.c" "$DIR/obj/abi/app-context.o" 0
echo 'ZCL_SPEC_CLUSTER_INFO_ABI=size18,attrTbl@6,register@10,appCb@14'

for rel in "${sdk_sources[@]}"; do
  src="$SDK/$rel"
  [[ -f "$src" ]] || { echo "ERROR: SDK source missing: $rel" >&2; exit 2; }
  obj="$DIR/obj/sdk/${rel//\//_}.o"
  compile_one "$src" "$obj" 1
  objects+=("$obj")
done

for src in "${app_sources[@]}"; do
  [[ -f "$src" ]] || { echo "ERROR: app source missing: $src" >&2; exit 2; }
  base="$(basename "${src%.c}")"
  obj="$DIR/obj/app/$base.o"
  compile_one "$src" "$obj" 0
  objects+=("$obj")
done

app_obj="$DIR/obj/app/glsd301p_telink_target.o"
"$TC32_NM" "$app_obj" | grep -Eq ' T user_init$' || { echo 'ERROR: target user_init missing' >&2; exit 1; }
"$TC32_NM" -u "$app_obj" | grep -Eq ' U drv_uart_tx_start$' || { echo 'ERROR: boot-OFF UART dependency missing' >&2; exit 1; }
"$TC32_NM" -u "$app_obj" | grep -Eq ' U uart_dma_send$' || { echo 'ERROR: nonblocking runtime UART dependency missing' >&2; exit 1; }
"$TC32_NM" -u "$app_obj" | grep -Eq ' U glsd301p_runtime_core_(apply_state|poll_push_ex|poll_pb4)' || {
  echo 'ERROR: target is not wired through guarded runtime core' >&2; exit 1;
}

elf="$DIR/glsd301p-ed.elf"
raw="$DIR/glsd301p-ed.bin"
final="$DIR/glsd301p-ed.final.bin"
map="$DIR/glsd301p-ed.map"
lst="$DIR/glsd301p-ed.lst"

"$TC32_LD" --gc-sections -nostartfiles \
  -T"$SDK/platform/boot/8258/boot_8258.link" -Map="$map" \
  -L"$SDK/zigbee/lib/tc32" -L"$SDK/platform/lib" \
  -o "$elf" "${objects[@]}" \
  --start-group -ldrivers_8258 -lzb_ed --end-group

"$TC32_OBJCOPY" -O binary "$elf" "$raw"
"$TC32_OBJDUMP" -h -t "$elf" > "$lst"
"$TC32_NM" -u "$elf" > "$DIR/unresolved.txt" || true
if [[ -s "$DIR/unresolved.txt" ]]; then
  echo 'ERROR: unresolved symbols'
  cat "$DIR/unresolved.txt"
  echo '--- unresolved-symbol ownership autopsy ---'
  while read -r kind sym; do
    [[ "$kind" == U ]] || continue
    echo "### $sym"
    for input in "$SDK/zigbee/lib/tc32/libzb_ed.a" "$SDK/platform/lib/libdrivers_8258.a" "${objects[@]}"; do
      [[ -f "$input" ]] || continue
      "$TC32_NM" -A "$input" 2>/dev/null | awk -v s="$sym" '$NF == s {print}' || true
    done
  done < "$DIR/unresolved.txt"
  echo '--- end ownership autopsy ---'
  exit 1
fi

# The role/security/beacon/OTP shims are valid only as pre-GC resolution aids.
# If any survives, a supposedly dead ED section is actually reachable.
for sym in "${gc_only_sentinels[@]}"; do
  if "$TC32_NM" "$elf" | awk -v s="$sym" '$NF == s {found=1} END {exit(found ? 0 : 1)}'; then
    echo "ERROR: GC-only linker sentinel survived final ELF: $sym" >&2
    exit 1
  fi
done
echo 'GC_ONLY_LINK_SENTINELS_FINAL_ELF=NONE'

# Touchlink remains disabled; only the three inert BDB closure hooks and the
# zero response-state byte may exist. No ZLL implementation TU is compiled.
if grep -Eq 'zcl_zll_commissioning\.c|zcl_zll_commissioning\.o' "$map"; then
  echo 'ERROR: Touchlink implementation entered final link map' >&2
  exit 1
fi
echo 'TOUCHLINK_IMPLEMENTATION_LINKED=NO'

raw_bytes="$(stat -c %s "$raw")"
(( raw_bytes < APP_SLOT_SIZE )) || { echo 'ERROR: raw image exceeds 0x34000 app slot' >&2; exit 1; }
python3 "$FINALIZER" check-link "$raw" --file-version "$FILE_VERSION" --max-final-size "$APP_SLOT_SIZE"
python3 "$FINALIZER" finalize "$raw" "$final" --file-version "$FILE_VERSION" --max-final-size "$APP_SLOT_SIZE"
python3 "$FINALIZER" check-final "$final" --file-version "$FILE_VERSION" --max-final-size "$APP_SLOT_SIZE"
final_bytes="$(stat -c %s "$final")"
physical_b_end=$((BANK_B_BASE + final_bytes))
(( physical_b_end < BANK_B_SLOT_END )) || { echo 'ERROR: target does not fit physical bank B slot' >&2; exit 1; }
(( physical_b_end < MAC_REGION_START )) || { echo 'ERROR: target reaches MAC storage region' >&2; exit 1; }

text_vma_hex="$("$TC32_OBJDUMP" -h "$elf" | awk '$2 == ".text" {print $4; exit}')"
[[ "$text_vma_hex" =~ ^[0-9A-Fa-f]+$ ]] || { echo 'ERROR: cannot parse .text VMA' >&2; exit 1; }
text_vma=$((16#$text_vma_hex))
(( text_vma < BANK_B_BASE )) || { echo 'ERROR: target appears physically relinked to bank B' >&2; exit 1; }

# Gate the actual reachable application/runtime chain rather than diagnostics-only
# wrappers, which are correctly removed by --gc-sections when unreferenced.
for sym in \
  user_init \
  glsd301p_runtime_core_apply_state \
  glsd301p_runtime_core_poll_push_ex \
  glsd301p_runtime_core_poll_pb4 \
  glsd301p_uart_transport_offer; do
  "$TC32_NM" "$elf" | grep -Eq " [Tt] ${sym}$" || {
    echo "ERROR: required reachable runtime symbol missing: $sym" >&2
    exit 1
  }
done
echo 'REACHABLE_TARGET_RUNTIME_CHAIN=PASS'

if grep -q 'libzb_router' "$map"; then
  echo 'ERROR: router archive entered final link map' >&2; exit 1
fi
grep -q 'libzb_ed' "$map" || { echo 'ERROR: End Device stack archive absent from final link map' >&2; exit 1; }

{
  echo QUARANTINED_BUILD=YES
  echo DEPLOYABLE=NO
  echo FIRST_FLASHABLE_CANARY_ALLOWED=NO
  echo SDK_EXPECTED_COMMIT=d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e
  echo STACK_ARCHIVE=libzb_ed.a
  echo STACK_ARCHIVE_GROUP_RESCAN=YES
  echo ROUTER_ARCHIVE_LINKED=NO
  echo ZB_ED_ROLE=1
  echo ZB_ROUTER_ROLE=0
  echo ZB_MAC_RX_ON_WHEN_IDLE=1
  echo PM_ENABLE=0
  echo ENDPOINT=11
  echo POWER_SOURCE=MAINS
  echo BOOT_FIRST_POWER_STAGE_FRAME=A55A010004AA
  echo FAMILY_0x02_CORE_RUNTIME=ABSENT
  echo TOUCHLINK_SUPPORT=0
  echo TOUCHLINK_IMPLEMENTATION_LINKED=NO
  echo TOUCHLINK_CLOSURE=INERT_BDB_HOOKS_ONLY
  echo GC_ONLY_LINK_SENTINELS_FINAL_ELF=NONE
  echo REACHABLE_TARGET_RUNTIME_CHAIN=PASS
  echo ADC_FLASH_SAFETY_PIN=GPIO_PB3_VENDOR_FIRMWARE_CONFIRMED
  echo UART=9600_8N1_PB1_TX_PA0_RX
  echo ZCL_SPEC_CLUSTER_INFO_ABI=size18_attrTbl6_register10_appCb14
  echo UART_RUNTIME_TRANSPORT=NONBLOCKING_OFF_PRIORITY_LATEST_NORMAL
  echo "RAW_BINARY_SIZE=$raw_bytes"
  echo "FINAL_BINARY_SIZE=$final_bytes"
  printf 'TEXT_VMA=0x%08x\n' "$text_vma"
  printf 'PHYSICAL_BANK_B_END_EXCLUSIVE=0x%05x\n' "$physical_b_end"
  "$TC32_SIZE" "$elf"
  sha256sum "$elf" "$raw" "$final" "$map"
} | tee "$DIR/manifest.txt"

echo GLSD301P_TC32_END_DEVICE_LINK=PASS
echo GLSD301P_TARGET_ARTIFACT=QUARANTINED_NOT_DEPLOYABLE
