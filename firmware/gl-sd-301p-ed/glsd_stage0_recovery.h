#pragma once

#ifdef GLSD_TELINK_SDK

#include "tl_common.h"

typedef enum {
    GLSD_STAGE0_RECOVERY_OK = 0,
    GLSD_STAGE0_RECOVERY_ERR_BOOT_LAYOUT = -1,
    GLSD_STAGE0_RECOVERY_ERR_STOCK_INVALID = -2,
    GLSD_STAGE0_RECOVERY_ERR_BACKUP = -3,
    GLSD_STAGE0_RECOVERY_ERR_RESTORE = -4,
    GLSD_STAGE0_RECOVERY_ERR_SELF_INVALIDATE = -5,
} glsd_stage0_recovery_status_t;

/*
 * Prepare a one-shot Stage-0 canary for fail-safe return to the pre-OTA stock
 * image. The implementation is bank-neutral: at entry it determines whether
 * Stage-0 is currently running from physical bank A (0x00000) or B (0x40000),
 * then treats the opposite bank as the preserved stock image.
 *
 * A verified copy of the stock bank's first 4-KiB sector plus a CRC-protected
 * journal are stored in two reserved sectors at +0x30000/+0x31000 of Stage-0's
 * own bank. The stock first sector is restored with its boot byte left erased
 * (0xFF), the complete stock image is revalidated, and only then is 0xFF ->
 * 0x4B programmed as the atomic stock-boot commit. Stage-0 invalidates its own
 * boot byte only after that stock commit has been verified.
 *
 * The routine never touches Zigbee NV, MAC, factory calibration, or the
 * power-stage UART. After GLSD_STAGE0_RECOVERY_OK, the opposite stock bank is
 * valid and Stage-0's bank is deliberately non-bootable.
 */
glsd_stage0_recovery_status_t glsd_stage0_recovery_prepare(void);

bool glsd_stage0_recovery_armed(void);

#endif /* GLSD_TELINK_SDK */
