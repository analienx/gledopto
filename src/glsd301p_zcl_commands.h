#ifndef GLSD301P_ZCL_COMMANDS_H
#define GLSD301P_ZCL_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "glsd301p_control.h"
#include "glsd301p_runtime_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared OnOff/Level cluster-command policy for the GL-SD-301P End Device.
 *
 * The target's ZCL application callbacks are thin adapters (NULL guard plus
 * endpoint forward); every command decision below is SDK-independent policy
 * over the shared control plane and compiles unmodified into the TC32
 * firmware and the hosted dispatch harness. The harness drives these
 * functions through the real pinned SDK client-command parsers
 * (zcl_level_clientCmdHandler / zcl_onOff_clientCmdHandler) with the P2
 * validation patches applied, so the on-wire parse, the length gates and
 * the callback-status propagation are all exercised, not modeled.
 *
 * Returned bytes are ZCL status codes; payload layouts are the pinned SDK
 * cluster types (moveToLvl_t / step_t / move_t), read only after the SDK
 * parser validated the frame length.
 */

typedef struct {
    uint8_t endpoint;
    uint8_t min_level;
    glsd301p_runtime_core_t *runtime;
    glsd301p_control_ctx_t *control;
    glsd301p_level_state_t *level;
    uint16_t *on_time;
    uint16_t *off_wait_time;
} glsd301p_zcl_ctx_t;

uint8_t glsd301p_zcl_onoff_command(glsd301p_zcl_ctx_t *ctx,
                                  uint8_t dst_ep,
                                  uint8_t cmd_id,
                                  void *payload);

uint8_t glsd301p_zcl_level_command(glsd301p_zcl_ctx_t *ctx,
                                  uint8_t dst_ep,
                                  uint8_t cmd_id,
                                  void *payload);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_ZCL_COMMANDS_H */
