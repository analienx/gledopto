/*
 * Telink TLSR8258 stack glue for features deliberately excluded from the
 * extraction stager's endpoint/application surface.
 *
 * Telink's prebuilt router library retains several application-owned hooks for
 * TouchLink and Green Power even when this application does not advertise or
 * initialize those features.  Providing explicit inert definitions is safer
 * and smaller than linking the complete optional feature implementations.
 *
 * Some public flash-vendor compatibility TUs also contain unused OTP wrappers.
 * Their normal lock/unlock functions are required by flash_common.c, so the
 * generic OTP calls are resolved here with non-mutating stubs. Link-time GC
 * must remove these stubs and every vendor OTP wrapper from the final ELF; CI
 * enforces that property.
 */

#ifdef GLSD_TELINK_SDK

#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "dGP_stub.h"
#include "zcl_zll_commissioning.h"

/* TouchLink is disabled: keep the library-visible inter-PAN state false. */
u8 deviceInfoRsp = 0u;

/*
 * Router builds compile Green Power support into Telink's stack library. The
 * stager does not expose a GP endpoint/cluster and supplies an all-zero shared
 * key with the public SDK's NO_KEY value (0). Any GP announce is rejected.
 */
u8 zclGpAttr_gpSharedSecKeyType = 0u;
u8 zclGpAttr_gpSharedSecKey[SEC_KEY_LEN] = {0};

static bool glsd_reject_gp_device_announce(u16 sinkNwkAddr, addrExt_t sinkIeeeAddr) {
    (void)sinkNwkAddr;
    (void)sinkIeeeAddr;
    return FALSE;
}

gpDeviceAnnounceCheckCb_t g_gpDeviceAnnounceCheckCb = glsd_reject_gp_device_announce;

/* BDB's router archive references these hooks even when TOUCHLINK_SUPPORT=0. */
void touchlink_keyModeSet(u8 keyType, u8 *key) {
    (void)keyType;
    (void)key;
}

void touchlink_lqiThresholdSet(u8 lqi) {
    (void)lqi;
}

status_t zcl_touchlink_register(u8 endpoint, const zcl_touchlinkAppCallbacks_t *cb) {
    (void)endpoint;
    (void)cb;
    return ZCL_STA_UNSUP_CLUSTER_COMMAND;
}

/*
 * Never mutate flash security/OTP registers. These exist solely to satisfy
 * references in unused vendor-specific wrapper sections before --gc-sections.
 * A surviving symbol in the final ELF is treated as a build failure.
 */
void flash_read_otp(unsigned long addr, unsigned long len, unsigned char *buf) {
    unsigned long i;
    (void)addr;
    if (buf == NULL) {
        return;
    }
    for (i = 0; i < len; ++i) {
        buf[i] = 0xFFu;
    }
}

void flash_write_otp(unsigned long addr, unsigned long len, unsigned char *buf) {
    (void)addr;
    (void)len;
    (void)buf;
}

void flash_erase_otp(unsigned long addr) {
    (void)addr;
}

#endif /* GLSD_TELINK_SDK */
