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
FILE_VERSION=0x7F050001

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
  "$CORE/glsd301p_timebase.c"
  "$CORE/glsd301p_uart_service.c"
  "$CORE/glsd301p_timer_events.c"
  "$CORE/glsd301p_control.c"
  "$CORE/glsd301p_identify.c"
  "$CORE/glsd301p_zcl_commands.c"
  "$CORE/glsd301p_rejoin.c"
  "$CORE/glsd301p_bdb_adapter.c"
  "$CORE/glsd301p_health.c"
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
    # Application TUs that bind SDK headers need the same size_t predefined
    # guard as the glsd301p_telink_* units; in src/ that set is the timer
    # ownership unit (ev_timer.h) and the shared ZCL command policy
    # (zcl_include.h for the pinned cluster command layouts/statuses).
    *glsd301p_telink_*.c|*src/glsd301p_timer_events.c|*src/glsd301p_zcl_commands.c)
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
if grep -q 'drv_uart_tx_start' "$TARGET/glsd301p_telink_target.c" "$CORE/glsd301p_control.c" "$CORE/glsd301p_uart_service.c"; then
  echo 'ERROR: blocking/allocating drv_uart_tx_start must not remain in target runtime (boot uses static DMA)' >&2; exit 1;
fi
[[ "$(grep -co 'uart_dma_send(g_uart_tx_dma)' "$TARGET/glsd301p_telink_target.c")" -eq 1 ]] || {
  echo 'ERROR: target must contain exactly one nonblocking DMA start (HW wrapper)' >&2; exit 1;
}
if grep -Eq 'while[[:space:]]*\([^)]*(uart|UART)' "$TARGET/glsd301p_telink_target.c" "$CORE/glsd301p_control.c" "$CORE/glsd301p_uart_service.c"; then
  echo 'ERROR: target UART runtime contains a polling loop' >&2; exit 1
fi
if grep -q 'TL_ZB_TIMER_SCHEDULE\|TL_ZB_TIMER_CANCEL\|ev_timer_taskPost\|ev_timer_taskCancel' \
    "$TARGET/glsd301p_telink_target.c" "$CORE/glsd301p_control.c" "$CORE/glsd301p_timer_events.c"; then
  echo 'ERROR: application must use owned static events, not pooled scheduling' >&2; exit 1;
fi
grep -q 'ev_on_timer' "$CORE/glsd301p_timer_events.c" || {
  echo 'ERROR: owned timer events must use direct ev_on_timer' >&2; exit 1;
}
grep -q 'ev_unon_timer' "$CORE/glsd301p_timer_events.c" || {
  echo 'ERROR: owned timer events must use direct ev_unon_timer' >&2; exit 1;
}
grep -q 'glsd301p_timebase_advance' "$CORE/glsd301p_timebase.c" || {
  echo 'ERROR: SDK timebase hook implementation missing' >&2; exit 1;
}
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

# M4: ZCL callbacks delegate to the shared harness-covered command policy;
# rejoin attempts flow through exactly one SDK start site with the documented
# ZDO_SUCCESS acceptance mapping and the owned one-shot 5 s pacer.
grep -q 'glsd301p_zcl_onoff_command' "$TARGET/glsd301p_telink_target.c" || {
  echo 'ERROR: OnOff callback is not delegated to shared command policy' >&2; exit 1;
}
grep -q 'glsd301p_zcl_level_command' "$TARGET/glsd301p_telink_target.c" || {
  echo 'ERROR: Level callback is not delegated to shared command policy' >&2; exit 1;
}
[[ "$(grep -co 'zb_rejoinReqWithBackOff' "$TARGET/glsd301p_telink_target.c")" -eq 1 ]] || {
  echo 'ERROR: target must contain exactly one rejoin start site' >&2; exit 1;
}
grep -q 'ZDO_SUCCESS' "$TARGET/glsd301p_telink_target.c" || {
  echo 'ERROR: rejoin acceptance mapping must use documented ZDO_SUCCESS' >&2; exit 1;
}
grep -q 'glsd301p_timer_retry_start_oneshot' "$TARGET/glsd301p_telink_target.c" || {
  echo 'ERROR: rejected rejoin starts must arm the owned one-shot pacer' >&2; exit 1;
}
grep -q 'glsd301p_rejoin_init' "$TARGET/glsd301p_telink_target.c" || {
  echo 'ERROR: rejoin ownership state is never initialized' >&2; exit 1;
}

# M5: RAM-only v2 health snapshot on Basic:0xFF10, read-only, refreshed by
# the owned 1 s event; development identity GLSD-ED-004 / 0x7F050001.
grep -q 'GLSD301P_HEALTH_ATTR_ID, ZCL_DATA_TYPE_OCTET_STR, ACCESS_CONTROL_READ, g_basic_health' \
  "$TARGET/glsd301p_telink_target.c" || {
  echo 'ERROR: Basic:0xFF10 health attribute entry missing or not read-only' >&2; exit 1;
}
if grep 'GLSD301P_HEALTH_ATTR_ID' "$TARGET/glsd301p_telink_target.c" | grep -q 'WRITE\|REPORTABLE'; then
  echo 'ERROR: health attribute must be read-only and never reported' >&2; exit 1;
fi
grep -q 'glsd301p_timer_health_start' "$TARGET/glsd301p_telink_target.c" || {
  echo 'ERROR: health snapshot is never refreshed by the owned 1 s event' >&2; exit 1;
}
grep -q 'glsd301p_health_note_bdb_status' "$TARGET/glsd301p_telink_target.c" || {
  echo 'ERROR: BDB status is never noted into health state' >&2; exit 1;
}
if grep -q 'nv_\|zcl_nv\|reportAttr\|zcl_report' "$CORE/glsd301p_health.c"; then
  echo 'ERROR: health snapshot must not touch NVM or reporting' >&2; exit 1;
fi
grep -q '#define FILE_VERSION[[:space:]]*0x7F050001' "$TARGET/version_cfg.h" || {
  echo 'ERROR: FILE_VERSION must be the allocated 0x7F050001' >&2; exit 1;
}
grep -q '#define APP_BUILD[[:space:]]*0x05' "$TARGET/version_cfg.h" || {
  echo 'ERROR: APP_BUILD must be the allocated 05' >&2; exit 1;
}
[[ "$FILE_VERSION" == '0x7F050001' ]] || {
  echo 'ERROR: build FILE_VERSION drifted from allocated identity' >&2; exit 1;
}

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

# R23: parsed foundation commands are allocated, filled, consumed and
# freed inside SDK-compiled code in both binaries; they cross to app
# code only as opaque attrCmd pointers. App sources must never
# dereference parsed-record internals (this is what makes the packed/
# unpacked host-vs-target layout delta harmless).
if grep -rEn 'attrCmd|attrList|pWriteCmd|pReportCmd|pCfgReport|pReadRspCmd|pReadReportCfg|->attrData' \
    "$CORE" "$TARGET" 2>/dev/null; then
  echo 'ERROR: app sources dereference parsed foundation internals' >&2; exit 1;
fi
# Harness fixtures may only NULL the incoming attrCmd slot, never read it.
if grep -rEn 'attrCmd' "$ROOT/tests" 2>/dev/null | grep -v 'attrCmd = NULL'; then
  echo 'ERROR: harness must treat attrCmd as opaque' >&2; exit 1;
fi

rm -rf "$DIR"
mkdir -p "$DIR/obj/sdk" "$DIR/obj/app" "$DIR/obj/abi"
objects=()

# Narrow deterministic SDK patches (hash-pinned, anchor-checked, provenance
# recorded). Idempotent: re-runs verify the patched hashes.
python3 "$ROOT/tools/apply_glsd301p_sdk_patches.py" --sdk-root "$SDK" \
  --provenance-out "$DIR/sdk-patches.json"

# Compile the real pinned SDK type through both translation-unit flag contexts.
# zcl_register() consumes this application-owned array, so a successful link is
# insufficient unless the layouts are byte-for-byte identical.
abi_probe_body='
#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "ev_timer.h"
#include "ev_buffer.h"
#define ABI_ASSERT(name, expr) typedef char name[(expr) ? 1 : -1]
ABI_ASSERT(glsd_zcl_spec_size, sizeof(zcl_specClusterInfo_t) == 18u);
ABI_ASSERT(glsd_zcl_spec_attr, __builtin_offsetof(zcl_specClusterInfo_t, attrTbl) == 6u);
ABI_ASSERT(glsd_zcl_spec_reg, __builtin_offsetof(zcl_specClusterInfo_t, clusterRegisterFunc) == 10u);
ABI_ASSERT(glsd_zcl_spec_cb, __builtin_offsetof(zcl_specClusterInfo_t, clusterAppCb) == 14u);
ABI_ASSERT(glsd_ev_timer_size, sizeof(ev_timer_event_t) == 28u);
ABI_ASSERT(glsd_ev_timer_next, __builtin_offsetof(ev_timer_event_t, next) == 0u);
ABI_ASSERT(glsd_ev_timer_cb, __builtin_offsetof(ev_timer_event_t, cb) == 4u);
ABI_ASSERT(glsd_ev_timer_data, __builtin_offsetof(ev_timer_event_t, data) == 8u);
ABI_ASSERT(glsd_ev_timer_timeout, __builtin_offsetof(ev_timer_event_t, timeout) == 12u);
ABI_ASSERT(glsd_ev_timer_period, __builtin_offsetof(ev_timer_event_t, period) == 16u);
ABI_ASSERT(glsd_ev_timer_cursystick, __builtin_offsetof(ev_timer_event_t, curSysTick) == 20u);
ABI_ASSERT(glsd_ev_timer_resv, __builtin_offsetof(ev_timer_event_t, resv) == 24u);
ABI_ASSERT(glsd_ev_timer_isbusy, __builtin_offsetof(ev_timer_event_t, isBusy) == 25u);
ABI_ASSERT(glsd_ev_timer_isrunning, __builtin_offsetof(ev_timer_event_t, isRunning) == 26u);
ABI_ASSERT(glsd_ev_timer_used, __builtin_offsetof(ev_timer_event_t, used) == 27u);
/* R23: P5 foundation record layouts (packed target ABI). */
ABI_ASSERT(glsd_write_rec_size, sizeof(zclWriteRec_t) == 7u);
ABI_ASSERT(glsd_write_rec_attr, __builtin_offsetof(zclWriteRec_t, attrID) == 0u);
ABI_ASSERT(glsd_write_rec_type, __builtin_offsetof(zclWriteRec_t, dataType) == 2u);
ABI_ASSERT(glsd_write_rec_data, __builtin_offsetof(zclWriteRec_t, attrData) == 3u);
ABI_ASSERT(glsd_write_cmd_size, sizeof(zclWriteCmd_t) == 1u);
ABI_ASSERT(glsd_report_rec_size, sizeof(zclReport_t) == 7u);
ABI_ASSERT(glsd_report_cmd_size, sizeof(zclReportCmd_t) == 1u);
ABI_ASSERT(glsd_write_rsp_size, sizeof(zclWriteRspStatus_t) == 3u);
ABI_ASSERT(glsd_write_rsp_status, __builtin_offsetof(zclWriteRspStatus_t, status) == 0u);
ABI_ASSERT(glsd_write_rsp_attr, __builtin_offsetof(zclWriteRspStatus_t, attrID) == 1u);
ABI_ASSERT(glsd_read_rsp_size, sizeof(zclReadRspStatus_t) == 8u);
ABI_ASSERT(glsd_read_rsp_attr, __builtin_offsetof(zclReadRspStatus_t, attrID) == 0u);
ABI_ASSERT(glsd_read_rsp_status, __builtin_offsetof(zclReadRspStatus_t, status) == 2u);
ABI_ASSERT(glsd_read_rsp_type, __builtin_offsetof(zclReadRspStatus_t, dataType) == 3u);
ABI_ASSERT(glsd_read_rsp_data, __builtin_offsetof(zclReadRspStatus_t, data) == 4u);
ABI_ASSERT(glsd_cfg_rec_size, sizeof(zclCfgReportRec_t) == 14u);
ABI_ASSERT(glsd_cfg_rec_dir, __builtin_offsetof(zclCfgReportRec_t, direction) == 0u);
ABI_ASSERT(glsd_cfg_rec_attr, __builtin_offsetof(zclCfgReportRec_t, attrID) == 1u);
ABI_ASSERT(glsd_cfg_rec_type, __builtin_offsetof(zclCfgReportRec_t, dataType) == 3u);
ABI_ASSERT(glsd_cfg_rec_min, __builtin_offsetof(zclCfgReportRec_t, minReportInt) == 4u);
ABI_ASSERT(glsd_cfg_rec_max, __builtin_offsetof(zclCfgReportRec_t, maxReportInt) == 6u);
ABI_ASSERT(glsd_cfg_rec_timeout, __builtin_offsetof(zclCfgReportRec_t, timeoutPeriod) == 8u);
ABI_ASSERT(glsd_cfg_rec_change, __builtin_offsetof(zclCfgReportRec_t, reportableChange) == 10u);
ABI_ASSERT(glsd_cfg_cmd_size, sizeof(zclCfgReportCmd_t) == 1u);
ABI_ASSERT(glsd_cfg_rsp_size, sizeof(zclCfgReportStatus_t) == 4u);
ABI_ASSERT(glsd_cfg_rsp_status, __builtin_offsetof(zclCfgReportStatus_t, status) == 0u);
ABI_ASSERT(glsd_cfg_rsp_dir, __builtin_offsetof(zclCfgReportStatus_t, direction) == 1u);
ABI_ASSERT(glsd_cfg_rsp_attr, __builtin_offsetof(zclCfgReportStatus_t, attrID) == 2u);
ABI_ASSERT(glsd_readcfg_rec_size, sizeof(zclReadReportCfgRec_t) == 3u);
ABI_ASSERT(glsd_readcfg_rec_dir, __builtin_offsetof(zclReadReportCfgRec_t, direction) == 0u);
ABI_ASSERT(glsd_readcfg_rec_attr, __builtin_offsetof(zclReadReportCfgRec_t, attrID) == 1u);
ABI_ASSERT(glsd_readcfgrsp_rec_size, sizeof(zclReportCfgRspRec_t) == 15u);
ABI_ASSERT(glsd_readcfgrsp_rec_status, __builtin_offsetof(zclReportCfgRspRec_t, status) == 0u);
ABI_ASSERT(glsd_readcfgrsp_rec_dir, __builtin_offsetof(zclReportCfgRspRec_t, direction) == 1u);
ABI_ASSERT(glsd_readcfgrsp_rec_attr, __builtin_offsetof(zclReportCfgRspRec_t, attrID) == 2u);
ABI_ASSERT(glsd_readcfgrsp_rec_type, __builtin_offsetof(zclReportCfgRspRec_t, dataType) == 4u);
ABI_ASSERT(glsd_readcfgrsp_rec_min, __builtin_offsetof(zclReportCfgRspRec_t, minReportInt) == 5u);
ABI_ASSERT(glsd_readcfgrsp_rec_max, __builtin_offsetof(zclReportCfgRspRec_t, maxReportInt) == 7u);
ABI_ASSERT(glsd_readcfgrsp_rec_timeout, __builtin_offsetof(zclReportCfgRspRec_t, timeoutPeriod) == 9u);
ABI_ASSERT(glsd_readcfgrsp_rec_change, __builtin_offsetof(zclReportCfgRspRec_t, reportableChange) == 11u);
/* R23: pool geometry plus 255-cap allocation thresholds. The u16 parsed
 * lengths never wrap at the P5 record cap, and the maxima exceed
 * LARGE_BUFFER, so oversized parses fail closed with
 * INSUFFICIENT_SPACE (proved behaviorally by pool-exhaustion tests). */
ABI_ASSERT(glsd_pool_g0, BUFFER_GROUP_0 == 24);
ABI_ASSERT(glsd_pool_g1, BUFFER_GROUP_1 == 60);
ABI_ASSERT(glsd_pool_g2, BUFFER_GROUP_2 == 152);
ABI_ASSERT(glsd_pool_g3, BUFFER_GROUP_3 == 512);
ABI_ASSERT(glsd_pool_large, LARGE_BUFFER == 504);
ABI_ASSERT(glsd_alloc_write_u16, sizeof(zclWriteCmd_t) + 255u * sizeof(zclWriteRec_t) <= 65535u);
ABI_ASSERT(glsd_alloc_write_failclosed, sizeof(zclWriteCmd_t) + 255u * sizeof(zclWriteRec_t) > LARGE_BUFFER);
ABI_ASSERT(glsd_alloc_readrsp_u16, sizeof(zclReadRspCmd_t) + 255u * sizeof(zclReadRspStatus_t) <= 65535u);
ABI_ASSERT(glsd_alloc_readrsp_failclosed, sizeof(zclReadRspCmd_t) + 255u * sizeof(zclReadRspStatus_t) > LARGE_BUFFER);
ABI_ASSERT(glsd_alloc_cfg_u16, sizeof(zclCfgReportCmd_t) + 255u * sizeof(zclCfgReportRec_t) <= 65535u);
ABI_ASSERT(glsd_alloc_cfg_failclosed, sizeof(zclCfgReportCmd_t) + 255u * sizeof(zclCfgReportRec_t) > LARGE_BUFFER);
ABI_ASSERT(glsd_alloc_readcfgrsp_u16, sizeof(zclReadReportCfgRspCmd_t) + 255u * sizeof(zclReportCfgRspRec_t) <= 65535u);
ABI_ASSERT(glsd_alloc_readcfgrsp_failclosed, sizeof(zclReadReportCfgRspCmd_t) + 255u * sizeof(zclReportCfgRspRec_t) > LARGE_BUFFER);
ABI_ASSERT(glsd_alloc_single_small, sizeof(zclWriteCmd_t) + sizeof(zclWriteRec_t) + 2u <= BUFFER_GROUP_0);
int glsd301p_abi_probe(void) { return (int)sizeof(zcl_specClusterInfo_t); }
'
printf '%s' "$abi_probe_body" > "$DIR/sdk_abi_probe.c"
printf '%s' "$abi_probe_body" > "$DIR/glsd301p_telink_abi_probe.c"
compile_one "$DIR/sdk_abi_probe.c" "$DIR/obj/abi/sdk-context.o" 1
compile_one "$DIR/glsd301p_telink_abi_probe.c" "$DIR/obj/abi/app-context.o" 0
echo 'ZCL_SPEC_CLUSTER_INFO_ABI=size18,attrTbl@6,register@10,appCb@14'
echo 'EV_TIMER_EVENT_ABI=size28,next@0,cb@4,data@8,timeout@12,period@16,curSysTick@20,resv@24,isBusy@25,isRunning@26,used@27'
echo 'FOUNDATION_RECORD_ABI=write7,cmd1,report7,writersp3,readrsp8,cfg14,cfgrsp4,readcfg3,readcfgrsp15'
echo 'POOL_ALLOC_ABI=groups24/60/152/512,large504,cap255-u16safe,failclosed-above-504'

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
control_obj="$DIR/obj/app/glsd301p_control.o"
service_obj="$DIR/obj/app/glsd301p_uart_service.o"
timers_obj="$DIR/obj/app/glsd301p_timer_events.o"
"$TC32_NM" "$app_obj" | grep -Eq ' T user_init$' || { echo 'ERROR: target user_init missing' >&2; exit 1; }
"$TC32_NM" "$app_obj" | grep -Eq ' T glsd301p_hw_uart_send_frame$' || { echo 'ERROR: static-DMA UART wrapper missing' >&2; exit 1; }
"$TC32_NM" -u "$app_obj" | grep -Eq ' U uart_dma_send$' || { echo 'ERROR: nonblocking runtime UART dependency missing' >&2; exit 1; }
if "$TC32_NM" -u "$app_obj" | grep -Eq ' U drv_uart_tx_start$'; then
  echo 'ERROR: blocking boot UART dependency must be gone' >&2; exit 1;
fi
# M4: the target reaches control emit through the shared ZCL command policy
# (target -> glsd301p_zcl_*_command -> glsd301p_control_emit); the two M4
# nm gates below pin both edges of that chain instead of a direct edge.
"$TC32_NM" -u "$app_obj" | grep -Eq ' U glsd301p_timer_io_start$' || {
  echo 'ERROR: target is not wired through owned timer events' >&2; exit 1;
}
"$TC32_NM" -u "$control_obj" | grep -Eq ' U glsd301p_runtime_core_(apply_state|poll_push_ex|poll_pb4)' || {
  echo 'ERROR: control plane is not wired through guarded runtime core' >&2; exit 1;
}
"$TC32_NM" -u "$service_obj" | grep -Eq ' U glsd301p_hw_uart_send_frame$' || {
  echo 'ERROR: UART service is not wired through nonblocking HW wrapper' >&2; exit 1;
}
"$TC32_NM" -u "$timers_obj" | grep -Eq ' U ev_on_timer$' || {
  echo 'ERROR: owned events are not wired through direct ev_on_timer' >&2; exit 1;
}
zclcmd_obj="$DIR/obj/app/glsd301p_zcl_commands.o"
rejoin_obj="$DIR/obj/app/glsd301p_rejoin.o"
adapter_obj="$DIR/obj/app/glsd301p_bdb_adapter.o"
"$TC32_NM" -u "$app_obj" | grep -Eq ' U glsd301p_zcl_(onoff|level)_command$' || {
  echo 'ERROR: target is not wired through shared ZCL command policy' >&2; exit 1;
}
"$TC32_NM" -u "$app_obj" | grep -Eq ' U glsd301p_bdb_handle_event$' || {
  echo 'ERROR: target is not wired through the shared BDB adapter' >&2; exit 1;
}
for sym in glsd301p_rejoin_note_joined glsd301p_rejoin_note_parent_lost glsd301p_rejoin_note_rejoin_failure glsd301p_rejoin_note_init_failure; do
  "$TC32_NM" -u "$adapter_obj" | grep -Eq " U $sym\$" || {
    echo "ERROR: BDB adapter does not drive rejoin ownership ($sym)" >&2; exit 1;
  }
done
"$TC32_NM" -u "$zclcmd_obj" | grep -Eq ' U glsd301p_control_(emit|level_start_target|level_start_move|level_cancel)$' || {
  echo 'ERROR: ZCL command policy is not wired through guarded control plane' >&2; exit 1;
}
"$TC32_NM" -u "$rejoin_obj" | grep -Eq ' U glsd301p_sat_inc_u32$' || {
  echo 'ERROR: rejoin counters are not wired through saturating increment' >&2; exit 1;
}
"$TC32_NM" -u "$app_obj" | grep -Eq ' U glsd301p_health_snapshot$' || {
  echo 'ERROR: target never refreshes the RAM health snapshot' >&2; exit 1;
}
"$TC32_NM" -u "$app_obj" | grep -Eq ' U T_evtExcept$' || {
  echo 'ERROR: health exception record is not wired to SDK ev.c' >&2; exit 1;
}
for obj in "$DIR"/obj/app/*.o; do
  if "$TC32_NM" -u "$obj" 2>/dev/null | grep -Eq ' U ev_timer_task(Post|Cancel)$'; then
    echo "ERROR: pooled timer API referenced by application object: $obj" >&2; exit 1;
  fi
done
echo 'APP_TIMER_API=OWNED_STATIC_DIRECT_ONLY'

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
  glsd301p_timebase_advance \
  glsd301p_control_io_step \
  glsd301p_control_emit \
  glsd301p_timer_io_start \
  glsd301p_timer_retry_start_oneshot \
  glsd301p_zcl_onoff_command \
  glsd301p_zcl_level_command \
  glsd301p_rejoin_init \
  glsd301p_health_snapshot \
  glsd301p_timer_health_start \
  glsd301p_hw_uart_send_frame \
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
  echo EV_TIMER_EVENT_ABI=size28_native_static_owned
  echo APP_TIMER_API=OWNED_STATIC_DIRECT_ONLY
  echo UART_RUNTIME_TRANSPORT=NONBLOCKING_OFF_PRIORITY_ELAPSED_DEADLINE
  echo UART_BOOT=STATIC_DMA_SINGLE_ATTEMPT_DEFERRED_ARM
  echo ZCL_COMMAND_POLICY=SHARED_DISPATCH_HARNESSED
  echo REJOIN=OWNED_SINGLE_ATTEMPT_ZDO_SUCCESS_MAPPED_ONESHOT_RETRY_5S
  echo HEALTH_SNAPSHOT=RAM_V2_48B_BASIC_0xFF10_READONLY_1S_OWNED
  echo DEV_IDENTITY=GLSD-ED-004_APP_BUILD_05_FILE_VERSION_0x7F050001_DATE_20261005
  python3 - "$DIR/sdk-patches.json" <<'PY'
import json, sys
report = json.load(open(sys.argv[1]))
for p in report["patches"]:
    print("SDK_PATCH_%s_ORIG=%s" % (p["id"], p["original_sha256"]))
    print("SDK_PATCH_%s_PATCHED=%s" % (p["id"], p["patched_sha256"]))
PY
  echo "RAW_BINARY_SIZE=$raw_bytes"
  echo "FINAL_BINARY_SIZE=$final_bytes"
  printf 'TEXT_VMA=0x%08x\n' "$text_vma"
  printf 'PHYSICAL_BANK_B_END_EXCLUSIVE=0x%05x\n' "$physical_b_end"
  "$TC32_SIZE" "$elf"
  sha256sum "$elf" "$raw" "$final" "$map"
  sha512sum "$elf" "$raw" "$final" "$map"
} | tee "$DIR/manifest.txt"

echo GLSD301P_TC32_END_DEVICE_LINK=PASS
echo GLSD301P_TARGET_ARTIFACT=QUARANTINED_NOT_DEPLOYABLE
