/*
 * Link-only closure for optional Telink SDK features that are deliberately not
 * part of the GL-SD-301P-ED product surface.
 *
 * The final product uses the End Device stack archive. Touchlink and Green
 * Power are disabled in app_cfg.h. These inert hooks exist only where Telink's
 * generic BDB/library objects retain application-owned references. OTP helpers
 * likewise exist only to allow section GC to discard unused vendor wrappers.
 */

#ifdef GLSD_TELINK_SDK

#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "dGP_stub.h"
#include "zcl_zll_commissioning.h"

u8 deviceInfoRsp = 0u;
u8 zclGpAttr_gpSharedSecKeyType = 0u;
u8 zclGpAttr_gpSharedSecKey[SEC_KEY_LEN] = {0};

static bool glsd_reject_gp_device_announce(u16 sinkNwkAddr, addrExt_t sinkIeeeAddr)
{
    (void)sinkNwkAddr;
    (void)sinkIeeeAddr;
    return FALSE;
}

gpDeviceAnnounceCheckCb_t g_gpDeviceAnnounceCheckCb = glsd_reject_gp_device_announce;

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

void flash_read_otp(unsigned long addr, unsigned long len, unsigned char *buf)
{
    unsigned long i;
    (void)addr;
    if (buf == NULL) {
        return;
    }
    for (i = 0; i < len; ++i) {
        buf[i] = 0xFFu;
    }
}

void flash_write_otp(unsigned long addr, unsigned long len, unsigned char *buf)
{
    (void)addr;
    (void)len;
    (void)buf;
}

void flash_erase_otp(unsigned long addr)
{
    (void)addr;
}

#endif /* GLSD_TELINK_SDK */
