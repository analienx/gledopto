#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the confirmed GL-SD-301P hardware boundary:
 * - 9600 8N1 UART, TX=PB1, RX=PA0;
 * - PC2 external PUSH sense input;
 * - PB4 auxiliary compatibility input.
 */
int glsd_power_stage_init(void);

/* Apply the normal logical On/Off + Level state through family 0x01. */
int glsd_power_stage_apply(uint8_t on, uint8_t level);

/* Apply the stock PB4 auxiliary half-scale request without changing logical
 * Zigbee Level state. */
int glsd_power_stage_apply_pb4_aux(uint8_t logical_level);

/* Raw input samples for the independently tested semantic decoders. */
int glsd_power_stage_pc2_high(void);
int glsd_power_stage_pb4_high(void);

#ifdef __cplusplus
}
#endif
