#pragma once

/*
 * Independently-authored GL-SD-301P Telink target configuration.
 * Public Telink SDK V3.7.2.0 consumes this header through tl_common.h.
 */

#define UART_PRINTF_MODE                        0
#define USB_PRINTF_MODE                         0
#define GSUART_PRINTF_MODE                      0
#define ZBHCI_UART                              0
#define ZBHCI_EN                                0

/* Mains-powered End Device: radio remains awake. */
#define PM_ENABLE                               0
#define CLOCK_32K_EXT_CRYSTAL                   0
#define PA_ENABLE                               0

#define TOUCHLINK_SUPPORT                       0
#define FIND_AND_BIND_SUPPORT                   0
#define VOLTAGE_DETECT_ENABLE                   0
#define FLASH_PROTECT_ENABLE                    1
#define MODULE_WATCHDOG_ENABLE                  1
#define MODULE_UART_ENABLE                      1

#if defined(MCU_CORE_8258)
#define CLOCK_SYS_CLOCK_HZ                      48000000
#else
#error "GL-SD-301P target supports only MCU_CORE_8258"
#endif

#include "version_cfg.h"
#include "stack_cfg.h"

/* Required interoperability surface. */
#define ZCL_POWER_CFG_SUPPORT                   0
#define ZCL_DEV_TEMPERATURE_CFG_SUPPORT         0
#define ZCL_GROUP_SUPPORT                       1
#define ZCL_SCENE_SUPPORT                       0
#define ZCL_ON_OFF_SUPPORT                      1
#define ZCL_ON_OFF_SWITCH_CFG_SUPPORT           0
#define ZCL_LEVEL_CTRL_SUPPORT                  1
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

/* mac_phy.c uses the ED scan poll slot. */
typedef enum {
    EV_POLL_ED_DETECT,
    EV_POLL_IDLE,
    EV_POLL_MAX,
} ev_poll_e;

#define GLSD301P_ENDPOINT                       0x0B
#include "glsd301p_target_contract.h"
