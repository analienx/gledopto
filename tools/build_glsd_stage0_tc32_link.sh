#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/firmware/gl-sd-301p-ed"
FIXTURE="$SRC/telink_stage0_fixture"
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
OUT_DIR="${OUT_DIR:-${TMPDIR:-/tmp}/glsd-stage0-tc32-link}"
DIR="$OUT_DIR/stage0"
SAMPLE_DIR="$SDK/apps/sampleLight"
COMMON_APP="$SDK/apps/common"

APP_SLOT_SIZE=0x34000
BANK_B_BASE=0x40000
STAGE0_BACKUP_SECTOR=0x70000
STAGE0_JOURNAL_SECTOR=0x71000
MAC_REGION_START=0x76000
FACTORY_REGION_START=0x77000
FLASH_END=0x80000
STAGE0_MAX_INNER_SIZE=$((STAGE0_BACKUP_SECTOR - BANK_B_BASE))

[[ -f "$SAMPLE_DIR/board_8258_dongle.h" ]] || { echo "ERROR: complete V3.7.2.0-style 8258 fixture required" >&2; exit 2; }
[[ -f "$SDK/platform/boot/8258/boot_8258.link" ]] || { echo "ERROR: boot_8258.link missing" >&2; exit 2; }
[[ -f "$SDK/zigbee/lib/tc32/libzb_ed.a" ]] || { echo "ERROR: Telink End Device stack archive missing" >&2; exit 2; }
[[ -f "$FINALIZER" ]] || { echo "ERROR: Telink finalizer missing" >&2; exit 2; }

roots=("$SDK/proj" "$SDK/platform" "$SDK/zigbee" "$COMMON_APP" "$SAMPLE_DIR")
includes=(-I"$FIXTURE" -I"$SAMPLE_DIR" -I"$COMMON_APP" -I"$SDK/proj" -I"$SRC")
while IFS= read -r -d '' d; do includes+=("-I$d"); done < <(find "${roots[@]}" -type d -print0 | sort -zu)

defs=(-DGLSD_TELINK_SDK -DGLSD_STAGE0_CANARY=1 -DMCU_CORE_8258=1 -DEND_DEVICE=1 -DMCU_STARTUP_8258=1)
telink_first=(-D_SIZE_T -D_SIZE_T_ -D__SIZE_T -D__SIZE_T__)
cflags=(-O2 -ffunction-sections -fdata-sections -fshort-enums -finline-small-functions -std=gnu99 -funsigned-char -fshort-wchar -fms-extensions -nostartfiles -nostdlib)
asflags=(-fomit-frame-pointer -fshort-enums -fdata-sections -ffunction-sections)

# This is intentionally not identical to the full product closure. In
# particular, Telink's generic b85m irq_handler.c references UART DMA handlers
# unconditionally. Stage-0 substitutes its own RF/timer-only dispatcher so the
# final image has no UART execution path at all.
sdk_sources=(
  platform/boot/8258/cstartup_8258.S
  platform/boot/link_cfg.S
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
  glsd_stage0_irq_handler.c
  glsd_stage0_recovery.c
  glsd_stage0_app.c
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
    *glsd_stage0_app.c|*glsd_telink_disabled_feature_glue.c)
      "$TC32_CC" "${f[@]}" "${defs[@]}" "${telink_first[@]}" "${includes[@]}" -c "$source" -o "$obj"
      ;;
    *)
      "$TC32_CC" "${f[@]}" "${defs[@]}" "${includes[@]}" -c "$source" -o "$obj"
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
  [[ -f "$src" ]] || { echo "ERROR: Stage-0 source missing: $rel" >&2; exit 2; }
  obj="$DIR/obj/app/${rel%.c}.o"
  compile_one "$src" "$obj" 0
  objects+=("$obj")
done

elf="$DIR/glsd-stage0.elf"
bin="$DIR/glsd-stage0.bin"
final_bin="$DIR/glsd-stage0.final.bin"
map="$DIR/glsd-stage0.map"
lst="$DIR/glsd-stage0.lst"
relocs="$DIR/live-relocations.txt"

"$TC32_LD" --gc-sections -nostartfiles -T"$SDK/platform/boot/8258/boot_8258.link" -Map="$map" \
  -L"$SDK/zigbee/lib/tc32" -L"$SDK/platform/lib" \
  -o "$elf" "${objects[@]}" -ldrivers_8258 -lzb_ed

"$TC32_OBJCOPY" -O binary "$elf" "$bin"
"$TC32_OBJDUMP" -h -t "$elf" > "$lst"
"$TC32_OBJDUMP" -r "$elf" > "$relocs"

if grep -Eq '^[[:space:]]*[0-9A-Fa-f]+[[:space:]]+' "$relocs"; then
  echo 'ERROR: Stage-0 ELF still contains live relocation records' >&2
  cat "$relocs" >&2
  exit 1
fi

raw_bytes="$(stat -c %s "$bin")"
(( raw_bytes < APP_SLOT_SIZE )) || { echo "ERROR: raw Stage-0 exceeds app slot" >&2; exit 1; }
file_version="$(awk '/^[[:space:]]*#define[[:space:]]+FILE_VERSION[[:space:]]+/ {print $3; exit}' "$FIXTURE/version_cfg.h")"
[[ "$file_version" =~ ^0[xX][0-9A-Fa-f]+$ ]] || { echo "ERROR: cannot parse Stage-0 FILE_VERSION" >&2; exit 2; }
python3 "$FINALIZER" check-link "$bin" --file-version "$file_version" --max-final-size "$STAGE0_MAX_INNER_SIZE"
python3 "$FINALIZER" finalize "$bin" "$final_bin" --file-version "$file_version" --max-final-size "$STAGE0_MAX_INNER_SIZE"
python3 "$FINALIZER" check-final "$final_bin" --file-version "$file_version" --max-final-size "$STAGE0_MAX_INNER_SIZE"
final_bytes="$(stat -c %s "$final_bin")"
physical_b_end=$((BANK_B_BASE + final_bytes))

(( physical_b_end <= STAGE0_BACKUP_SECTOR )) || {
  echo "ERROR: Stage-0 executable overlaps persistent rollback backup sector" >&2
  exit 1
}
(( STAGE0_JOURNAL_SECTOR + 0x1000 <= MAC_REGION_START )) || {
  echo "ERROR: Stage-0 journal reaches MAC region" >&2
  exit 1
}
(( FACTORY_REGION_START < FLASH_END )) || exit 1

# Negative capability is a final-ELF property, not merely a source convention.
if "$TC32_NM" "$elf" | grep -E '(glsd_power_stage|glsd301p_push|glsd301p_pb4|drv_uart_|uart_dma|uart_send)'; then
  echo 'ERROR: forbidden power-stage/UART/local-input capability survived Stage-0 link' >&2
  exit 1
fi
if "$TC32_NM" "$elf" | grep -E '([[:space:]])(zb_nwkFormation|bdb_networkFormationStart|zb_setPermitJoin)$'; then
  echo 'ERROR: Stage-0 contains network-formation/permit-join capability' >&2
  exit 1
fi
if ! "$TC32_NM" "$elf" | grep -q 'glsd_stage0_recovery_prepare'; then
  echo 'ERROR: Stage-0 recovery routine missing from final image' >&2
  exit 1
fi

{
  echo PRODUCT_FIRMWARE=GL-SD-301P-STAGE0
  echo STAGE0_CANARY=YES
  echo FLASHABLE_ARTIFACT=YES
  echo DEPLOY_AUTHORIZED=NO
  echo DEPLOYABLE=NO
  echo DEPLOYABLE_BLOCKER=PRODUCTION_ONLY_STAGE0_NOT_YET_EXECUTED
  echo ZIGBEE_STACK_ARCHIVE=libzb_ed.a
  echo ZIGBEE_ROLE=END_DEVICE
  echo ZB_MAC_RX_ON_WHEN_IDLE=1
  echo PM_ENABLE=0
  echo MODULE_WATCHDOG_ENABLE=1
  echo POWER_STAGE_DRIVER=NONE
  echo POWER_STAGE_UART_LINKED=NO
  echo PUSH_PB4_LINKED=NO
  echo GENERIC_UART_IRQ_DISPATCHER=REPLACED
  echo NORMAL_GLEDOPTO_OTA_WHEN_ARMED=DISABLED
  echo RECOVERY_ONLY_OTA_IMAGE_TYPE=0x7F10
  echo STOCK_BANK_RESTORE=JOURNALED_FIRST_SECTOR
  echo STOCK_BANK_FULL_IMAGE_CRC_REVALIDATION=YES
  echo STOCK_BOOT_ENABLE=TWO_PHASE_FF_TO_4B_COMMIT
  echo STAGE0_SELF_INVALIDATION=AFTER_STOCK_REVALIDATION_ONLY
  echo POWER_FAIL_INVARIANT=B_REMAINS_BOOTABLE_UNTIL_A_VALID
  printf 'STAGE0_BACKUP_SECTOR=0x%05x\n' "$STAGE0_BACKUP_SECTOR"
  printf 'STAGE0_JOURNAL_SECTOR=0x%05x\n' "$STAGE0_JOURNAL_SECTOR"
  printf 'STAGE0_PHYSICAL_B_END_EXCLUSIVE=0x%05x\n' "$physical_b_end"
  echo STAGE0_SCRATCH_FIT=PASS
  echo TELINK_XCRC32=PASS
  git -C "$SDK" rev-parse HEAD 2>/dev/null | sed 's/^/SDK_GIT_HEAD=/' || true
  "$TC32_CC" --version | head -n 1 | sed 's/^/COMPILER_VERSION=/'
  "$TC32_SIZE" "$elf"
  sha256sum "$elf" "$bin" "$final_bin" "$map" "$relocs"
} | tee "$DIR/manifest.txt"

echo GLSD_STAGE0_TC32_FULL_LINK=PASS
echo 'STOP: Stage-0 is structurally flashable but no device deployment is authorized by this build.'
