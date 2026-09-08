#ifndef GLSD301P_RUNTIME_CORE_H
#define GLSD301P_RUNTIME_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "glsd301p_output_guard.h"
#include "glsd301p_pb4_compat.h"
#include "glsd301p_push_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GLSD301P_RUNTIME_NO_FRAME = 0,
    GLSD301P_RUNTIME_FRAME_READY,
    GLSD301P_RUNTIME_FORCED_OFF,
    GLSD301P_RUNTIME_INVALID_ARGUMENT
} glsd301p_runtime_result_t;

typedef struct {
    glsd301p_output_guard_t output_guard;
    glsd301p_push_decoder_t push_decoder;
    glsd301p_pb4_compat_t pb4_compat;

    bool state_restored;
    bool logical_output_enabled;
    uint8_t current_level;
    uint8_t minimum_output;
    bool allow_below_min;
} glsd301p_runtime_core_t;

/** Initialize the runtime in a locked-OFF, state-unknown condition. */
void glsd301p_runtime_core_init(glsd301p_runtime_core_t *core);

/**
 * Produce the mandatory boot OFF frame.  This does not arm the runtime.
 */
glsd301p_runtime_result_t glsd301p_runtime_core_boot_off(
    glsd301p_runtime_core_t *core,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE]);

/**
 * Restore validated application state, arm the output guard, then encode the
 * desired restored logical state.  Invalid state leaves the runtime locked and
 * emits the confirmed OFF frame.
 */
glsd301p_runtime_result_t glsd301p_runtime_core_restore_state(
    glsd301p_runtime_core_t *core,
    bool output_enabled,
    uint8_t current_level,
    uint8_t minimum_output,
    bool allow_below_min,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE]);

/**
 * Apply a normal Zigbee/application OnOff+Level state change.  State commits
 * only when the guarded power-stage encoding succeeds.
 */
glsd301p_runtime_result_t glsd301p_runtime_core_apply_state(
    glsd301p_runtime_core_t *core,
    bool output_enabled,
    uint8_t current_level,
    uint8_t minimum_output,
    bool allow_below_min,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE]);

/**
 * Feed one 1 ms PC2 sample.  PUSH semantic events are translated into the same
 * guarded logical OnOff/Level path; no direct UART bypass exists here.
 *
 * If took_control is non-NULL it is set whenever a decoded physical PUSH
 * semantic action occurs, including a local level action that intentionally
 * emits no frame while logical output is OFF.  Targets use this signal to
 * cancel superseded remote Level transitions before they can re-energize.
 */
glsd301p_runtime_result_t glsd301p_runtime_core_poll_push_ex(
    glsd301p_runtime_core_t *core,
    bool pc2_high,
    bool *took_control,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE]);

/** Compatibility wrapper for callers that do not need takeover metadata. */
glsd301p_runtime_result_t glsd301p_runtime_core_poll_push(
    glsd301p_runtime_core_t *core,
    bool pc2_high,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE]);

/**
 * Feed one 1 ms PB4 sample.  Qualified PB4 requests are routed through the
 * output guard, including the logical-OFF-dominates safety rule.
 */
glsd301p_runtime_result_t glsd301p_runtime_core_poll_pb4(
    glsd301p_runtime_core_t *core,
    bool pb4_high,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE]);

/**
 * Latch a safety fault, invalidate restored state and return an OFF frame.
 */
glsd301p_runtime_result_t glsd301p_runtime_core_latch_fault(
    glsd301p_runtime_core_t *core,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE]);

/**
 * Clear the fault latch into the same locked/not-restored state as boot.
 * This never re-energizes the output and requires a fresh restore_state call.
 */
void glsd301p_runtime_core_clear_fault_for_reinit(glsd301p_runtime_core_t *core);

bool glsd301p_runtime_core_is_ready(const glsd301p_runtime_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_RUNTIME_CORE_H */
