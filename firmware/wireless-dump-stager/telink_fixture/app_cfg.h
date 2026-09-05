#pragma once

/*
 * TLSR8258 compile/link fixture for the temporary read-only extraction stager.
 * BOARD_8258_DONGLE is used only to satisfy the public SDK's platform/header
 * mechanics. The stager application never calls the sample board's LED/light,
 * GPIO, PWM, key, factory-reset, touchlink, or commissioning UI routines.
 */

#define UART_PRINTF_MODE                        0
#define USB_PRINTF_MODE                         0
#define ZBHCI_UART                              0
#define ZBHCI_EN                                0

#define TOUCHLINK_SUPPORT                       0
#define FIND_AND_BIND_SUPPORT                   0
#define VOLTAGE_DETECT_ENABLE                   0
#define FLASH_PROTECT_ENABLE                    1
#define MODULE_WATCHDOG_ENABLE                  0

#define BOARD_826x_EVK                          0
#define BOARD_826x_DONGLE                       1
#define BOARD_8258_EVK                          2
#define BOARD_8258_EVK_V1P2                     3
#define BOARD_8258_DONGLE                       4
#define BOARD_8278_EVK                          5
#define BOARD_8278_DONGLE                       6

#if defined(MCU_CORE_8258)
#define BOARD                                   BOARD_8258_DONGLE
#define CLOCK_SYS_CLOCK_HZ                      48000000
#else
#error "GL-SD stager mechanics fixture supports only MCU_CORE_8258"
#endif

#include "version_cfg.h"
#include "board_8258_dongle.h"
#include "stack_cfg.h"

/* Keep the active ZCL surface deliberately minimal. */
#define ZCL_POWER_CFG_SUPPORT                   0
#define ZCL_DEV_TEMPERATURE_CFG_SUPPORT         0
#define ZCL_GROUP_SUPPORT                       0
#define ZCL_SCENE_SUPPORT                       0
#define ZCL_ON_OFF_SUPPORT                      0
#define ZCL_ON_OFF_SWITCH_CFG_SUPPORT           0
#define ZCL_LEVEL_CTRL_SUPPORT                  0
#define ZCL_ALARMS_SUPPORT                      0
#define ZCL_TIME_SUPPORT                        0
#define ZCL_RSSI_LOCATION_SUPPORT               0
#define ZCL_DIAGNOSTICS_SUPPORT                 0
#define ZCL_POLL_CTRL_SUPPORT                   0
#define ZCL_GP_SUPPORT                          0
#define ZCL_BINARY_INPUT_SUPPORT                0
#define ZCL_BINARY_OUTPUT_SUPPORT               0
#define ZCL_MULTISTATE_INPUT_SUPPORT            0
#define ZCL_MULTISTATE_OUTPUT_SUPPORT           0
#define ZCL_ILLUMINANCE_MEASUREMENT_SUPPORT     0
#define ZCL_ILLUMINANCE_LEVEL_SENSING_SUPPORT   0
#define ZCL_TEMPERATURE_MEASUREMENT_SUPPORT     0
#define ZCL_OCCUPANCY_SENSING_SUPPORT           0
#define ZCL_ELECTRICAL_MEASUREMENT_SUPPORT      0
#define ZCL_LIGHT_COLOR_CONTROL_SUPPORT         0
#define ZCL_THERMOSTAT_SUPPORT                  0
#define ZCL_DOOR_LOCK_SUPPORT                   0
#define ZCL_WINDOW_COVERING_SUPPORT             0
#define ZCL_IAS_ZONE_SUPPORT                    0
#define ZCL_IAS_ACE_SUPPORT                     0
#define ZCL_IAS_WD_SUPPORT                      0
#define ZCL_METERING_SUPPORT                    0
#define ZCL_OTA_SUPPORT                         1
#define ZCL_ZLL_COMMISSIONING_SUPPORT           0
#define ZCL_WWAH_SUPPORT                        0
#define AF_TEST_ENABLE                          0

typedef enum {
    EV_POLL_IDLE,
    EV_POLL_MAX,
} ev_poll_e;
