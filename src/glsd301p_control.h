#ifndef GLSD301P_CONTROL_H
#define GLSD301P_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "glsd301p_identify.h"
#include "glsd301p_runtime_core.h"
#include "glsd301p_uart_service.h"
#include "glsd301p_uart_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared GL-SD-301P control plane: IO service, Level transition engine and
 * the guarded emit path. This code is SDK-independent (hardware reaches it
 * only through glsd301p_hw_io.h) and compiles unmodified into both the
 * TC32 firmware and the hosted harnesses, which drive it with scripted
 * inputs, stub hardware and the real pinned SDK timer bodies.
 *
 * The target owns exactly one context plus the module instances it points
 * to, registers the two callbacks below on the owned static IO/Level timer
 * events, and routes Zigbee cluster commands into the start/cancel calls.
 * ZCL attribute mirrors are written directly: CurrentLevel/RemainingTime
 * live in the level state itself, OnOff through onoff_mirror.
 */

typedef enum {
    GLSD301P_LEVEL_IDLE = 0,
    GLSD301P_LEVEL_TARGET,
    GLSD301P_LEVEL_MOVE
} glsd301p_level_mode_t;

/*
 * Elapsed-time transition state (R4). TARGET transitions interpolate from
 * (origin, start, duration) on the millisecond timebase: duration is
 * transition_time tenths converted to ms (transition_time 0 and reserved
 * 0xFFFF both mean immediate, documented at start_target). MOVE integrates
 * the ZCL rate (levels/second) over elapsed ms into a milli-level
 * accumulator. Both survive callback delays and uint32 wrap; arithmetic
 * is bounded u32 with saturation, never overshooting.
 *
 * R9: target_dir retains the TARGET command's actual direction (+1 up,
 * -1 down, 0 equal) from dispatch through every interpolation tick, so
 * With On/Off effects never derive direction from rounded samples and
 * equality never invents an increase. direction_up keeps serving MOVE.
 */
typedef struct {
    uint8_t mode;
    uint8_t target;
    uint8_t rate;
    uint32_t rate_accum_milli;
    uint8_t direction_up;
    int8_t target_dir;
    uint8_t with_onoff;
    uint8_t current_level;
    uint16_t remaining_time;
    uint8_t trans_origin;
    uint32_t trans_start_ms;
    uint32_t trans_dur_ms;
    uint32_t move_last_ms;
} glsd301p_level_state_t;

typedef struct {
    glsd301p_runtime_core_t *runtime;
    glsd301p_uart_transport_t *transport;
    glsd301p_uart_service_t *uart;
    glsd301p_level_state_t *level;
    uint8_t *onoff_mirror;
    uint8_t min_level;
    uint8_t max_level;
    uint8_t boot_level;
    uint8_t boot_min;
    bool io_serviced_once;
    uint32_t io_last_ms;
    uint32_t io_max_gap_ms;
    /*
     * R13: the control plane hosts the Identify adapter. identify_store
     * binds the ZCL IdentifyTime attribute (NULL until bound); the
     * household IO tick polls the countdown through it.
     */
    glsd301p_identify_t identify;
    uint16_t *identify_store;
} glsd301p_control_ctx_t;

void glsd301p_control_init(glsd301p_control_ctx_t *ctx,
                           glsd301p_runtime_core_t *runtime,
                           glsd301p_uart_transport_t *transport,
                           glsd301p_uart_service_t *uart,
                           glsd301p_level_state_t *level,
                           uint8_t *onoff_mirror,
                           uint8_t min_level,
                           uint8_t max_level,
                           uint8_t boot_level,
                           uint8_t boot_min);

/*
 * Emit exactly one boot OFF frame through a single bounded DMA attempt.
 * Returns true when hardware accepted the transfer (completion is observed
 * by later IO steps); false leaves the runtime untouched for the caller to
 * fail closed via glsd301p_control_boot_failed().
 */
bool glsd301p_control_boot_off(glsd301p_control_ctx_t *ctx, uint32_t now_ms);

/* Fail-closed boot path: latch the runtime fault, prioritize OFF, sync. */
void glsd301p_control_boot_failed(glsd301p_control_ctx_t *ctx, uint32_t now_ms);

/*
 * One IO service call: sample handling for both physical inputs, UART
 * service, boot observation and IO gap tracking. Physical PUSH takeover
 * cancels any remote Level transition before it can re-energize.
 */
void glsd301p_control_io_step(glsd301p_control_ctx_t *ctx,
                              bool pc2_high,
                              bool pb4_high,
                              uint32_t now_ms);

/* Owned static 1 ms IO event callback. data must be the control context. */
int glsd301p_control_io_cb(void *data);

/*
 * Level transition calls. Each start validates readiness first and returns
 * false without touching transition state when refused; the caller then
 * reports failure and must not register timer activity. Cancellation is
 * idempotent and always leaves mode IDLE with no timer registered.
 */
bool glsd301p_control_level_start_target(glsd301p_control_ctx_t *ctx,
                                         uint8_t target,
                                         uint16_t transition_time,
                                         uint8_t with_onoff);
bool glsd301p_control_level_start_move(glsd301p_control_ctx_t *ctx,
                                       uint8_t direction_up,
                                       uint8_t rate,
                                       uint8_t with_onoff);
void glsd301p_control_level_cancel(glsd301p_control_ctx_t *ctx);

/* Owned static 100 ms Level event callback. data must be the context. */
int glsd301p_control_level_cb(void *data);

/*
 * Shared guarded emit path. Returns true on the success path (mirrors
 * synced), false when the caller must report failure. A not-ready runtime
 * refuses without latching: OFF already holds by construction.
 */
bool glsd301p_control_emit(glsd301p_control_ctx_t *ctx,
                           glsd301p_runtime_result_t result,
                           uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE],
                           uint32_t now_ms);

uint8_t glsd301p_control_clamp_level(const glsd301p_control_ctx_t *ctx,
                                     uint16_t level);

/*
 * R16: proportional Step/MoveToLevel time after target clipping.
 * Returns ceil(duration * moved_span / requested_span) when a finite
 * nonzero duration covers a clipped move (0 < moved < requested);
 * otherwise returns duration unchanged (immediate/reserved
 * conventions, unclipped and fully-clipped moves keep their
 * semantics; fully-clipped targets short-circuit as immediate in
 * start_target). Bounded u32 arithmetic, no division by zero.
 */
uint16_t glsd301p_control_proportional_time(uint16_t requested_span,
                                            uint16_t moved_span,
                                            uint16_t duration);

/*
 * R13 Identify wiring. The store binds the ZCL IdentifyTime attribute;
 * effect_start validates readiness and the effect id, cancels any
 * running transition, and renders on the owned Level timer; it returns
 * false without touching effect state when refused. effect_abort ends
 * the program without emitting (the aborting cause always emits, except
 * the emit-less Stop path, which restores explicitly) and reports
 * whether a program was active. effect_restore re-emits the saved
 * pre-effect output through the guarded path.
 */
void glsd301p_control_identify_bind_store(glsd301p_control_ctx_t *ctx,
                                          uint16_t *store);
bool glsd301p_control_identify_effect_start(glsd301p_control_ctx_t *ctx,
                                            uint8_t effect_id,
                                            uint32_t now_ms);
bool glsd301p_control_identify_effect_abort(glsd301p_control_ctx_t *ctx);
void glsd301p_control_identify_effect_restore(glsd301p_control_ctx_t *ctx,
                                              uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_CONTROL_H */
