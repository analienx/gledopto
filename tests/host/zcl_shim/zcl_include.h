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
 * general/zcl_onoff.h, common/types.h, common/utility.h) are used
 * unmodified.
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
#include "af/zb_af.h"
#include "zcl_config.h"
#include "zcl_const.h"
#include "zcl.h"
#include "general/zcl_level.h"
#include "general/zcl_onoff.h"

/*
 * Narrow seam model for the AF data-indication wrapper (real zb_api.h at
 * the pinned commit spells it exactly so; zb_api.h itself cannot be
 * staged because it drags the MAC/NWK/ZDO/BDB stacks). indInfo is the
 * first member and is the REAL staged aps_data_ind_t, so every offset
 * the staged SDK bodies touch (msg->indInfo.dst_ep) is exact. The
 * flexible asdu tail is never read through msg: payloads arrive via
 * pData in exact-size ASan buffers owned by the test.
 */
typedef struct apsdeDataInd_s {
    aps_data_ind_t indInfo;
    u16 asduLen;
    u8 asdu[];
} apsdeDataInd_t;
