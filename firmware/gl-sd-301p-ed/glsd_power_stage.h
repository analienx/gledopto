#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The Zigbee/product layer only depends on this boundary. The implementation
 * must eventually map the logical (on, level) pair to the installed GL-SD
 * hardware, either by direct Telink phase-control GPIO/timers or by the
 * protocol of a secondary power-stage MCU.
 */
int glsd_power_stage_init(void);
int glsd_power_stage_apply(uint8_t on, uint8_t level);

/* Optional physical PUSH input. Return 1 while pressed, 0 while released. */
int glsd_power_stage_push_pressed(void);

#ifdef __cplusplus
}
#endif
