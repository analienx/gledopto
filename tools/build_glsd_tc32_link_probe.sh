#!/usr/bin/env bash
set -euo pipefail

# Build and LINK the minimal GL-SD extraction stager against the public Telink
# TLSR8258 SDK as a mechanics proof only. Outputs stay in OUT_DIR. This script
# never creates a Zigbee OTA container and never accesses or serves hardware.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/firmware/wireless-dump-stager"
FIXTURE="$SRC/telink_fixture"
: "${TELINK_SDK_ROOT:?set TELINK_SDK_ROOT}"
SDK="$(cd "$TELINK_SDK_ROOT" && pwd)"

TC32_CC="${TC32_CC:-tc32-elf-gcc}"
TC32_LD="${TC32_LD:-$(dirname "$TC32_CC")/tc32-elf-ld}"
TC32_NM="${TC32_NM:-$(dirname "$TC32_CC")/tc32-elf-nm}"
TC32_SIZE="${TC32_SIZE:-$(dirname "$TC32_CC")/tc32-elf-size}"
TC32_OBJCOPY="${TC32_OBJCOPY:-$(dirname "$TC32_CC")/tc32-elf-objcopy}"
TC32_OBJDUMP="${TC32_OBJDUMP:-$(dirname "$TC32_CC")/tc32-elf-objdump}"
OUT_DIR="${OUT_DIR:-${TMPDIR:-/tmp}/glsd-tc32-link}"
mkdir -p "$OUT_DIR"

for t in "$TC32_CC" "$TC32_LD" "$TC32_NM" "$TC32_SIZE" "$TC32_OBJCOPY" "$TC32_OBJDUMP"; do
  [[ -x "$t" ]] || command -v "$t" >/dev/null 2>&1 || { echo "ERROR: tool not found: $t" >&2; exit 2; }
done

SAMPLE_DIR="$SDK/apps/sampleLight"
COMMON_APP="$SDK/apps/common"
[[ -f "$SAMPLE_DIR/board_8258_dongle.h" ]] || { echo "ERROR: V3.7.2.0-style 8258 fixture required" >&2; exit 2; }
[[ -f "$SDK/platform/boot/8258/boot_8258.link" ]] || { echo "ERROR: boot_8258.link missing" >&2; exit 2; }

include_roots=("$SDK/proj" "$SDK/platform" "$SDK/zigbee" "$COMMON_APP" "$SAMPLE_DIR")
includes=(-I"$FIXTURE" -I"$SAMPLE_DIR" -I"$COMMON_APP" -I"$SDK/proj")
while IFS= read -r -d '' d; do includes+=("-I$d"); done < <(find "${include_roots[@]}" -type d -print0 | sort -zu)

common_defs=(-DGLSD_TELINK_SDK -DMCU_CORE_8258=1 -DROUTER=1 -DMCU_STARTUP_8258=1)
telink_first_defs=(-D_SIZE_T -D_SIZE_T_ -D__SIZE_T -D__SIZE_T__)
common_flags=(-O2 -ffunction-sections -fdata-sections -fshort-enums -finline-small-functions -std=gnu99 -funsigned-char -fshort-wchar -fms-extensions -nostartfiles -nostdlib)
asm_flags=(-fomit-frame-pointer -fshort-enums -fdata-sections -ffunction-sections)

# Generic SDK closure based on the public 8258 build topology used by mature
# open-source Telink firmware. Intentionally NO sampleLight*.c, light control,
# key UI, GPIO application, factory_reset.c, or touchlink application sources.
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
  glsd_stager_core.c
  glsd_stager_dispatch.c
  glsd_transport_adapter.c
  glsd_telink_sdk_adapter.c
  glsd_telink_stager_app.c
)

compile_one() {
  local source="$1" obj="$2" base="$3" is_sdk="$4"
  mkdir -p "$(dirname "$obj")"
  local defs=("${common_defs[@]}" "-DGLSD_STAGER_LINK_BASE=$base")
  local flags=("${common_flags[@]}")
  if [[ "$is_sdk" == 1 ]]; then flags+=(-fpack-struct); fi
  case "$source" in
    *.S) "$TC32_CC" "${asm_flags[@]}" "${defs[@]}" "${includes[@]}" -c "$source" -o "$obj" ;;
    *glsd_telink_sdk_adapter.c|*glsd_telink_stager_app.c)
      "$TC32_CC" "${flags[@]}" "${defs[@]}" "${telink_first_defs[@]}" "${includes[@]}" -I"$SRC" -c "$source" -o "$obj" ;;
    *) "$TC32_CC" "${flags[@]}" "${defs[@]}" "${includes[@]}" -I"$SRC" -c "$source" -o "$obj" ;;
  esac
}

build_bank() {
  local label="$1" base="$2" dir="$OUT_DIR/$label"
  rm -rf "$dir" && mkdir -p "$dir/obj/sdk" "$dir/obj/app"
  local objects=() rel src obj

  for rel in "${sdk_sources[@]}"; do
    src="$SDK/$rel"
    [[ -f "$src" ]] || { echo "ERROR: required SDK source missing: $rel" >&2; exit 2; }
    obj="$dir/obj/sdk/${rel//\//_}.o"
    compile_one "$src" "$obj" "$base" 1
    objects+=("$obj")
  done
  for rel in "${app_sources[@]}"; do
    src="$SRC/$rel"
    obj="$dir/obj/app/${rel%.c}.o"
    compile_one "$src" "$obj" "$base" 0
    objects+=("$obj")
  done

  local elf="$dir/glsd-stager-$label.elf"
  local bin="$dir/glsd-stager-$label.bin"
  local map="$dir/glsd-stager-$label.map"
  local lst="$dir/glsd-stager-$label.lst"

  "$TC32_LD" --gc-sections -nostartfiles \
    -T"$SDK/platform/boot/8258/boot_8258.link" \
    -Map="$map" \
    -L"$SDK/zigbee/lib/tc32" -L"$SDK/platform/lib" \
    -o "$elf" "${objects[@]}" -ldrivers_8258 -lzb_router

  "$TC32_OBJCOPY" -O binary "$elf" "$bin"
  "$TC32_OBJDUMP" -h -t "$elf" > "$lst"
  "$TC32_SIZE" "$elf"
  "$TC32_NM" -u "$elf" > "$dir/unresolved.txt" || true
  [[ ! -s "$dir/unresolved.txt" ]] || { echo "ERROR: unresolved symbols in $label"; cat "$dir/unresolved.txt"; exit 1; }

  local bytes
  bytes="$(stat -c %s "$bin")"
  # Mechanics slot limit inherited from the proved 512K dual-bank SDK layout.
  (( bytes < 0x34000 )) || { echo "ERROR: $label binary exceeds 0x34000 mechanics slot" >&2; exit 1; }

  {
    echo "MECHANICS_ONLY=YES"
    echo "DEPLOYABLE=NO"
    echo "BANK=$label"
    echo "GLSD_STAGER_LINK_BASE=$base"
    echo "BINARY_SIZE=$bytes"
    sha256sum "$elf" "$bin" "$map"
    echo "POWER_STAGE_SYMBOL_SCAN"
    if "$TC32_NM" "$elf" | grep -Eai '(^|[[:space:]])(light_|led_|pwm_|gpio_|factoryRst|factory_reset|bdb_networkSteerStart)'; then
      echo "ERROR: forbidden application power/reset/steering symbol reachable" >&2
      exit 1
    else
      echo "NONE"
    fi
    echo "EXTRACTION_WRITE_SYMBOL_SCAN"
    if "$TC32_NM" "$dir/obj/app/"*.o | grep -Eai '(flash_write|flash_erase|nv_flashWrite|nv_reset|factory|leave|commission)'; then
      echo "ERROR: private extraction/app objects import mutation primitive" >&2
      exit 1
    else
      echo "NONE"
    fi
  } | tee "$dir/manifest.txt"
}

build_bank bank_a 0x00000
build_bank bank_b 0x40000

echo "GLSD_TC32_FULL_LINK_PROBE=PASS_BOTH_BANKS"
echo "STOP: linked outputs are mechanics-only, non-deployable, and must not be packaged or served as OTA."
