#pragma once

/*
 * Host-harness stand-in for the SDK zigbee/zcl/zcl_include.h.
 *
 * CI stages a shadow tree (build/host-sdk/zigbee/...) holding byte-identical
 * copies of the pinned SDK ZCL headers plus the P2-patched zcl_level.c and
 * zcl_onoff.c bodies; this file is staged as
 * build/host-sdk/zigbee/zcl/zcl_include.h so both the SDK bodies and the
 * shared command policy (src/glsd301p_zcl_commands.c) keep their own
 * #include "zcl_include.h" spelling. The real SDK headers (zcl_config.h,
 * zcl_const.h, zcl.h, aps/aps_api.h, af/zb_af.h, general/zcl_level.h,
 * general/zcl_onoff.h, common/types.h, common/utility.h, common/bit.h)
 * are used unmodified.
 *
 * The ZCL_*_SUPPORT selection below mirrors firmware/glsd301p-ed/app_cfg.h
 * exactly (CI asserts value-for-value equality), so the real zcl_config.h
 * derives the identical bare-cluster macro set as the target build
 * (ZCL_LEVEL_CTRL/ZCL_ON_OFF defined, ZCL_SCENE undefined).
 * _attribute_packed_/_attribute_aligned_ match the SDK compiler.h
 * spellings; _CODE_ZCL_ is empty on the host (section placement has no
 * logic effect). Only the ZCL stack seams the SDK bodies call are stubbed
 * (zcl_registerCluster, zcl_getAttrVal, zcl_setAttrVal, zcl_sendCmd); the
 * stubs live in the dispatch test with scripted behavior.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * The real types.h spells NULL as 0; undef first so including this shim
 * after the system headers cannot trip -Werror on macro redefinition.
 * stddef.h is deliberately absent (string.h already provides size_t).
 */
#ifdef NULL
#undef NULL
#endif

#include "common/types.h"
#include "common/utility.h"
#include "common/bit.h"

/*
 * Real OS headers (staged): the reporting body needs the timer API and
 * the exception-post macro, which the target chain provides through
 * zb_common.h. Requires -I<tree>/proj/os on every compile unit that
 * parses this shim.
 */
#include "ev_timer.h"
#include "ev.h"

/*
 * zb_common.h / nwk.h fragments the staged bodies need but whose full
 * headers drag the MAC/NWK/ZDO/BDB archives. Each value is an exact
 * copy at the pinned commit (tl_zigbee_sdk d5bc2f7b0c1f8536fe21c8127ca680ea8214bc8e):
 * TL_SETSTRUCTCONTENT + EXT_ADDR_LEN + ZB_64BIT_ADDR_COPY from
 * zigbee/common/includes/zb_common.h, NWK_BROADCAST_ROUTER_COORDINATOR
 * from zigbee/nwk/includes/nwk.h, TL_SCHEDULE_TASK from
 * zigbee/common/includes/zb_task_queue.h.
 */
#define TL_SETSTRUCTCONTENT(s, v) (memset((u8 *)&s, v, sizeof(s)))
#define NWK_BROADCAST_ROUTER_COORDINATOR 0xFFFCu
#define EXT_ADDR_LEN 8
#define ZB_64BIT_ADDR_COPY(dst, src) (memcpy(dst, src, EXT_ADDR_LEN))
#define ZB_IEEE_ADDR_COPY ZB_64BIT_ADDR_COPY
typedef void (*tl_zb_callback_t)(void *arg);
u8 tl_zbTaskPost(tl_zb_callback_t func, void *arg);
#define TL_SCHEDULE_TASK tl_zbTaskPost

/*
 * Head of the generic return-code enum in
 * zigbee/common/includes/zb_common.h at the pinned commit. Only RET_OK
 * is named by the staged bodies; the rest of the enum is not mirrored
 * because nothing in the harness can produce it.
 */
#define RET_OK 0

/*
 * Stack diagnostics block (exact member mirror of sys_diagnostics_t in
 * zigbee/common/includes/zb_common.h at the pinned commit). The read
 * handler stamps last-message LQI/RSSI here; the harness owns the
 * storage since the ZB archive is not linked.
 */
/*
 * The root dispatcher is SDK-internal (no public declaration); the
 * harness drives it synchronously instead of the async AF task path.
 * Signature mirrors the definition in zcl.c at the pinned commit.
 */
void zcl_cmdHandler(void *pCmd);

typedef struct {
    u16 numberOfResets;
    u16 persistentMemoryWrites;
    u32 macRxCrcFail;
    u32 macTxCcaFail;
    u32 macRxBcast;
    u32 macTxBcast;
    u32 macRxUcast;
    u32 macTxUcast;
    u16 macTxUcastRetry;
    u16 macTxUcastFail;
    u16 nwkTxCnt;
    u16 nwkTxEnDecryptFail;
    u16 apsRxBcast;
    u16 apsTxBcast;
    u16 apsRxUcast;
    u16 apsTxUcastSuccess;
    u16 apsTxUcastRetry;
    u16 apsTxUcastFail;
    u16 routeDiscInitiated;
    u16 neighborAdded;
    u16 neighborRemoved;
    u16 neighborStale;
    u16 joinIndication;
    u16 childMoved;
    u32 panIdConflictCheck;
    u16 nwkFCFailure;
    u16 apsFCFailure;
    u16 apsUnauthorizedKey;
    u16 nwkDecryptFailures;
    u16 apsDecryptFailures;
    u16 packetBufferAllocateFailures;
    u16 relayedUcast;
    u16 phytoMACqueuelimitreached;
    u16 packetValidateDropCount;
    u8 lastMessageLQI;
    s8 lastMessageRSSI;
    u8 macTxIrqTimeoutCnt;
    u8 macTxIrqCnt;
    u8 macRxIrqCnt;
    u8 phyLengthError;
    u8 panIdConflict;
    u8 panIdModified;
    u8 nwkAddrConflict;
} sys_diagnostics_t;
extern sys_diagnostics_t g_sysDiags;

/* SDK compiler.h spellings (verified at the pinned commit). */
#define _attribute_packed_ __attribute__((packed))
#define _attribute_aligned_(s) __attribute__((aligned(s)))
#define _CODE_ZCL_

/* Cluster selection: value-for-value mirror of app_cfg.h (CI-checked). */
#define ZCL_POWER_CFG_SUPPORT                   0
#define ZCL_DEV_TEMPERATURE_CFG_SUPPORT         0
#define ZCL_GROUP_SUPPORT                       1
#define ZCL_SCENE_SUPPORT                       0
#define ZCL_ON_OFF_SUPPORT                      1
#define ZCL_ON_OFF_SWITCH_CFG_SUPPORT           0
#define ZCL_LEVEL_CTRL_SUPPORT                  1
#define ZCL_ALARMS_SUPPORT                      0
#define ZCL_TIME_SUPPORT                        0
#define ZCL_RSSI_LOCATION_SUPPORT               0
#define ZCL_DIAGNOSTICS_SUPPORT                 0
#define ZCL_POLL_CTRL_SUPPORT                   0
#define ZCL_GP_SUPPORT                          0
#define ZCL_BINARY_INPUT_SUPPORT                0
#define ZCL_BINARY_OUTPUT_SUPPORT               0
#define ZCL_MULTISTATE_INPUT_SUPPORT            0
#define ZCL_MULTISTATE_OUTPUT_SUPPORT           0
#define ZCL_ILLUMINANCE_MEASUREMENT_SUPPORT     0
#define ZCL_ILLUMINANCE_LEVEL_SENSING_SUPPORT   0
#define ZCL_TEMPERATURE_MEASUREMENT_SUPPORT     0
#define ZCL_OCCUPANCY_SENSING_SUPPORT           0
#define ZCL_ELECTRICAL_MEASUREMENT_SUPPORT      0
#define ZCL_LIGHT_COLOR_CONTROL_SUPPORT         0
#define ZCL_THERMOSTAT_SUPPORT                  0
#define ZCL_DOOR_LOCK_SUPPORT                   0
#define ZCL_WINDOW_COVERING_SUPPORT             0
#define ZCL_IAS_ZONE_SUPPORT                    0
#define ZCL_IAS_ACE_SUPPORT                     0
#define ZCL_IAS_WD_SUPPORT                      0
#define ZCL_METERING_SUPPORT                    0
#define ZCL_OTA_SUPPORT                         1
#define ZCL_ZLL_COMMISSIONING_SUPPORT           0
#define ZCL_WWAH_SUPPORT                        0

#include "aps/aps_api.h"

/*
 * Narrow seam model for the AF data-indication wrapper (real zb_api.h at
 * the pinned commit spells it exactly so; zb_api.h itself cannot be
 * staged because it drags the MAC/NWK/ZDO/BDB stacks). indInfo is the
 * first member and is the REAL staged aps_data_ind_t, so every offset
 * the staged SDK bodies touch (msg->indInfo.*, asduLen, asdu) is exact.
 * Root-dispatch tests pool-allocate this wrapper (the real root frees
 * the message with ev_buf_free) with an exact-size asdu tail, so parser
 * overreads trip ASan deterministically. This precedes zcl.h, which
 * names the wrapper in zclIncoming_t.
 */
typedef struct apsdeDataInd_s {
    aps_data_ind_t indInfo;
    u16 asduLen;
    u8 asdu[];
} apsdeDataInd_t;

#include "af/zb_af.h"
#include "zcl_config.h"
#include "zcl_const.h"
#include "zcl.h"
#include "general/zcl_basic.h"
#include "general/zcl_level.h"
#include "general/zcl_onoff.h"
#include "general/zcl_identify.h"
#include "general/zcl_group.h"
#include "ota_upgrading/zcl_ota.h"

/*
 * Declarations for the seams the staged bodies call that live outside the
 * staged set (opaque archives, MCU/NV drivers, OTA core). Signatures mirror
 * the pinned SDK; scripted definitions live in the dispatch test.
 * NV_SUCC is the first nv_sts_t enumerator at the pinned commit
 * (proj/drivers/drv_nv.h), i.e. 0.
 */
typedef enum {
    NV_SUCC_SHIM = 0
} nv_sts_t_shim;
#define NV_SUCC 0
nv_sts_t_shim zcl_reportingTab_save(void);
nv_sts_t_shim zcl_reportingTab_restore(void);
void *zb_bindingTblSearched(u16 clusterId, u8 endpoint);
bool zb_isDeviceJoinedNwk(void);
u32 zb_random(void);
void ota_upgradeAbort(void);
