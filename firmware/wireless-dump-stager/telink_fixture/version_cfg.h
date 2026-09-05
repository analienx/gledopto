#pragma once

#include "comm_cfg.h"

/* Mechanics-only TLSR8258 512K normal-mode profile. Runtime MID is still checked. */
#define CHIP_TYPE                       TLSR_8258_512K
#define APP_RELEASE                     0x7Fu
#define APP_BUILD                       0x01u
#define STACK_RELEASE                   0x00u
#define STACK_BUILD                     0x01u

/* Target lineage identity; this fixture is never packaged or served as OTA. */
#define MANUFACTURER_CODE_TELINK        0x124Fu
#define IMAGE_TYPE                      0x1416u
#define FILE_VERSION                    0x7F010001u

#define IS_BOOT_LOADER_IMAGE            0
#define RESV_FOR_APP_RAM_CODE_SIZE      0
#ifndef GLSD_STAGER_LINK_BASE
#define GLSD_STAGER_LINK_BASE           0x00000u
#endif
#define IMAGE_OFFSET                    GLSD_STAGER_LINK_BASE
