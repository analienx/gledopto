#pragma once

#include "comm_cfg.h"

/* Mechanics-only TLSR8258 512K normal-mode profile. Runtime MID is still checked. */
#define CHIP_TYPE                       TLSR_8258_512K
#define APP_RELEASE                     0x7F
#define APP_BUILD                       0x01
#define STACK_RELEASE                   0x00
#define STACK_BUILD                     0x01

/*
 * Target-lineage identity. Keep these literals assembler-safe because Telink
 * cstartup_8258.S includes version_cfg.h and emits them directly with .word /
 * .short. This fixture is never authorized for production deployment by itself.
 */
#define MANUFACTURER_CODE_TELINK        0x124F
#define IMAGE_TYPE                      0x1416
#define FILE_VERSION                    0x7F010001

#define IS_BOOT_LOADER_IMAGE            0
#define RESV_FOR_APP_RAM_CODE_SIZE      0

/*
 * IMPORTANT: standard TLSR8258 Zigbee OTA uses hardware multi-address startup.
 * A normal application is linked at APP_IMAGE_ADDR (0x00000) once; the same
 * binary can be stored/booted at physical 0x00000 or 0x40000. Do NOT relink the
 * OTA payload to 0x40000. The running physical bank is discovered at runtime by
 * mcuBootAddrGet(). This mirrors Telink sampleLight/version_cfg.h.
 */
#define IMAGE_OFFSET                    APP_IMAGE_ADDR
