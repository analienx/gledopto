#include "glsd301p_zcl_commands.h"

#include "glsd301p_output_guard.h"
#include "glsd301p_timebase.h"
#include "zcl_include.h"

uint8_t glsd301p_zcl_onoff_command(glsd301p_zcl_ctx_t *ctx,
                                  uint8_t dst_ep,
                                  uint8_t cmd_id,
                                  void *payload)
{
    bool requested;
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];

    if (ctx == NULL || ctx->runtime == NULL || ctx->control == NULL ||
        ctx->level == NULL || ctx->on_time == NULL ||
        ctx->off_wait_time == NULL) {
        return ZCL_STA_FAILURE;
    }
    if (dst_ep != ctx->endpoint) {
        return ZCL_STA_INVALID_FIELD;
    }

    /*
     * F9: only the two defined effect ids map to OFF; reserved ids are
     * rejected before any state change (no cancel, no emit).
     */
    if (cmd_id == ZCL_CMD_OFF_WITH_EFFECT) {
        zcl_onoff_offWithEffectCmd_t *cmd =
            (zcl_onoff_offWithEffectCmd_t *)payload;
        if (cmd == NULL ||
            (cmd->effectId != ZCL_OFF_EFFECT_DELAYED_ALL_OFF &&
             cmd->effectId != ZCL_OFF_EFFECT_DYING_LIGHT)) {
            return ZCL_STA_INVALID_FIELD;
        }
    }

    switch (cmd_id) {
    case ZCL_CMD_ONOFF_OFF:
    case ZCL_CMD_OFF_WITH_EFFECT:
        requested = false;
        break;
    case ZCL_CMD_ONOFF_ON:
    case ZCL_CMD_ON_WITH_RECALL_GLOBAL_SCENE:
        requested = true;
        break;
    case ZCL_CMD_ONOFF_TOGGLE:
        requested = !ctx->runtime->logical_output_enabled;
        break;
    default:
        return ZCL_STA_UNSUP_CLUSTER_COMMAND;
    }

    /*
     * Refusal leaves any running transition untouched. OFF already holds by
     * construction while not ready, so it still reports success.
     */
    if (!glsd301p_runtime_core_is_ready(ctx->runtime)) {
        return requested ? ZCL_STA_FAILURE : ZCL_STA_SUCCESS;
    }

    glsd301p_control_level_cancel(ctx->control);

    if (!glsd301p_control_emit(
            ctx->control,
            glsd301p_runtime_core_apply_state(ctx->runtime, requested,
                                              ctx->level->current_level,
                                              ctx->min_level, false, frame),
            frame, glsd301p_timebase_now_ms())) {
        return ZCL_STA_FAILURE;
    }

    *ctx->on_time = 0u;
    if (!requested) {
        *ctx->off_wait_time = 0u;
    }
    return ZCL_STA_SUCCESS;
}

uint8_t glsd301p_zcl_level_command(glsd301p_zcl_ctx_t *ctx,
                                  uint8_t dst_ep,
                                  uint8_t cmd_id,
                                  void *payload)
{
    if (ctx == NULL || ctx->control == NULL || ctx->level == NULL) {
        return ZCL_STA_FAILURE;
    }
    if (dst_ep != ctx->endpoint) {
        return ZCL_STA_INVALID_FIELD;
    }

    switch (cmd_id) {
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL:
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF: {
        moveToLvl_t *cmd = (moveToLvl_t *)payload;
        if (cmd == NULL || cmd->level == GLSD301P_ZCL_LEVEL_UNKNOWN) {
            return ZCL_STA_INVALID_FIELD;
        }
        if (!glsd301p_control_level_start_target(
                ctx->control, cmd->level, cmd->transitionTime,
                (uint8_t)(cmd_id == ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF ? 1u : 0u))) {
            return ZCL_STA_FAILURE;
        }
        return ZCL_STA_SUCCESS;
    }
    case ZCL_CMD_LEVEL_STEP:
    case ZCL_CMD_LEVEL_STEP_WITH_ON_OFF: {
        step_t *cmd = (step_t *)payload;
        uint16_t target = ctx->level->current_level;
        if (cmd == NULL) {
            return ZCL_STA_INVALID_FIELD;
        }
        /* F8: reserved step modes rejected before any state change. */
        if (cmd->stepMode != LEVEL_STEP_UP &&
            cmd->stepMode != LEVEL_STEP_DOWN) {
            return ZCL_STA_INVALID_FIELD;
        }
        if (cmd->stepMode == LEVEL_STEP_UP) {
            target = (uint16_t)(target + cmd->stepSize);
        } else {
            target = (target > cmd->stepSize) ? (uint16_t)(target - cmd->stepSize) : ctx->min_level;
        }
        {
            uint8_t clipped =
                glsd301p_control_clamp_level(ctx->control, target);
            uint8_t current = ctx->level->current_level;
            uint16_t moved = clipped > current
                                 ? (uint16_t)(clipped - current)
                                 : (uint16_t)(current - clipped);
            uint16_t duration = glsd301p_control_proportional_time(
                cmd->stepSize, moved, cmd->transitionTime);

            if (!glsd301p_control_level_start_target(
                    ctx->control, clipped, duration,
                    (uint8_t)(cmd_id == ZCL_CMD_LEVEL_STEP_WITH_ON_OFF ? 1u
                                                                       : 0u))) {
                return ZCL_STA_FAILURE;
            }
        }
        return ZCL_STA_SUCCESS;
    }
    case ZCL_CMD_LEVEL_MOVE:
    case ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF: {
        move_t *cmd = (move_t *)payload;
        if (cmd == NULL) {
            return ZCL_STA_INVALID_FIELD;
        }
        /*
         * F8: reserved move modes and rate 0 are rejected before the
         * control layer can cancel the running transition.
         */
        if (cmd->moveMode != LEVEL_MOVE_UP &&
            cmd->moveMode != LEVEL_MOVE_DOWN) {
            return ZCL_STA_INVALID_FIELD;
        }
        if (cmd->rate == 0u) {
            return ZCL_STA_INVALID_FIELD;
        }
        if (!glsd301p_control_level_start_move(
                ctx->control,
                (uint8_t)(cmd->moveMode == LEVEL_MOVE_UP ? 1u : 0u), cmd->rate,
                (uint8_t)(cmd_id == ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF ? 1u : 0u))) {
            return ZCL_STA_FAILURE;
        }
        return ZCL_STA_SUCCESS;
    }
    case ZCL_CMD_LEVEL_STOP:
    case ZCL_CMD_LEVEL_STOP_WITH_ON_OFF:
        /*
         * R13: Stop emits nothing itself, so a preempted effect is
         * restored explicitly; without an effect this is a no-op.
         */
        if (glsd301p_control_identify_effect_abort(ctx->control)) {
            glsd301p_control_identify_effect_restore(
                ctx->control, glsd301p_timebase_now_ms());
        }
        glsd301p_control_level_cancel(ctx->control);
        return ZCL_STA_SUCCESS;
    default:
        return ZCL_STA_UNSUP_CLUSTER_COMMAND;
    }
}
