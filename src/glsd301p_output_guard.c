#include "glsd301p_output_guard.h"

#include <stddef.h>

static bool glsd301p_output_state_valid(uint8_t current_level,
                                        uint8_t minimum_output,
                                        bool allow_below_min)
{
    if (current_level == GLSD301P_ZCL_LEVEL_UNKNOWN) {
        return false;
    }

    if (!allow_below_min && minimum_output == GLSD301P_ZCL_LEVEL_UNKNOWN) {
        return false;
    }

    return true;
}

static bool glsd301p_encode_confirmed_off(uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    return glsd301p_normal_output_frame_encode(false, 0u, 0u, false, out);
}

void glsd301p_output_guard_init(glsd301p_output_guard_t *guard)
{
    if (guard == NULL) {
        return;
    }

    guard->ready = false;
    guard->fault_latched = false;
}

bool glsd301p_output_guard_arm(glsd301p_output_guard_t *guard,
                               uint8_t current_level,
                               uint8_t minimum_output,
                               bool allow_below_min)
{
    if (guard == NULL || guard->fault_latched) {
        return false;
    }

    if (!glsd301p_output_state_valid(current_level, minimum_output, allow_below_min)) {
        guard->ready = false;
        return false;
    }

    guard->ready = true;
    return true;
}

void glsd301p_output_guard_latch_fault(glsd301p_output_guard_t *guard)
{
    if (guard == NULL) {
        return;
    }

    guard->fault_latched = true;
    guard->ready = false;
}

void glsd301p_output_guard_clear_fault_for_reinit(glsd301p_output_guard_t *guard)
{
    if (guard == NULL) {
        return;
    }

    guard->fault_latched = false;
    guard->ready = false;
}

bool glsd301p_output_guard_is_ready(const glsd301p_output_guard_t *guard)
{
    return guard != NULL && guard->ready && !guard->fault_latched;
}

glsd301p_output_guard_result_t glsd301p_output_guard_encode_normal(
    const glsd301p_output_guard_t *guard,
    bool output_enabled,
    uint8_t current_level,
    uint8_t minimum_output,
    bool allow_below_min,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    if (guard == NULL || out == NULL) {
        return GLSD301P_OUTPUT_GUARD_INVALID_ARGUMENT;
    }

    /* OFF is absolute and remains available in every guard state. */
    if (!output_enabled) {
        if (!glsd301p_encode_confirmed_off(out)) {
            return GLSD301P_OUTPUT_GUARD_INVALID_ARGUMENT;
        }
        return GLSD301P_OUTPUT_GUARD_OK;
    }

    if (guard->fault_latched) {
        (void)glsd301p_encode_confirmed_off(out);
        return GLSD301P_OUTPUT_GUARD_FORCED_OFF_FAULT_LATCHED;
    }

    if (!guard->ready) {
        (void)glsd301p_encode_confirmed_off(out);
        return GLSD301P_OUTPUT_GUARD_FORCED_OFF_NOT_READY;
    }

    if (!glsd301p_output_state_valid(current_level, minimum_output, allow_below_min)) {
        (void)glsd301p_encode_confirmed_off(out);
        return GLSD301P_OUTPUT_GUARD_FORCED_OFF_INVALID_LEVEL;
    }

    if (!glsd301p_normal_output_frame_encode(output_enabled,
                                              current_level,
                                              minimum_output,
                                              allow_below_min,
                                              out)) {
        (void)glsd301p_encode_confirmed_off(out);
        return GLSD301P_OUTPUT_GUARD_FORCED_OFF_INVALID_LEVEL;
    }

    return GLSD301P_OUTPUT_GUARD_OK;
}

glsd301p_output_guard_result_t glsd301p_output_guard_encode_pb4(
    const glsd301p_output_guard_t *guard,
    bool logical_output_enabled,
    uint8_t current_level,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    if (guard == NULL || out == NULL) {
        return GLSD301P_OUTPUT_GUARD_INVALID_ARGUMENT;
    }

    /* Safety enhancement: logical OFF dominates all auxiliary requests. */
    if (!logical_output_enabled) {
        if (!glsd301p_encode_confirmed_off(out)) {
            return GLSD301P_OUTPUT_GUARD_INVALID_ARGUMENT;
        }
        return GLSD301P_OUTPUT_GUARD_OK;
    }

    if (guard->fault_latched) {
        (void)glsd301p_encode_confirmed_off(out);
        return GLSD301P_OUTPUT_GUARD_FORCED_OFF_FAULT_LATCHED;
    }

    if (!guard->ready) {
        (void)glsd301p_encode_confirmed_off(out);
        return GLSD301P_OUTPUT_GUARD_FORCED_OFF_NOT_READY;
    }

    if (current_level == GLSD301P_ZCL_LEVEL_UNKNOWN) {
        (void)glsd301p_encode_confirmed_off(out);
        return GLSD301P_OUTPUT_GUARD_FORCED_OFF_INVALID_LEVEL;
    }

    if (!glsd301p_control_frame_encode(GLSD301P_CONTROL_FAMILY_LEVEL,
                                        (uint8_t)(current_level >> 1),
                                        out)) {
        (void)glsd301p_encode_confirmed_off(out);
        return GLSD301P_OUTPUT_GUARD_FORCED_OFF_INVALID_LEVEL;
    }

    return GLSD301P_OUTPUT_GUARD_OK;
}
