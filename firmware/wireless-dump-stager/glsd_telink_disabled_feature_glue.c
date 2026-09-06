/*
 * Telink TLSR8258 stack glue for features deliberately disabled by the
 * extraction stager.
 *
 * The public router library retains references to two application-owned hooks
 * even when TouchLink / Green Power are not part of the endpoint surface:
 *   - deviceInfoRsp (TouchLink inter-PAN state)
 *   - g_gpDeviceAnnounceCheckCb (Green Power device-announce filter)
 *
 * Supplying inert definitions is narrower than linking the full disabled
 * feature implementations.  If the GP hook is reached unexpectedly it rejects
 * the announce instead of dereferencing NULL or enabling GP behavior.
 */

#ifdef GLSD_TELINK_SDK

#include "tl_common.h"
#include "zb_api.h"
#include "dGP_stub.h"

/* TouchLink is disabled. Keep the library-visible inter-PAN state false. */
u8 deviceInfoRsp = 0u;

static bool glsd_reject_gp_device_announce(u16 sinkNwkAddr, addrExt_t sinkIeeeAddr) {
    (void)sinkNwkAddr;
    (void)sinkIeeeAddr;
    return FALSE;
}

gpDeviceAnnounceCheckCb_t g_gpDeviceAnnounceCheckCb = glsd_reject_gp_device_announce;

#endif /* GLSD_TELINK_SDK */
