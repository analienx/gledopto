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
 * Prepare a one-shot Stage-0 canary for fail-safe return to stock.
 *
 * Required initial state:
 *   bank A (0x00000): stock image, boot byte disabled by Telink OTA activation
 *   bank B (0x40000): Stage-0 image, currently bootable
 *
 * The routine validates stock A with only its boot byte normalized, journals a
 * persistent copy of stock sector 0 inside reserved Stage-0 tail sectors,
 * restores and revalidates stock A, then invalidates Stage-0's own boot byte.
 * It never touches Zigbee NV, MAC, factory calibration, or the power-stage UART.
 *
 * After GLSD_STAGE0_RECOVERY_OK, any subsequent reset/power loss must select
 * stock A because bank A is valid and bank B has deliberately been invalidated.
 */
glsd_stage0_recovery_status_t glsd_stage0_recovery_prepare(void);

bool glsd_stage0_recovery_armed(void);

#endif /* GLSD_TELINK_SDK */
