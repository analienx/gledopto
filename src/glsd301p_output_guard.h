#ifndef GLSD301P_OUTPUT_GUARD_H
#define GLSD301P_OUTPUT_GUARD_H

#include <stdbool.h>
#include <stdint.h>

#include "glsd301p_power_stage_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GLSD301P_ZCL_LEVEL_UNKNOWN 0xFFu

typedef enum {
    GLSD301P_OUTPUT_GUARD_OK = 0,
    GLSD301P_OUTPUT_GUARD_FORCED_OFF_NOT_READY,
    GLSD301P_OUTPUT_GUARD_FORCED_OFF_FAULT_LATCHED,
    GLSD301P_OUTPUT_GUARD_FORCED_OFF_INVALID_LEVEL,
    GLSD301P_OUTPUT_GUARD_INVALID_ARGUMENT
} glsd301p_output_guard_result_t;

typedef struct {
    bool ready;
    bool fault_latched;
} glsd301p_output_guard_t;

/**
 * Start in a fail-closed state.  Non-zero output is blocked until a validated
 * application state is explicitly armed.
 */
void glsd301p_output_guard_init(glsd301p_output_guard_t *guard);

/**
 * Arm normal/auxiliary output after persisted/application state has been
 * restored and validated.  Zigbee currentLevel 0xFF is never accepted as a
 * power-stage level.  minimum_output 0xFF is rejected whenever minimum
 * clamping is active.
 */
bool glsd301p_output_guard_arm(glsd301p_output_guard_t *guard,
                               uint8_t current_level,
                               uint8_t minimum_output,
                               bool allow_below_min);

/** Latch a runtime safety fault.  The guard immediately becomes not-ready. */
void glsd301p_output_guard_latch_fault(glsd301p_output_guard_t *guard);

/**
 * Clear a latched fault only into the boot-locked/not-ready state.  A separate
 * successful arm is required before non-zero output can resume.
 */
void glsd301p_output_guard_clear_fault_for_reinit(glsd301p_output_guard_t *guard);

bool glsd301p_output_guard_is_ready(const glsd301p_output_guard_t *guard);

/**
 * Encode the ordinary On/Off + Level path through the safety guard.
 *
 * A logical OFF request is always allowed and always produces the confirmed
 * A5 5A 01 00 04 AA frame, even while locked or faulted.  Any unsafe ON request
 * produces that same OFF frame and returns the reason.
 */
glsd301p_output_guard_result_t glsd301p_output_guard_encode_normal(
    const glsd301p_output_guard_t *guard,
    bool output_enabled,
    uint8_t current_level,
    uint8_t minimum_output,
    bool allow_below_min,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE]);

/**
 * Encode the solved PB4 compatibility action through the same guard.
 *
 * Intentional safety invariant: logical OFF dominates PB4, so PB4 can never
 * energize the load from an OFF state.  When ready and logically ON, the
 * confirmed stock-compatible currentLevel>>1 family-0x01 value is emitted.
 */
glsd301p_output_guard_result_t glsd301p_output_guard_encode_pb4(
    const glsd301p_output_guard_t *guard,
    bool logical_output_enabled,
    uint8_t current_level,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_OUTPUT_GUARD_H */
