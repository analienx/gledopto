#include "glsd301p_runtime_core.h"

#include <stddef.h>

static void glsd301p_runtime_core_invalidate_state(glsd301p_runtime_core_t *core)
{
    core->state_restored = false;
    core->logical_output_enabled = false;
    core->current_level = GLSD301P_ZCL_LEVEL_UNKNOWN;
    core->minimum_output = GLSD301P_ZCL_LEVEL_UNKNOWN;
    core->allow_below_min = false;
    glsd301p_push_decoder_init(&core->push_decoder);
    glsd301p_pb4_compat_init(&core->pb4_compat);
}

static glsd301p_runtime_result_t glsd301p_runtime_map_guard_result(
    glsd301p_output_guard_result_t result)
{
    switch (result) {
    case GLSD301P_OUTPUT_GUARD_OK:
        return GLSD301P_RUNTIME_FRAME_READY;
    case GLSD301P_OUTPUT_GUARD_FORCED_OFF_NOT_READY:
    case GLSD301P_OUTPUT_GUARD_FORCED_OFF_FAULT_LATCHED:
    case GLSD301P_OUTPUT_GUARD_FORCED_OFF_INVALID_LEVEL:
        return GLSD301P_RUNTIME_FORCED_OFF;
    case GLSD301P_OUTPUT_GUARD_INVALID_ARGUMENT:
    default:
        return GLSD301P_RUNTIME_INVALID_ARGUMENT;
    }
}

static glsd301p_runtime_result_t glsd301p_runtime_force_off(
    const glsd301p_runtime_core_t *core,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    glsd301p_output_guard_result_t result;

    result = glsd301p_output_guard_encode_normal(&core->output_guard,
                                                  false,
                                                  GLSD301P_ZCL_LEVEL_UNKNOWN,
                                                  GLSD301P_ZCL_LEVEL_UNKNOWN,
                                                  false,
                                                  out);
    if (result == GLSD301P_OUTPUT_GUARD_OK) {
        return GLSD301P_RUNTIME_FORCED_OFF;
    }
    return glsd301p_runtime_map_guard_result(result);
}

void glsd301p_runtime_core_init(glsd301p_runtime_core_t *core)
{
    if (core == NULL) {
        return;
    }

    glsd301p_output_guard_init(&core->output_guard);
    glsd301p_runtime_core_invalidate_state(core);
}

glsd301p_runtime_result_t glsd301p_runtime_core_boot_off(
    glsd301p_runtime_core_t *core,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    glsd301p_output_guard_result_t result;

    if (core == NULL || out == NULL) {
        return GLSD301P_RUNTIME_INVALID_ARGUMENT;
    }

    result = glsd301p_output_guard_encode_normal(&core->output_guard,
                                                  false,
                                                  GLSD301P_ZCL_LEVEL_UNKNOWN,
                                                  GLSD301P_ZCL_LEVEL_UNKNOWN,
                                                  false,
                                                  out);
    return glsd301p_runtime_map_guard_result(result);
}

glsd301p_runtime_result_t glsd301p_runtime_core_restore_state(
    glsd301p_runtime_core_t *core,
    bool output_enabled,
    uint8_t current_level,
    uint8_t minimum_output,
    bool allow_below_min,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    glsd301p_output_guard_result_t result;

    if (core == NULL || out == NULL) {
        return GLSD301P_RUNTIME_INVALID_ARGUMENT;
    }

    if (!glsd301p_output_guard_arm(&core->output_guard,
                                    current_level,
                                    minimum_output,
                                    allow_below_min)) {
        glsd301p_runtime_core_invalidate_state(core);
        return glsd301p_runtime_force_off(core, out);
    }

    result = glsd301p_output_guard_encode_normal(&core->output_guard,
                                                  output_enabled,
                                                  current_level,
                                                  minimum_output,
                                                  allow_below_min,
                                                  out);
    if (result != GLSD301P_OUTPUT_GUARD_OK) {
        /* An armed guard without restored application state is forbidden. */
        glsd301p_output_guard_init(&core->output_guard);
        glsd301p_runtime_core_invalidate_state(core);
        (void)glsd301p_runtime_force_off(core, out);
        return GLSD301P_RUNTIME_FORCED_OFF;
    }

    core->state_restored = true;
    core->logical_output_enabled = output_enabled;
    core->current_level = current_level;
    core->minimum_output = minimum_output;
    core->allow_below_min = allow_below_min;
    return GLSD301P_RUNTIME_FRAME_READY;
}

glsd301p_runtime_result_t glsd301p_runtime_core_apply_state(
    glsd301p_runtime_core_t *core,
    bool output_enabled,
    uint8_t current_level,
    uint8_t minimum_output,
    bool allow_below_min,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    glsd301p_output_guard_result_t result;

    if (core == NULL || out == NULL) {
        return GLSD301P_RUNTIME_INVALID_ARGUMENT;
    }

    /* Application-state restoration is an independent mandatory gate. */
    if (!core->state_restored) {
        return glsd301p_runtime_force_off(core, out);
    }

    result = glsd301p_output_guard_encode_normal(&core->output_guard,
                                                  output_enabled,
                                                  current_level,
                                                  minimum_output,
                                                  allow_below_min,
                                                  out);
    if (result != GLSD301P_OUTPUT_GUARD_OK) {
        return glsd301p_runtime_map_guard_result(result);
    }

    core->logical_output_enabled = output_enabled;
    core->current_level = current_level;
    core->minimum_output = minimum_output;
    core->allow_below_min = allow_below_min;
    return GLSD301P_RUNTIME_FRAME_READY;
}

glsd301p_runtime_result_t glsd301p_runtime_core_poll_push_ex(
    glsd301p_runtime_core_t *core,
    bool pc2_high,
    bool *took_control,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    glsd301p_push_event_t event;
    glsd301p_output_guard_result_t result;

    if (took_control != NULL) {
        *took_control = false;
    }
    if (core == NULL || out == NULL) {
        return GLSD301P_RUNTIME_INVALID_ARGUMENT;
    }

    event = glsd301p_push_decoder_poll(&core->push_decoder, pc2_high);
    if (event == GLSD301P_PUSH_EVENT_NONE) {
        return GLSD301P_RUNTIME_NO_FRAME;
    }
    if (took_control != NULL) {
        *took_control = true;
    }

    if (!core->state_restored) {
        return glsd301p_runtime_force_off(core, out);
    }

    if (event == GLSD301P_PUSH_EVENT_TOGGLE) {
        bool requested_output = !core->logical_output_enabled;

        result = glsd301p_output_guard_encode_normal(&core->output_guard,
                                                      requested_output,
                                                      core->current_level,
                                                      core->minimum_output,
                                                      core->allow_below_min,
                                                      out);
        if (result == GLSD301P_OUTPUT_GUARD_OK) {
            core->logical_output_enabled = requested_output;
        }
        return glsd301p_runtime_map_guard_result(result);
    }

    if (event == GLSD301P_PUSH_EVENT_LEVEL_STEP) {
        uint8_t requested_level;

        /* Stock local level adjustment is gated by logical OnOff state. */
        if (!core->logical_output_enabled) {
            return GLSD301P_RUNTIME_NO_FRAME;
        }

        requested_level = glsd301p_push_level_step(
            core->current_level,
            glsd301p_push_decoder_direction(&core->push_decoder));

        result = glsd301p_output_guard_encode_normal(&core->output_guard,
                                                      true,
                                                      requested_level,
                                                      core->minimum_output,
                                                      core->allow_below_min,
                                                      out);
        if (result == GLSD301P_OUTPUT_GUARD_OK) {
            core->current_level = requested_level;
        }
        return glsd301p_runtime_map_guard_result(result);
    }

    return GLSD301P_RUNTIME_NO_FRAME;
}

glsd301p_runtime_result_t glsd301p_runtime_core_poll_push(
    glsd301p_runtime_core_t *core,
    bool pc2_high,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    return glsd301p_runtime_core_poll_push_ex(core, pc2_high, NULL, out);
}

glsd301p_runtime_result_t glsd301p_runtime_core_poll_pb4(
    glsd301p_runtime_core_t *core,
    bool pb4_high,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    glsd301p_output_guard_result_t result;

    if (core == NULL || out == NULL) {
        return GLSD301P_RUNTIME_INVALID_ARGUMENT;
    }

    if (!glsd301p_pb4_compat_poll(&core->pb4_compat, pb4_high)) {
        return GLSD301P_RUNTIME_NO_FRAME;
    }

    if (!core->state_restored) {
        return glsd301p_runtime_force_off(core, out);
    }

    result = glsd301p_output_guard_encode_pb4(&core->output_guard,
                                               core->logical_output_enabled,
                                               core->current_level,
                                               out);
    return glsd301p_runtime_map_guard_result(result);
}

glsd301p_runtime_result_t glsd301p_runtime_core_latch_fault(
    glsd301p_runtime_core_t *core,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    if (core == NULL || out == NULL) {
        return GLSD301P_RUNTIME_INVALID_ARGUMENT;
    }

    glsd301p_output_guard_latch_fault(&core->output_guard);
    glsd301p_runtime_core_invalidate_state(core);
    return glsd301p_runtime_force_off(core, out);
}

void glsd301p_runtime_core_clear_fault_for_reinit(glsd301p_runtime_core_t *core)
{
    if (core == NULL) {
        return;
    }

    glsd301p_output_guard_clear_fault_for_reinit(&core->output_guard);
    glsd301p_runtime_core_invalidate_state(core);
}

bool glsd301p_runtime_core_is_ready(const glsd301p_runtime_core_t *core)
{
    return core != NULL &&
           core->state_restored &&
           glsd301p_output_guard_is_ready(&core->output_guard);
}
