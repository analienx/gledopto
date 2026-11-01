/*
 * Application-owned closure for Telink hooks retained by the prebuilt stack
 * even though this GL-SD-301P target deliberately disables Touchlink.
 *
 * These definitions are intentionally inert. They must never create a ZLL
 * commissioning surface or mutate security state. CI separately proves
 * TOUCHLINK_SUPPORT=0 and ZCL_ZLL_COMMISSIONING_SUPPORT=0.
 */

#include "tl_common.h"
#include "zcl_include.h"
#include "zcl_zll_commissioning.h"
#include "drv_gpio.h"

#include "glsd301p_telink_pin_contract.h"

u8 deviceInfoRsp = 0u;

void touchlink_keyModeSet(u8 keyType, u8 *key)
{
    (void)keyType;
    (void)key;
}

void touchlink_lqiThresholdSet(u8 lqi)
{
    (void)lqi;
}

status_t zcl_touchlink_register(u8 endpoint, const zcl_touchlinkAppCallbacks_t *cb)
{
    (void)endpoint;
    (void)cb;
    return ZCL_STA_UNSUP_CLUSTER_COMMAND;
}
