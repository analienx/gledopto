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
        level->rate_accum_milli = 0u;
        level->direction_up = 0u;
        level->with_onoff = 0u;
        level->current_level = boot_level;
        level->remaining_time = 0u;
        level->trans_origin = boot_level;
        level->trans_start_ms = 0u;
        level->trans_dur_ms = 0u;
        level->move_last_ms = 0u;
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
    ctx->level->rate_accum_milli = 0u;
    ctx->level->remaining_time = 0u;
    ctx->level->trans_dur_ms = 0u;
    ctx->level->move_last_ms = 0u;
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

    /*
     * Duration policy: 0 means immediate; reserved 0xFFFF ("as fast as
     * possible") is also immediate; an already-reached target applies
     * once for its with_onoff side effects. Anything else interpolates.
     */
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
    ctx->level->trans_origin = ctx->level->current_level;
    ctx->level->trans_start_ms = glsd301p_timebase_now_ms();
    ctx->level->trans_dur_ms = (uint32_t)transition_time * 100u;
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
    ctx->level->rate_accum_milli = 0u;
    ctx->level->with_onoff = with_onoff;
    ctx->level->move_last_ms = glsd301p_timebase_now_ms();
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
    if (ctx->level->mode == GLSD301P_LEVEL_IDLE ||
        !glsd301p_runtime_core_is_ready(ctx->runtime)) {
        glsd301p_control_level_cancel(ctx);
        return -1;
    }
    next = ctx->level->current_level;

    if (ctx->level->mode == GLSD301P_LEVEL_TARGET) {
        if (ctx->level->current_level == ctx->level->target) {
            glsd301p_control_level_cancel(ctx);
            return -1;
        }
        /*
         * R4: bounded integer interpolation on elapsed time. Floor keeps
         * the offset strictly below diff until the duration elapses, so
         * small deltas cannot finish early and nothing overshoots:
         * diff * elapsed <= 254 * 6553499 < 2^32, offset <= 254.
         */
        {
            uint32_t elapsed = glsd301p_timebase_age_ms(
                ctx->level->trans_start_ms, now_ms);
            uint16_t diff;
            uint32_t offset;
            diff = ctx->level->trans_origin > ctx->level->target
                       ? (uint16_t)(ctx->level->trans_origin -
                                    ctx->level->target)
                       : (uint16_t)(ctx->level->target -
                                    ctx->level->trans_origin);
            if (elapsed >= ctx->level->trans_dur_ms) {
                next = ctx->level->target;
                ctx->level->remaining_time = 0u;
            } else {
                offset = (uint32_t)diff * elapsed /
                         ctx->level->trans_dur_ms;
                if (ctx->level->trans_origin > ctx->level->target) {
                    next = (uint8_t)((uint32_t)ctx->level->trans_origin -
                                     offset);
                } else {
                    next = (uint8_t)((uint32_t)ctx->level->trans_origin +
                                     offset);
                }
                ctx->level->remaining_time = (uint16_t)(
                    (ctx->level->trans_dur_ms - elapsed + 99u) / 100u);
            }
        }
        if (glsd301p_control_apply_level(
                ctx, next, ctx->level->with_onoff,
                (uint8_t)(next >= ctx->level->current_level ? 1u : 0u),
                now_ms) == 0u) {
            glsd301p_control_level_cancel(ctx);
            return -1;
        }
        if (next == ctx->level->target) {
            glsd301p_control_level_cancel(ctx);
            return -1;
        }
        return 0;
    }

    if (ctx->level->mode == GLSD301P_LEVEL_MOVE) {
        /*
         * R4: the ZCL rate (levels/second) integrates over elapsed ms.
         * Divide-first keeps every product in u32 for any gap:
         * whole seconds * rate <= 4294967 * 255 < 2^32. Delta saturates
         * at 0xFF: any larger step hits a bound below.
         */
        uint32_t elapsed = glsd301p_timebase_age_ms(
            ctx->level->move_last_ms, now_ms);
        uint32_t delta = (elapsed / 1000u) * (uint32_t)ctx->level->rate;
        uint8_t step;
        ctx->level->move_last_ms = now_ms;
        ctx->level->rate_accum_milli +=
            (uint32_t)ctx->level->rate * (elapsed % 1000u);
        delta += ctx->level->rate_accum_milli / 1000u;
        ctx->level->rate_accum_milli %= 1000u;
        step = delta > 0xFFu ? 0xFFu : (uint8_t)delta;
        if (ctx->level->direction_up) {
            next = glsd301p_control_clamp_level(
                ctx, (uint16_t)ctx->level->current_level + step);
        } else {
            next = ctx->level->current_level > step
                       ? (uint8_t)(ctx->level->current_level - step)
                       : ctx->min_level;
        }
        if (glsd301p_control_apply_level(ctx, next, ctx->level->with_onoff,
                                         ctx->level->direction_up,
                                         now_ms) == 0u) {
            glsd301p_control_level_cancel(ctx);
            return -1;
        }
        if ((ctx->level->direction_up && next >= ctx->max_level) ||
            (!ctx->level->direction_up && next <= ctx->min_level)) {
            glsd301p_control_level_cancel(ctx);
            return -1;
        }
        return 0;
    }

    glsd301p_control_level_cancel(ctx);
    return -1;
}

void glsd301p_control_io_step(glsd301p_control_ctx_t *ctx,
                              bool pc2_high,
                              bool pb4_high,
                              uint32_t now_ms)
{
    glsd301p_runtime_result_t result;
    glsd301p_uart_service_event_t ev;
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];
    bool took_control = false;

    if (ctx == NULL || ctx->runtime == NULL || ctx->transport == NULL ||
        ctx->uart == NULL) {
        return;
    }

    /* Delayed IO gaps are recorded honestly, never interpolated. */
    if (ctx->io_serviced_once) {
        uint32_t gap = glsd301p_timebase_age_ms(ctx->io_last_ms, now_ms);
        if (gap > ctx->io_max_gap_ms) {
            ctx->io_max_gap_ms = gap;
        }
    } else {
        ctx->io_serviced_once = true;
    }
    ctx->io_last_ms = now_ms;

    /* Both physical inputs are sampled before the UART transport moves. */
    result = glsd301p_runtime_core_poll_push_ex(ctx->runtime, pc2_high,
                                                &took_control, frame);
    if (took_control) {
        glsd301p_control_level_cancel(ctx);
    }
    if (result != GLSD301P_RUNTIME_NO_FRAME) {
        (void)glsd301p_control_emit(ctx, result, frame, now_ms);
    }

    result = glsd301p_runtime_core_poll_pb4(ctx->runtime, pb4_high, frame);
    if (result != GLSD301P_RUNTIME_NO_FRAME) {
        (void)glsd301p_control_emit(ctx, result, frame, now_ms);
    }

    ev = glsd301p_uart_service_step(ctx->uart, ctx->transport, now_ms);
    if (ev.fault_raised) {
        (void)glsd301p_runtime_core_latch_fault(ctx->runtime, frame);
        (void)glsd301p_uart_transport_offer(ctx->transport, frame, now_ms);
        /* R1: a faulted device must not keep a transition alive that could
         * re-energize later; the OFF above is the only recovery traffic. */
        glsd301p_control_level_cancel(ctx);
        glsd301p_control_sync_from_runtime(ctx);
    }
    if (ev.boot_completed) {
        /*
         * ON readiness is unreachable unless the essential IO event is
         * registered; reaching this step proves it, and the check below
         * keeps that ordering explicit for future edits.
         */
        if (!glsd301p_timer_io_registered()) {
            return;
        }
        result = glsd301p_runtime_core_restore_state(
            ctx->runtime, false, ctx->boot_level, ctx->boot_min, false,
            frame);
        if (result == GLSD301P_RUNTIME_FRAME_READY) {
            (void)glsd301p_control_emit(ctx, result, frame, now_ms);
        } else {
            (void)glsd301p_runtime_core_latch_fault(ctx->runtime, frame);
            (void)glsd301p_uart_transport_offer(ctx->transport, frame,
                                                now_ms);
            glsd301p_control_sync_from_runtime(ctx);
        }
    }
}

int glsd301p_control_io_cb(void *data)
{
    glsd301p_control_ctx_t *ctx = (glsd301p_control_ctx_t *)data;
    bool pc2;
    bool pb4;

    if (ctx == NULL) {
        return 0;
    }
    pc2 = glsd301p_hw_gpio_pc2_high();
    pb4 = glsd301p_hw_gpio_pb4_high();
    glsd301p_control_io_step(ctx, pc2, pb4, glsd301p_timebase_now_ms());
    return 0;
}
