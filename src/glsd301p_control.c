#include "glsd301p_control.h"

#include <stddef.h>

#include "glsd301p_hw_io.h"
#include "glsd301p_timebase.h"
#include "glsd301p_timer_events.h"

#define GLSD301P_ZCL_ONOFF_ON 0x01u
#define GLSD301P_ZCL_ONOFF_OFF 0x00u

static bool glsd301p_control_ready(const glsd301p_control_ctx_t *ctx)
{
    return ctx != NULL && ctx->runtime != NULL &&
           glsd301p_runtime_core_is_ready(ctx->runtime);
}

static void glsd301p_control_sync_from_runtime(glsd301p_control_ctx_t *ctx)
{
    if (ctx == NULL || ctx->runtime == NULL) {
        return;
    }
    if (ctx->onoff_mirror != NULL) {
        *ctx->onoff_mirror = ctx->runtime->logical_output_enabled
                                 ? GLSD301P_ZCL_ONOFF_ON
                                 : GLSD301P_ZCL_ONOFF_OFF;
    }
    if (ctx->level != NULL) {
        ctx->level->current_level = ctx->runtime->current_level;
    }
}

void glsd301p_control_init(glsd301p_control_ctx_t *ctx,
                           glsd301p_runtime_core_t *runtime,
                           glsd301p_uart_transport_t *transport,
                           glsd301p_uart_service_t *uart,
                           glsd301p_level_state_t *level,
                           uint8_t *onoff_mirror,
                           uint8_t min_level,
                           uint8_t max_level,
                           uint8_t boot_level,
                           uint8_t boot_min)
{
    if (ctx == NULL) {
        return;
    }

    ctx->runtime = runtime;
    ctx->transport = transport;
    ctx->uart = uart;
    ctx->level = level;
    ctx->onoff_mirror = onoff_mirror;
    ctx->min_level = min_level;
    ctx->max_level = max_level;
    ctx->boot_level = boot_level;
    ctx->boot_min = boot_min;
    ctx->io_serviced_once = false;
    ctx->io_last_ms = 0u;
    ctx->io_max_gap_ms = 0u;

    if (level != NULL) {
        level->mode = GLSD301P_LEVEL_IDLE;
        level->target = boot_level;
        level->rate = 0u;
        level->rate_accum_tenths = 0u;
        level->direction_up = 0u;
        level->with_onoff = 0u;
        level->current_level = boot_level;
        level->remaining_time = 0u;
    }
    if (onoff_mirror != NULL) {
        *onoff_mirror = GLSD301P_ZCL_ONOFF_OFF;
    }
}

bool glsd301p_control_boot_off(glsd301p_control_ctx_t *ctx, uint32_t now_ms)
{
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];

    if (ctx == NULL || ctx->runtime == NULL || ctx->uart == NULL) {
        return false;
    }
    if (glsd301p_runtime_core_boot_off(ctx->runtime, frame) !=
        GLSD301P_RUNTIME_FRAME_READY) {
        return false;
    }
    /* Single bounded attempt: no spin, no allocation, no busy-gate (the TX
     * DONE flag state right after UART init is not a reliable idle signal). */
    if (!glsd301p_hw_uart_send_frame(frame)) {
        return false;
    }

    glsd301p_uart_service_note_boot_sent(ctx->uart, now_ms);
    return true;
}

void glsd301p_control_boot_failed(glsd301p_control_ctx_t *ctx, uint32_t now_ms)
{
    uint8_t off[GLSD301P_CONTROL_FRAME_SIZE];

    if (ctx == NULL || ctx->runtime == NULL || ctx->transport == NULL) {
        return;
    }

    (void)glsd301p_runtime_core_latch_fault(ctx->runtime, off);
    (void)glsd301p_uart_transport_offer(ctx->transport, off, now_ms);
    glsd301p_control_sync_from_runtime(ctx);
}

bool glsd301p_control_emit(glsd301p_control_ctx_t *ctx,
                           glsd301p_runtime_result_t result,
                           uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE],
                           uint32_t now_ms)
{
    uint8_t off[GLSD301P_CONTROL_FRAME_SIZE];

    if (ctx == NULL || ctx->runtime == NULL || ctx->transport == NULL) {
        return false;
    }
    if (result == GLSD301P_RUNTIME_NO_FRAME) {
        return true;
    }
    if (result == GLSD301P_RUNTIME_INVALID_ARGUMENT || frame == NULL) {
        return false;
    }

    /*
     * Not-ready output refuses without latching: logical OFF already holds
     * by construction, so there is nothing to force and no fault to record.
     * In particular a command arriving between Zigbee init and boot-OFF
     * completion must not permanently wedge the runtime.
     */
    if (!glsd301p_runtime_core_is_ready(ctx->runtime)) {
        return false;
    }

    if (result == GLSD301P_RUNTIME_FORCED_OFF) {
        /* An impossible internal state while ready fails closed. */
        (void)glsd301p_runtime_core_latch_fault(ctx->runtime, off);
        (void)glsd301p_uart_transport_offer(ctx->transport, off, now_ms);
        glsd301p_control_sync_from_runtime(ctx);
        return false;
    }

    if (!glsd301p_uart_transport_offer(ctx->transport, frame, now_ms)) {
        (void)glsd301p_runtime_core_latch_fault(ctx->runtime, off);
        (void)glsd301p_uart_transport_offer(ctx->transport, off, now_ms);
        glsd301p_control_sync_from_runtime(ctx);
        return false;
    }

    glsd301p_control_sync_from_runtime(ctx);
    return true;
}

uint8_t glsd301p_control_clamp_level(const glsd301p_control_ctx_t *ctx,
                                     uint16_t level)
{
    uint8_t min_level;
    uint8_t max_level;

    if (ctx == NULL) {
        return (uint8_t)(level > 0xFEu ? 0xFEu : level);
    }
    min_level = ctx->min_level;
    max_level = ctx->max_level;
    if (level < min_level) {
        return min_level;
    }
    if (level > max_level) {
        return max_level;
    }
    return (uint8_t)level;
}

void glsd301p_control_level_cancel(glsd301p_control_ctx_t *ctx)
{
    if (ctx == NULL || ctx->level == NULL) {
        return;
    }

    ctx->level->mode = GLSD301P_LEVEL_IDLE;
    ctx->level->rate_accum_tenths = 0u;
    ctx->level->remaining_time = 0u;
    glsd301p_timer_level_stop();
}

static uint8_t glsd301p_control_apply_level(glsd301p_control_ctx_t *ctx,
                                            uint8_t level,
                                            uint8_t with_onoff,
                                            uint8_t direction_up,
                                            uint32_t now_ms)
{
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];
    bool output;

    if (ctx == NULL || ctx->runtime == NULL) {
        return 0u;
    }
    output = ctx->runtime->logical_output_enabled;

    /*
     * 0xFF is Zigbee's reserved/unknown Level value. Never normalize it
     * into an energizing value; an impossible internal state fails closed.
     */
    if (level == GLSD301P_ZCL_LEVEL_UNKNOWN) {
        (void)glsd301p_runtime_core_latch_fault(ctx->runtime, frame);
        if (ctx->transport != NULL) {
            (void)glsd301p_uart_transport_offer(ctx->transport, frame, now_ms);
        }
        glsd301p_control_sync_from_runtime(ctx);
        return 0u;
    }

    level = glsd301p_control_clamp_level(ctx, level);
    if (with_onoff) {
        if (direction_up || level > ctx->min_level) {
            output = true;
        } else if (level <= ctx->min_level) {
            output = false;
        }
    }

    return glsd301p_control_emit(
        ctx,
        glsd301p_runtime_core_apply_state(ctx->runtime, output, level,
                                          ctx->min_level, false, frame),
        frame, now_ms);
}

bool glsd301p_control_level_start_target(glsd301p_control_ctx_t *ctx,
                                         uint8_t target,
                                         uint16_t transition_time,
                                         uint8_t with_onoff)
{
    if (ctx == NULL || ctx->level == NULL) {
        return false;
    }
    /* Refusal leaves any running transition untouched. */
    if (!glsd301p_control_ready(ctx)) {
        return false;
    }

    glsd301p_control_level_cancel(ctx);
    target = glsd301p_control_clamp_level(ctx, target);

    if (transition_time == 0u || transition_time == 0xFFFFu ||
        target == ctx->level->current_level) {
        return glsd301p_control_apply_level(
                   ctx, target, with_onoff,
                   (uint8_t)(target >= ctx->level->current_level ? 1u : 0u),
                   glsd301p_timebase_now_ms()) != 0u;
    }

    ctx->level->mode = GLSD301P_LEVEL_TARGET;
    ctx->level->target = target;
    ctx->level->with_onoff = with_onoff;
    ctx->level->remaining_time = transition_time;
    if (!glsd301p_timer_level_start(glsd301p_control_level_cb, ctx)) {
        glsd301p_control_level_cancel(ctx);
        return false;
    }
    return true;
}

bool glsd301p_control_level_start_move(glsd301p_control_ctx_t *ctx,
                                       uint8_t direction_up,
                                       uint8_t rate,
                                       uint8_t with_onoff)
{
    if (ctx == NULL || ctx->level == NULL) {
        return false;
    }
    if (!glsd301p_control_ready(ctx)) {
        return false;
    }

    glsd301p_control_level_cancel(ctx);
    if (rate == 0u) {
        return true;
    }

    ctx->level->mode = GLSD301P_LEVEL_MOVE;
    ctx->level->direction_up = direction_up;
    ctx->level->rate = rate;
    ctx->level->rate_accum_tenths = 0u;
    ctx->level->with_onoff = with_onoff;
    ctx->level->remaining_time = 0xFFFFu;

    if (with_onoff && direction_up && !ctx->runtime->logical_output_enabled) {
        (void)glsd301p_control_apply_level(
            ctx, ctx->level->current_level, 1u, 1u,
            glsd301p_timebase_now_ms());
    }

    if (!glsd301p_timer_level_start(glsd301p_control_level_cb, ctx)) {
        glsd301p_control_level_cancel(ctx);
        return false;
    }
    return true;
}

int glsd301p_control_level_cb(void *data)
{
    glsd301p_control_ctx_t *ctx = (glsd301p_control_ctx_t *)data;
    uint32_t now_ms;
    uint8_t next;

    if (ctx == NULL || ctx->level == NULL) {
        return 0;
    }
    now_ms = glsd301p_timebase_now_ms();

    /* No stale transition may survive: every stop resets the mode. */
    if (ctx->level->mode == GLSD301P_LEVEL
...[truncated 3869 chars]