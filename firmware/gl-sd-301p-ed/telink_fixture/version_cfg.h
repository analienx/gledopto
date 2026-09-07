#pragma once

#include "comm_cfg.h"

#define CHIP_TYPE                       TLSR_8258_512K
#define APP_RELEASE                     0x7F
#define APP_BUILD                       0x20
#define STACK_RELEASE                   0x00
#define STACK_BUILD                     0x01

#define MANUFACTURER_CODE_TELINK        0x124F
#define IMAGE_TYPE                      0x1416
#define FILE_VERSION                    0x7F200001

#define IS_BOOT_LOADER_IMAGE            0
#define RESV_FOR_APP_RAM_CODE_SIZE      0
#define IMAGE_OFFSET                    APP_IMAGE_ADDR
