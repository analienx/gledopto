#!/usr/bin/env bash
set -euo pipefail

# Build the actual GL-SD-301P End Device product application against Telink's
# pinned TLSR8258 End Device stack. The electrical power-stage implementation
# is still a rejecting stub, so the resulting image is intentionally marked
# DEPLOYABLE=NO even though the Zigbee application is a complete linked image.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/firmware/gl-sd-301p-ed"
FIXTURE="$SRC/telink_fixture"
FINALIZER="$ROOT/tools/telink_app_finalize.py"

: "${TELINK_SDK_ROOT:?set TELINK_SDK_ROOT to the tl_zigbee_sdk directory}"
SDK="$(cd "$TELINK_SDK_ROOT" && pwd)"
TC32_CC="${TC32_CC:-tc32-elf-gcc}"
BINDIR="$(dirname "$TC32_CC")"
TC32_LD="${TC32_LD:-$BINDIR/tc32-elf-ld}"
TC32_NM="${TC32_NM:-$BINDIR/tc32-elf-nm}"
TC32_SIZE="${TC32_SIZE:-$BINDIR/tc32-elf-size}"
TC32_OBJCOPY="${TC32_OBJCOPY:-$BINDIR/tc32-elf-objcopy}"
TC32_OBJDUMP="${TC32_OBJDUMP:-$BINDIR/tc32-elf-objdump}"
OUT_DIR="${OUT_DIR:-${TMPDIR:-/tmp}/glsd-ed-tc32-link}"
DIR="$OUT_DIR/product"
SAMPLE_DIR="$SDK/apps/sampleLight"
COMMON_APP="$SDK/apps/common"

APP_SLOT_SIZE=0x34000
BANK_A_BASE=0x00000
BANK_B_BASE=0x40000
BANK_A_SLOT_END=0x34000
BANK_B_SLOT_END=0x74000
MAC_REGION_START=0x76000
FACTORY_REGION_START=0x77000
FLASH_END=0x80000

[[ -f "$SAMPLE_DIR/board_8258_dongle.h" ]] || { echo "ERROR: complete V3.7.2.0-style 8258 fixture required" >&2; exit 2; }
[[ -f "$SDK/platform/boot/8258/boot_8258.link" ]] || { echo "ERROR: boot_8258.link missing" >&2; exit 2; }
[[ -f "$SDK/zigbee/lib/tc32/libzb_ed.a" ]] || { echo "ERROR: Telink End Device stack archive missing" >&2; exit 2; }
[[ -f "$FINALIZER" ]] || { echo "ERROR: Telink finalizer missing" >&2; exit 2; }

roots=("$SDK/proj" "$SDK/platform" "$SDK/zigbee" "$COMMON_APP" "$SAMPLE_DIR")
includes=(-I"$FIXTURE" -I"$SAMPLE_DIR" -I"$COMMON_APP" -I"$SDK/proj")
while IFS= read -r -d '' d; do includes+=("-I$d"); done < <(find "${roots[@]}" -type d -print0 | sort -zu)

defs=(-DGLSD_TELINK_SDK -DMCU_CORE_8258=1 -DEND_DEVICE=1 -DMCU_STARTUP_8258=1)
telink_first=(-D_SIZE_T -D_SIZE_T_ -D__SIZE_T -D__SIZE_T__)
cflags=(-O2 -ffunction-sections -fdata-sections -fshort-enums -finline-small-functions -std=gnu99 -funsigned-char -fshort-wchar -fms-extensions -nostartfiles -nostdlib)
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
  zigbee/zcl/general/zcl_group.c
  zigbee/zcl/general/zcl_identify.c
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
  glsd_ed_core.c
  glsd_power_stage_stub.c
  glsd_telink_ed_app.c
  glsd_telink_disabled_feature_glue.c
)

compile_one() {
  local source="$1" obj="$2" sdk_source="$3"
  local f=("${cflags[@]}")
  [[ "$sdk_source" == 1 ]] && f+=(-fpack-struct)
  mkdir -p "$(dirname "$obj")"
  case "$source" in
    *.S)
      "$TC32_CC" "${asflags[@]}" "${defs[@]}" "${includes[@]}" -c "$source" -o "$obj"
      ;;
    *glsd_telink_ed_app.c|*glsd_telink_disabled_feature_glue.c)
      "$TC32_CC" "${f[@]}" "${defs[@]}" "${telink_first[@]}" "${includes[@]}" -I"$SRC" -c "$source" -o "$obj"
      ;;
    *)
      "$TC32_CC" "${f[@]}" "${defs[@]}" "${includes[@]}" -I"$SRC" -c "$source" -o "$obj"
      ;;
  esac
}

rm -rf "$DIR"
mkdir -p "$DIR/obj/sdk" "$DIR/obj/app"
objects=()

for rel in "${sdk_sources[@]}"; do
  src="$SDK/$rel"
  [[ -f "$src" ]] || { echo "ERROR: SDK source missing: $rel" >&2; exit 2; }
  obj="$DIR/obj/sdk/${rel//\//_}.o"
  compile_one "$src" "$obj" 1
  objects+=("$obj")
done

for rel in "${app_sources[@]}"; do
  src="$SRC/$rel"
  [[ -f "$src" ]] || { echo "ERROR: product source missing: $rel" >&2; exit 2; }
  obj="$DIR/obj/app/${rel%.c}.o"
  compile_one "$src" "$obj" 0
  objects+=("$obj")
done

elf="$DIR/glsd-ed.elf"
bin="$DIR/glsd-ed.bin"
final_bin="$DIR/glsd-ed.final.bin"
map="$DIR/glsd-ed.map"
lst="$DIR/glsd-ed.lst"

# The Telink driver and End Device stack archives contain cross-archive
# dependencies. Use a linker group so the archives are rescanned to a fixed
# point; do not replace genuine stack/security functions with application stubs.
"$TC32_LD" --gc-sections -nostartfiles -T"$SDK/platform/boot/8258/boot_8258.link" -Map="$map" \
  -L"$SDK/zigbee/lib/tc32" -L"$SDK/platform/lib" \
  -o "$elf" "${objects[@]}" --start-group -ldrivers_8258 -lzb_ed --end-group

"$TC32_OBJCOPY" -O binary "$elf" "$bin"
"$TC32_OBJDUMP" -h -t "$elf" > "$lst"
"$TC32_NM" -u "$elf" > "$DIR/unresolved.txt" || true
[[ ! -s "$DIR/unresolved.txt" ]] || { echo "ERROR: unresolved symbols" >&2; cat "$DIR/unresolved.txt"; exit 1; }

raw_bytes="$(stat -c %s "$bin")"
(( raw_bytes < APP_SLOT_SIZE )) || { echo "ERROR: raw image exceeds 0x34000 app slot" >&2; exit 1; }
python3 "$FINALIZER" check-link "$bin" --max-final-size "$APP_SLOT_SIZE"
python3 "$FINALIZER" finalize "$bin" "$final_bin" --max-final-size "$APP_SLOT_SIZE"
python3 "$FINALIZER" check-final "$final_bin" --max-final-size "$APP_SLOT_SIZE"
final_bytes="$(stat -c %s "$final_bin")"

physical_a_end=$((BANK_A_BASE + final_bytes))
physical_b_end=$((BANK_B_BASE + final_bytes))
(( physical_a_end < BANK_A_SLOT_END )) || { echo "ERROR: image does not fit bank A" >&2; exit 1; }
(( physical_b_end < BANK_B_SLOT_END )) || { echo "ERROR: image does not fit bank B" >&2; exit 1; }
(( physical_b_end < MAC_REGION_START )) || { echo "ERROR: bank-B placement reaches MAC region" >&2; exit 1; }
(( physical_b_end < FACTORY_REGION_START )) || { echo "ERROR: bank-B placement reaches factory region" >&2; exit 1; }
(( physical_b_end <= FLASH_END )) || { echo "ERROR: bank-B placement exceeds 512K flash" >&2; exit 1; }

text_vma_hex="$("$TC32_OBJDUMP" -h "$elf" | awk '$2 == ".text" {print $4; exit}')"
[[ "$text_vma_hex" =~ ^[0-9A-Fa-f]+$ ]] || { echo "ERROR: cannot parse .text VMA" >&2; exit 1; }
text_vma=$((16#$text_vma_hex))
(( text_vma < raw_bytes )) || { echo "ERROR: .text is outside logical image" >&2; exit 1; }
(( text_vma < BANK_B_BASE )) || { echo "ERROR: product image was physically relinked instead of logical-address-0" >&2; exit 1; }

# Product must be linked against the End Device archive only. Fail if obvious
# coordinator/router application primitives survive into the final image.
if "$TC32_NM" "$elf" | grep -E '([[:space:]])(zb_nwkFormation|bdb_networkFormationStart|zb_setPermitJoin)$'; then
  echo "ERROR: router/coordinator formation primitive survived product link" >&2
  exit 1
fi

# Generic flash-vendor OTP wrappers may exist pre-link but must be removed by GC.
if "$TC32_NM" "$elf" | grep -Eai '(^|[[:space:]_])flash_(read|write|erase|lock)_otp'; then
  echo "ERROR: OTP wrapper survived product link" >&2
  exit 1
fi

{
  echo PRODUCT_FIRMWARE=GL-SD-301P-ED
  echo DEPLOYABLE=NO
  echo DEPLOYABLE_BLOCKER=POWER_STAGE_DRIVER_STUB
  echo POWER_STAGE_DRIVER=STUB
  echo ZIGBEE_STACK_ARCHIVE=libzb_ed.a
  echo ZIGBEE_ROLE=END_DEVICE
  echo ZB_MAC_RX_ON_WHEN_IDLE=1
  echo PM_ENABLE=0
  echo ENDPOINT=11
  echo BANK_NEUTRAL=YES
  echo LOGICAL_LINK_BASE=0x00000
  echo "RAW_BINARY_SIZE=$raw_bytes"
  echo "FINAL_INNER_BINARY_SIZE=$final_bytes"
  printf 'PHYSICAL_A_END_EXCLUSIVE=0x%05x\n' "$physical_a_end"
  printf 'PHYSICAL_B_END_EXCLUSIVE=0x%05x\n' "$physical_b_end"
  printf 'BANK_A_SLOT_END=0x%05x\n' "$BANK_A_SLOT_END"
  printf 'BANK_B_SLOT_END=0x%05x\n' "$BANK_B_SLOT_END"
  printf 'TEXT_VMA=0x%08x\n' "$text_vma"
  echo TELINK_MULTI_ADDRESS_FIT=PASS
  echo TELINK_PREAMBLE=PASS
  echo TELINK_XCRC32=PASS
  git -C "$SDK" rev-parse HEAD 2>/dev/null | sed 's/^/SDK_GIT_HEAD=/' || true
  "$TC32_CC" --version | head -n 1 | sed 's/^/COMPILER_VERSION=/'
  "$TC32_SIZE" "$elf"
  sha256sum "$elf" "$bin" "$final_bin" "$map"
} | tee "$DIR/manifest.txt"

echo GLSD_ED_TC32_FULL_LINK=PASS
echo GLSD_ED_STACK=TELINK_END_DEVICE
echo 'STOP: product image is non-deployable until glsd_power_stage_stub.c is replaced by the verified GL-SD hardware driver.'
