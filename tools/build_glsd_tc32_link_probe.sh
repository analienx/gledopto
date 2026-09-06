#!/usr/bin/env bash
set -euo pipefail

# Full TLSR8258 link mechanics proof for the minimal GL-SD extraction stager.
# Outputs remain temporary. No Zigbee OTA container is created or served.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/firmware/wireless-dump-stager"
FIXTURE="$SRC/telink_fixture"
FINALIZER="$ROOT/tools/telink_app_finalize.py"
: "${TELINK_SDK_ROOT:?set TELINK_SDK_ROOT}"
SDK="$(cd "$TELINK_SDK_ROOT" && pwd)"
TC32_CC="${TC32_CC:-tc32-elf-gcc}"
BINDIR="$(dirname "$TC32_CC")"
TC32_LD="${TC32_LD:-$BINDIR/tc32-elf-ld}"
TC32_NM="${TC32_NM:-$BINDIR/tc32-elf-nm}"
TC32_SIZE="${TC32_SIZE:-$BINDIR/tc32-elf-size}"
TC32_OBJCOPY="${TC32_OBJCOPY:-$BINDIR/tc32-elf-objcopy}"
TC32_OBJDUMP="${TC32_OBJDUMP:-$BINDIR/tc32-elf-objdump}"
OUT_DIR="${OUT_DIR:-${TMPDIR:-/tmp}/glsd-tc32-link}"
SAMPLE_DIR="$SDK/apps/sampleLight"
COMMON_APP="$SDK/apps/common"
APP_SLOT_SIZE=0x34000
BANK_A_BASE=0x00000
BANK_B_BASE=0x40000
BANK_B_RESERVED_START=0x74000
MAC_REGION_START=0x76000
FACTORY_REGION_START=0x77000
FLASH_END=0x80000
mkdir -p "$OUT_DIR"

[[ -f "$SAMPLE_DIR/board_8258_dongle.h" ]] || { echo "ERROR: complete V3.7.2.0-style 8258 fixture required" >&2; exit 2; }
[[ -f "$SDK/platform/boot/8258/boot_8258.link" ]] || { echo "ERROR: boot_8258.link missing" >&2; exit 2; }
[[ -f "$FINALIZER" ]] || { echo "ERROR: Telink finalizer missing" >&2; exit 2; }

roots=("$SDK/proj" "$SDK/platform" "$SDK/zigbee" "$COMMON_APP" "$SAMPLE_DIR")
includes=(-I"$FIXTURE" -I"$SAMPLE_DIR" -I"$COMMON_APP" -I"$SDK/proj")
while IFS= read -r -d '' d; do includes+=("-I$d"); done < <(find "${roots[@]}" -type d -print0 | sort -zu)

defs=(-DGLSD_TELINK_SDK -DMCU_CORE_8258=1 -DROUTER=1 -DMCU_STARTUP_8258=1)
telink_first=(-D_SIZE_T -D_SIZE_T_ -D__SIZE_T -D__SIZE_T__)
cflags=(-O2 -ffunction-sections -fdata-sections -fshort-enums -finline-small-functions -std=gnu99 -funsigned-char -fshort-wchar -fms-extensions -nostartfiles -nostdlib)
asflags=(-fomit-frame-pointer -fshort-enums -fdata-sections -ffunction-sections)

# flash_common.c references each supported vendor's lock/unlock helpers, so the
# corresponding TUs must remain available. Some of those TUs also carry unused
# OTP wrappers. glsd_telink_disabled_feature_glue.c resolves those generic OTP
# calls with non-mutating stubs solely so --gc-sections can discard the wrappers;
# the final ELF is rejected if any OTP-named symbol survives.
sdk_sources=(
 platform/boot/8258/cstartup_8258.S platform/boot/link_cfg.S platform/services/b85m/irq_handler.c platform/tc32/div_mod.S
 platform/chip_8258/flash.c platform/chip_8258/flash/flash_common.c platform/chip_8258/flash/flash_mid1060c8.c
 platform/chip_8258/flash/flash_mid1360c8.c platform/chip_8258/flash/flash_mid011460c8.c platform/chip_8258/flash/flash_mid134051.c
 platform/chip_8258/flash/flash_mid136085.c platform/chip_8258/flash/flash_mid1360eb.c platform/chip_8258/flash/flash_mid14325e.c
 platform/chip_8258/flash/flash_mid1460c8.c platform/chip_8258/flash/flash_mid13325e.c platform/chip_8258/adc.c
 proj/common/list.c proj/common/mempool.c proj/common/tlPrintf.c proj/common/string.c proj/common/utility.c
 proj/drivers/drv_gpio.c proj/drivers/drv_adc.c proj/drivers/drv_nv.c proj/drivers/drv_pm.c proj/drivers/drv_putchar.c
 proj/drivers/drv_timer.c proj/drivers/drv_uart.c proj/drivers/drv_calibration.c proj/drivers/drv_flash.c proj/drivers/drv_hw.c proj/drivers/drv_security.c
 proj/os/ev.c proj/os/ev_buffer.c proj/os/ev_poll.c proj/os/ev_queue.c proj/os/ev_timer.c proj/os/ev_rtc.c
 zigbee/bdb/bdb.c zigbee/aps/aps_group.c zigbee/mac/mac_phy.c zigbee/mac/mac_pib.c zigbee/zdo/zdp.c
 zigbee/zcl/zcl.c zigbee/zcl/zcl_nv.c zigbee/zcl/zcl_reporting.c zigbee/zcl/general/zcl_basic.c zigbee/zcl/general/zcl_identify.c
 zigbee/zcl/ota_upgrading/zcl_ota.c zigbee/zcl/ota_upgrading/zcl_ota_attr.c zigbee/common/zb_config.c zigbee/af/zb_af.c
 zigbee/ss/ss_nv.c zigbee/ota/ota.c zigbee/ota/otaEpCfg.c apps/common/main.c
)
app_sources=(glsd_stager_core.c glsd_stager_dispatch.c glsd_transport_adapter.c glsd_telink_sdk_adapter.c glsd_telink_stager_app.c glsd_telink_disabled_feature_glue.c)

compile_one() {
  local source="$1" obj="$2" base="$3" sdk_source="$4"
  local d=("${defs[@]}" "-DGLSD_STAGER_LINK_BASE=$base")
  local f=("${cflags[@]}")
  [[ "$sdk_source" == 1 ]] && f+=(-fpack-struct)
  mkdir -p "$(dirname "$obj")"
  case "$source" in
    *.S) "$TC32_CC" "${asflags[@]}" "${d[@]}" "${includes[@]}" -c "$source" -o "$obj" ;;
    *glsd_telink_sdk_adapter.c|*glsd_telink_stager_app.c|*glsd_telink_disabled_feature_glue.c)
      "$TC32_CC" "${f[@]}" "${d[@]}" "${telink_first[@]}" "${includes[@]}" -I"$SRC" -c "$source" -o "$obj" ;;
    *) "$TC32_CC" "${f[@]}" "${d[@]}" "${includes[@]}" -I"$SRC" -c "$source" -o "$obj" ;;
  esac
}

trace_dependency_origins() {
  local pattern='flash_erase_otp|flash_read_otp|flash_write_otp|touchlink_keyModeSet|touchlink_lqiThresholdSet|zclGpAttr_gpSharedSecKey|zclGpAttr_gpSharedSecKeyType|zcl_touchlink_register'
  local obj refs lib
  echo PRELINK_REFERENCE_ORIGINS
  for obj in "$@"; do
    refs="$($TC32_NM -u "$obj" 2>/dev/null | grep -E "$pattern" || true)"
    [[ -z "$refs" ]] || { echo "OBJECT=$obj"; printf '%s\n' "$refs"; }
  done
  for lib in "$SDK/platform/lib/libdrivers_8258.a" "$SDK/zigbee/lib/tc32/libzb_router.a"; do
    if [[ -f "$lib" ]]; then
      refs="$($TC32_NM -A -u "$lib" 2>/dev/null | grep -E "$pattern" || true)"
      [[ -z "$refs" ]] || { echo "ARCHIVE=$lib"; printf '%s\n' "$refs"; }
    fi
  done
  echo PRELINK_REFERENCE_ORIGINS_END
}

build_bank() {
  local label="$1"
  local base="$2"
  local dir="$OUT_DIR/$label"
  local objects=() rel src obj
  rm -rf "$dir"; mkdir -p "$dir/obj/sdk" "$dir/obj/app"

  for rel in "${sdk_sources[@]}"; do
    src="$SDK/$rel"; [[ -f "$src" ]] || { echo "ERROR: SDK source missing: $rel" >&2; exit 2; }
    obj="$dir/obj/sdk/${rel//\//_}.o"; compile_one "$src" "$obj" "$base" 1; objects+=("$obj")
  done
  for rel in "${app_sources[@]}"; do
    src="$SRC/$rel"; obj="$dir/obj/app/${rel%.c}.o"; compile_one "$src" "$obj" "$base" 0; objects+=("$obj")
  done

  trace_dependency_origins "${objects[@]}"

  local elf="$dir/glsd-stager-$label.elf"
  local bin="$dir/glsd-stager-$label.bin"
  local final_bin="$dir/glsd-stager-$label.final.bin"
  local map="$dir/glsd-stager-$label.map"
  "$TC32_LD" --gc-sections -nostartfiles -T"$SDK/platform/boot/8258/boot_8258.link" -Map="$map" \
    -L"$SDK/zigbee/lib/tc32" -L"$SDK/platform/lib" -o "$elf" "${objects[@]}" -ldrivers_8258 -lzb_router
  "$TC32_OBJCOPY" -O binary "$elf" "$bin"
  "$TC32_OBJDUMP" -h -t "$elf" > "$dir/glsd-stager-$label.lst"
  "$TC32_NM" -u "$elf" > "$dir/unresolved.txt" || true
  [[ ! -s "$dir/unresolved.txt" ]] || { echo "ERROR: unresolved symbols"; cat "$dir/unresolved.txt"; exit 1; }

  local raw_bytes final_bytes physical_end slot_end text_vma_hex text_vma
  raw_bytes="$(stat -c %s "$bin")"
  (( raw_bytes < APP_SLOT_SIZE )) || { echo "ERROR: raw image exceeds mechanics slot" >&2; exit 1; }

  # Prove the raw linker preamble, then produce and validate a transient inner
  # Telink application image with declared-size + xcrc32 fixed. No OTA wrapper.
  python3 "$FINALIZER" check-link "$bin" --max-final-size "$APP_SLOT_SIZE"
  python3 "$FINALIZER" finalize "$bin" "$final_bin" --max-final-size "$APP_SLOT_SIZE"
  python3 "$FINALIZER" check-final "$final_bin" --max-final-size "$APP_SLOT_SIZE"
  final_bytes="$(stat -c %s "$final_bin")"

  physical_end=$((base + final_bytes))
  slot_end=$((base + APP_SLOT_SIZE))
  (( physical_end < slot_end )) || { echo "ERROR: finalized image reaches reserved area" >&2; exit 1; }
  (( physical_end <= FLASH_END )) || { echo "ERROR: finalized image exceeds 512K flash" >&2; exit 1; }
  if (( base == BANK_B_BASE )); then
    (( physical_end < BANK_B_RESERVED_START )) || { echo "ERROR: bank-B image overlaps 0x74000+ reserved region" >&2; exit 1; }
    (( physical_end < MAC_REGION_START )) || { echo "ERROR: bank-B image reaches MAC region" >&2; exit 1; }
    (( physical_end < FACTORY_REGION_START )) || { echo "ERROR: bank-B image reaches factory region" >&2; exit 1; }
  fi

  # Telink boot_8258.link offsets .text by __FW_OFFSET. Record the VMA and later
  # require the bank-B .text VMA to be exactly +0x40000 from bank A. This proves
  # that the two builds are genuinely offset-linked, not just tagged differently.
  text_vma_hex="$("$TC32_OBJDUMP" -h "$elf" | awk '$2 == ".text" {print $4; exit}')"
  [[ "$text_vma_hex" =~ ^[0-9A-Fa-f]+$ ]] || { echo "ERROR: cannot parse .text VMA" >&2; exit 1; }
  text_vma=$((16#$text_vma_hex))
  (( text_vma >= base && text_vma < base + raw_bytes )) || {
    printf 'ERROR: .text VMA 0x%x outside expected bank image [0x%x,0x%x)\n' "$text_vma" "$base" "$((base + raw_bytes))" >&2
    exit 1
  }
  printf '%u\n' "$text_vma" > "$dir/text-vma.dec"

  {
    echo MECHANICS_ONLY=YES
    echo DEPLOYABLE=NO
    echo "BANK=$label"
    printf 'GLSD_STAGER_LINK_BASE=0x%05x\n' "$base"
    echo "RAW_BINARY_SIZE=$raw_bytes"
    echo "FINAL_INNER_BINARY_SIZE=$final_bytes"
    printf 'PHYSICAL_FLASH_START=0x%05x\n' "$base"
    printf 'PHYSICAL_FLASH_END_EXCLUSIVE=0x%05x\n' "$physical_end"
    printf 'APP_SLOT_END=0x%05x\n' "$slot_end"
    printf 'TEXT_VMA=0x%08x\n' "$text_vma"
    echo "TELINK_PREAMBLE=PASS"
    echo "TELINK_XCRC32=PASS"
    "$TC32_SIZE" "$elf"
    sha256sum "$elf" "$bin" "$final_bin" "$map"
    echo FINAL_OTP_SYMBOL_SCAN
    if "$TC32_NM" "$elf" | grep -Eai '(^|[[:space:]_])flash_(read|write|erase|lock)_otp'; then
      echo "ERROR: OTP path survived section GC" >&2; exit 1
    else echo NONE; fi
    echo APPLICATION_POWER_STAGE_AND_RESET_SCAN
    if "$TC32_NM" "$elf" | grep -Eai '(^|[[:space:]])(light_|led_|pwm_|factoryRst|factory_reset|bdb_networkSteerStart)'; then
      echo "ERROR: forbidden application light/PWM/reset/steering symbol reachable" >&2; exit 1
    else echo NONE; fi
    echo PRIVATE_EXTRACTION_MUTATION_IMPORT_SCAN
    if "$TC32_NM" -u "$dir/obj/app/"*.o | grep -Eai '(flash_write|flash_erase|nv_flashWrite|nv_reset|factory|leave|commission)'; then
      echo "ERROR: private extraction/app object imports mutation primitive" >&2; exit 1
    else echo NONE; fi
  } | tee "$dir/manifest.txt"
}

build_bank bank_a "$BANK_A_BASE"
build_bank bank_b "$BANK_B_BASE"

a_text="$(cat "$OUT_DIR/bank_a/text-vma.dec")"
b_text="$(cat "$OUT_DIR/bank_b/text-vma.dec")"
(( b_text - a_text == BANK_B_BASE - BANK_A_BASE )) || {
  printf 'ERROR: bank .text VMA delta is 0x%x, expected 0x40000\n' "$((b_text - a_text))" >&2
  exit 1
}
printf 'BANK_TEXT_VMA_DELTA=0x%05x\n' "$((b_text - a_text))"
echo RESERVED_FLASH_GEOMETRY=PASS
echo GLSD_TC32_FULL_LINK_PROBE=PASS_BOTH_BANKS
echo 'STOP: mechanics-only outputs are non-deployable and must not be packaged/served as OTA.'
