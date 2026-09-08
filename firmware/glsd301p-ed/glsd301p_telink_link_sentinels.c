/*
 * Link-resolution sentinels for references that must be unreachable in the
 * GL-SD-301P End Device runtime.
 *
 * Why this exists:
 * - libzb_ed.a:zb_api.o contains undefined references to Trust-Center/router
 *   security primitives whose definitions exist only in Telink's router and
 *   coordinator archives.
 * - libzb_ed.a:nwk_panid_conflict.o references the router/coordinator beacon
 *   payload updater, likewise absent from libzb_ed.a.
 * - selected public TLSR8258 flash-vendor wrappers reference OTP helpers whose
 *   implementations are deliberately compiled out (#if 0) in flash.c.
 *
 * GNU/TC32 ld resolves undefined references before --gc-sections discards dead
 * function sections. These inert definitions permit that resolution only.
 * The build MUST fail if any symbol in this file survives into the final ELF.
 * They are not implementations and must never become a runtime fallback.
 */

#include "tl_common.h"
#include "security_service.h"

u8 ss_apsmeSwitchKeyReq(ss_apsmeSwitchKeyReq_t *req)
{
    (void)req;
    return 1u;
}

u8 ss_apsmeTransportKeyReq(ss_apsmeTransportKeyReq_t *req)
{
    (void)req;
    return 1u;
}

void tl_zbNwkBeaconPayloadUpdate(void)
{
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
