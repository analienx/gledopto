#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "host_types.h"
#include "ev_timer.h"

#include "glsd301p_control.h"
#include "glsd301p_hw_io.h"
#include "glsd301p_runtime_core.h"
#include "glsd301p_timebase.h"
#include "glsd301p_timer_events.h"
#include "glsd301p_uart_service.h"
#include "glsd301p_uart_transport.h"

#include "hw_stub.h"

static const uint8_t OFF[] = {0xA5, 0x5A, 0x01, 0x00, 0x04, 0xAA};
static const uint8_t ON_FE[] = {0xA5, 0x5A, 0x01, 0xFE, 0x04, 0xAA};

static glsd301p_runtime_core_t g_runtime;
static glsd301p_uart_transport_t g_transport;
static glsd301p_uart_service_t g_uart;
static glsd301p_level_state_t g_level;
static glsd301p_control_ctx_t g_ctx;
static uint8_t g_onoff;

static void fixture_init(void)
{
    host_stub_reset();
    host_clock_set(0u);
    ev_timer_init();
    ev_timer_setPrevSysTick(0u);
    glsd301p_timebase_init();
    glsd301p_timer_events_init();
    glsd301p_runtime_core_init(&g_runtime);
    glsd301p_uart_transport_init(&g_transport);
    glsd301p_uart_service_init(&g_uart);
    glsd301p_control_init(&g_ctx, &g_runtime, &g_transport, &g_uart,
                          &g_level, &g_onoff, 0x02u, 0xFEu, 0xFEu, 0x02u);
    host_gpio_set(true, false);
}

static void pump_ms(uint32_t ms)
{
    uint32_t i;

    for (i = 0u; i < ms; i++) {
        host_clock_advance(HOST_TICKS_PER_MS);
        ev_timer_process();
    }
}

static void drive_gpio_ms(bool pc2_high, uint32_t ms)
{
    host_gpio_set(pc2_high, false);
    pump_ms(ms);
}

static void push_short_press(void)
{
    drive_gpio_ms(false, 3u);
    drive_gpio_ms(true, 6u);
    drive_gpio_ms(false, 3u);
    drive_gpio_ms(true, 51u);
}

static void boot_via_pump(void)
{
    assert(glsd301p_timer_io_start(glsd301p_control_io_cb, &g_ctx));
    assert(glsd301p_control_boot_off(&g_ctx, glsd301p_timebase_now_ms()));
    host_uart_set_busy(true);
    pump_ms(6u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
    host_uart_set_busy(false);
    pump_ms(1u);
    assert(glsd301p_runtime_core_is_ready(&g_runtime));
    assert(g_onoff == 0u);
    assert(g_level.current_level == 0xFEu);
}

static void turn_on(uint8_t level)
{
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];
    bool ok;

    ok = glsd301p_control_emit(
        &g_ctx,
        glsd301p_runtime_core_apply_state(&g_runtime, true, level, 0x02u,
                                          false, frame),
        frame, glsd301p_timebase_now_ms());
    assert(ok);
}

static int dummy_cb(void *data)
{
    (void)data;
    return -1;
}

static void test_boot_arms_after_off_completion(void)
{
    uint8_t last[6];

    fixture_init();
    boot_via_pump();

    turn_on(0xFEu);
    assert(g_onoff == 1u);
    /* Boot restore queued OFF first: OFF priority holds on the wire. */
    pump_ms(1u);
    assert(host_uart_last_frame(last));
    assert(memcmp(last, OFF, sizeof(last)) == 0);
    pump_ms(1u);
    assert(host_uart_last_frame(last));
    assert(memcmp(last, ON_FE, sizeof(last)) == 0);
    assert(!glsd301p_uart_transport_has_pending(&g_transport));
}

static void test_boot_reject_locks_off(void)
{
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];

    fixture_init();
    assert(glsd301p_timer_io_start(glsd301p_control_io_cb, &g_ctx));
    host_uart_set_send_accepts(false);
    assert(!glsd301p_control_boot_off(&g_ctx, glsd301p_timebase_now_ms()));
    glsd301p_control_boot_failed(&g_ctx, glsd301p_timebase_now_ms());

    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
    assert(g_onoff == 0u);

    /* ON is refused; OFF still reports through the refused emit path. */
    assert(!glsd301p_control_emit(
        &g_ctx,
        glsd301p_runtime_core_apply_state(&g_runtime, true, 0xFEu, 0x02u,
                                          false, frame),
        frame, glsd301p_timebase_now_ms()));

    /* Bounded OFF retry while the link stays down. */
    assert(glsd301p_uart_transport_has_pending(&g_transport));
    pump_ms(5u);
    assert(host_uart_send_attempts() == 6u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
}

static void test_boot_timeout_fault(void)
{
    fixture_init();
    assert(glsd301p_timer_io_start(glsd301p_control_io_cb, &g_ctx));
    assert(glsd301p_control_boot_off(&g_ctx, glsd301p_timebase_now_ms()));
    host_uart_set_busy(true);
    pump_ms(31u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 0u);
    pump_ms(1u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 1u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
    assert(g_onoff == 0u);
    /* Never armed afterwards. */
    host_uart_set_busy(false);
    pump_ms(10u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
}

static void test_not_ready_refuses_without_latching(void)
{
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];
    glsd301p_runtime_result_t result;

    fixture_init();
    assert(glsd301p_timer_io_start(glsd301p_control_io_cb, &g_ctx));

    /* An ON attempt before boot completion refuses cleanly (F10). */
    result = glsd301p_runtime_core_apply_state(&g_runtime, true, 0xFEu,
                                               0x02u, false, frame);
    assert(result == GLSD301P_RUNTIME_FORCED_OFF);
    assert(!glsd301p_control_emit(&g_ctx, result, frame,
                                  glsd301p_timebase_now_ms()));

    /* The refusal latched nothing: normal boot still arms afterwards. */
    assert(glsd301p_control_boot_off(&g_ctx, glsd301p_timebase_now_ms()));
    host_uart_set_busy(true);
    pump_ms(6u);
    host_uart_set_busy(false);
    pump_ms(1u);
    assert(glsd301p_runtime_core_is_ready(&g_runtime));
}

static void test_push_cancels_level_transition(void)
{
    uint8_t last[6];

    fixture_init();
    boot_via_pump();
    turn_on(0xFEu);
    pump_ms(1u);

    assert(glsd301p_control_level_start_target(&g_ctx, 0x64u, 100u, 0u));
    assert(glsd301p_timer_level_registered());
    pump_ms(500u);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.current_level < 0xFEu);

    push_short_press();

    /* TOGGLE took control: transition gone, output OFF, OFF on the wire. */
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
    assert(!glsd301p_timer_level_registered());
    assert(g_level.remaining_time == 0u);
    assert(g_onoff == 0u);
    assert(host_uart_last_frame(last));
    assert(memcmp(last, OFF, sizeof(last)) == 0);
}

static void test_level_target_progresses_and_completes(void)
{
    fixture_init();
    boot_via_pump();
    turn_on(0xFEu);

    assert(glsd301p_control_level_start_target(&g_ctx, 0xF4u, 10u, 0u));
    pump_ms(500u);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.current_level < 0xFEu);
    assert(g_level.current_level > 0xF4u);
    pump_ms(500u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
    assert(!glsd301p_timer_level_registered());
    assert(g_level.current_level == 0xF4u);
    assert(g_level.remaining_time == 0u);
    assert(g_onoff == 1u);
}

static void test_level_immediate_with_onoff(void)
{
    fixture_init();
    boot_via_pump();
    assert(g_onoff == 0u);

    assert(glsd301p_control_level_start_target(&g_ctx, 0x64u, 0u, 1u));
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
    assert(!glsd301p_timer_level_registered());
    assert(g_level.current_level == 0x64u);
    assert(g_onoff == 1u);

    /* A level-only immediate move while ON keeps the output energized. */
    assert(glsd301p_control_level_start_target(&g_ctx, 0x70u, 0xFFFFu, 0u));
    assert(g_level.current_level == 0x70u);
    assert(g_onoff == 1u);
}

static void test_level_refused_when_not_ready(void)
{
    fixture_init();

    assert(!glsd301p_control_level_start_target(&g_ctx, 0x64u, 10u, 0u));
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
    assert(!glsd301p_timer_level_registered());
    assert(!glsd301p_control_level_start_move(&g_ctx, 1u, 10u, 0u));
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
    assert(!glsd301p_timer_level_registered());
}

static void test_fault_mid_transition_stops_clean(void)
{
    fixture_init();
    boot_via_pump();
    turn_on(0xFEu);
    pump_ms(1u);

    assert(glsd301p_control_level_start_move(&g_ctx, 0u, 20u, 0u));
    pump_ms(200u);
    assert(g_level.mode == GLSD301P_LEVEL_MOVE);
    assert(g_level.current_level < 0xFEu);

    /* Wedge the UART: the elapsed deadline latches the runtime fault. */
    assert(glsd301p_uart_transport_offer(&g_transport, ON_FE,
                                         glsd301p_timebase_now_ms()));
    host_uart_set_busy(true);
    pump_ms(32u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 1u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
    assert(g_onoff == 0u);

    /*
     * The next Level tick observes not-ready and leaves no stale mode.
     * Ticks run every 100 ms from registration, so cross one boundary.
     */
    pump_ms(100u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
    assert(!glsd301p_timer_level_registered());
    assert(g_level.remaining_time == 0u);
    pump_ms(300u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
    assert(!glsd301p_timer_level_registered());
}

static void test_elapsed_deadline_across_two_ticks(void)
{
    fixture_init();
    assert(glsd301p_timer_io_start(glsd301p_control_io_cb, &g_ctx));
    host_uart_set_busy(true);

    /* Boot first so the runtime is armed for this direct-step test. */
    assert(glsd301p_control_boot_off(&g_ctx, 0u));
    host_uart_set_busy(true);
    glsd301p_control_io_step(&g_ctx, true, false, 0u);
    host_uart_set_busy(false);
    glsd301p_control_io_step(&g_ctx, true, false, 7u);
    assert(glsd301p_runtime_core_is_ready(&g_runtime));

    assert(glsd301p_uart_transport_offer(&g_transport, ON_FE, 7u));
    host_uart_set_busy(true);

    /* Two further steps 24 ms apart: still inside the deadline. */
    glsd301p_control_io_step(&g_ctx, true, false, 31u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 0u);

    /* One more step past 32 ms of elapsed age: fault, after 4 callbacks. */
    glsd301p_control_io_step(&g_ctx, true, false, 40u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 1u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));

    /* The delayed gap is recorded, not interpolated. */
    assert(g_ctx.io_max_gap_ms == 24u);
    assert(g_ctx.io_last_ms == 40u);
}

static void test_stale_inputs_keep_transition_intact(void)
{
    fixture_init();
    boot_via_pump();
    turn_on(0xFEu);
    assert(glsd301p_control_level_start_target(&g_ctx, 0x64u, 100u, 1u));
    pump_ms(2u);
    assert(!glsd301p_uart_transport_has_pending(&g_transport));

    /*
     * A 200 ms service gap delivers exactly two PC2 samples. Two lows
     * cannot complete a PUSH waveform, so no takeover fires and the
     * transition survives. The 991 ms jump out of the 9 ms pump era is
     * recorded honestly as the maximum gap.
     */
    glsd301p_control_io_step(&g_ctx, false, false, 1000u);
    glsd301p_control_io_step(&g_ctx, false, false, 1200u);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(glsd301p_timer_level_registered());
    assert(g_ctx.io_last_ms == 1200u);
    assert(g_ctx.io_max_gap_ms == 991u);
    assert(g_onoff == 1u);
}

static void test_deadline_survives_uint32_wrap(void)
{
    fixture_init();
    boot_via_pump();
    turn_on(0xFEu);
    pump_ms(2u);
    assert(!glsd301p_uart_transport_has_pending(&g_transport));
    host_uart_set_busy(true);

    assert(glsd301p_uart_transport_offer(&g_transport, ON_FE, 0xFFFFFFF0u));
    glsd301p_control_io_step(&g_ctx, true, false, 0xFFFFFFFFu);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 0u);
    glsd301p_control_io_step(&g_ctx, true, false, 0x00000010u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 1u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
}

static void test_static_io_works_with_full_pool(void)
{
    ev_timer_event_t *held[24];
    uint8_t last[6];
    unsigned i;

    fixture_init();
    for (i = 0u; i < 24u; i++) {
        held[i] = ev_timer_taskPost(dummy_cb, NULL, 0xFFFFFFu);
        assert(held[i] != NULL);
    }
    assert(ev_timer_taskPost(dummy_cb, NULL, 1u) == NULL);

    boot_via_pump();
    turn_on(0xFEu);
    pump_ms(1u);
    assert(host_uart_last_frame(last));
    assert(memcmp(last, OFF, sizeof(last)) == 0);
    pump_ms(1u);
    assert(host_uart_last_frame(last));
    assert(memcmp(last, ON_FE, sizeof(last)) == 0);

    for (i = 0u; i < 24u; i++) {
        (void)ev_timer_taskCancel(&held[i]);
        assert(held[i] == NULL);
    }
}

/* Direct-step armed boot with a drained queue and an idle link. */
static void boot_armed_direct(void)
{
    fixture_init();
    assert(glsd301p_timer_io_start(glsd301p_control_io_cb, &g_ctx));
    assert(glsd301p_control_boot_off(&g_ctx, 0u));
    host_uart_set_busy(true);
    glsd301p_control_io_step(&g_ctx, true, false, 0u);
    host_uart_set_busy(false);
    glsd301p_control_io_step(&g_ctx, true, false, 7u);
    assert(glsd301p_runtime_core_is_ready(&g_runtime));
    glsd301p_control_io_step(&g_ctx, true, false, 8u);
    assert(!glsd301p_uart_transport_has_pending(&g_transport));
}

/*
 * R1 through the full control/service/transport chain: DMA rejects until
 * age 31, accepts exactly at the deadline. No ON may start; the running
 * transition dies at the fault step; later ON is refused; OFF recovers.
 */
static void test_r1_chain_reject_accept_with_transition(void)
{
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];
    uint8_t last[6];
    glsd301p_runtime_result_t result;
    uint32_t base;

    boot_armed_direct();
    turn_on(0xFEu);
    glsd301p_control_io_step(&g_ctx, true, false, 9u);
    assert(!glsd301p_uart_transport_has_pending(&g_transport));
    assert(glsd301p_control_level_start_target(&g_ctx, 0x64u, 100u, 0u));
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(glsd301p_timer_level_registered());
    base = host_uart_accepted_count();

    assert(glsd301p_uart_transport_offer(&g_transport, ON_FE, 100u));
    host_uart_set_send_accepts(false);
    glsd301p_control_io_step(&g_ctx, true, false, 100u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 0u);
    glsd301p_control_io_step(&g_ctx, true, false, 131u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 0u);
    assert(host_uart_accepted_count() == base);

    host_uart_set_send_accepts(true);
    glsd301p_control_io_step(&g_ctx, true, false, 132u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 1u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
    assert(g_onoff == 0u);
    /* The fault step itself accepts nothing: no ON after the deadline. */
    assert(host_uart_accepted_count() == base);
    /* The transition is canceled at the fault, not at the next tick. */
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
    assert(!glsd301p_timer_level_registered());
    assert(g_level.remaining_time == 0u);

    /* A later ON is refused without latching anything new. */
    result = glsd301p_runtime_core_apply_state(&g_runtime, true, 0xFEu,
                                               0x02u, false, frame);
    assert(!glsd301p_control_emit(&g_ctx, result, frame, 132u));
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 1u);

    /* Bounded confirmed-OFF recovery flows on the following steps. */
    glsd301p_control_io_step(&g_ctx, true, false, 133u);
    assert(host_uart_accepted_count() == base + 1u);
    assert(host_uart_last_frame(last));
    assert(memcmp(last, OFF, sizeof(last)) == 0);
}

/* R1 busy-to-idle variant across a uint32 wrap, asserting the wire log. */
static void test_r1_chain_busy_idle_wrap(void)
{
    uint8_t last[6];
    uint32_t base;

    boot_armed_direct();
    base = host_uart_accepted_count();

    assert(glsd301p_uart_transport_offer(&g_transport, ON_FE, 0xFFFFFFF0u));
    host_uart_set_busy(true);
    glsd301p_control_io_step(&g_ctx, true, false, 0xFFFFFFF0u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 0u);
    glsd301p_control_io_step(&g_ctx, true, false, 0xFFFFFFFFu);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 0u);

    host_uart_set_busy(false);
    glsd301p_control_io_step(&g_ctx, true, false, 0x00000010u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 1u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
    assert(host_uart_accepted_count() == base);

    glsd301p_control_io_step(&g_ctx, true, false, 0x00000011u);
    assert(host_uart_accepted_count() == base + 1u);
    assert(host_uart_last_frame(last));
    assert(memcmp(last, OFF, sizeof(last)) == 0);
}

/*
 * R2 through the full chain: the t=100 accepted transfer stays busy while
 * fresh traffic arrives at t=131. The original deadline still trips at
 * t=132; recovery sends only OFF afterwards.
 */
static void test_r2_chain_inflight_with_fresh_traffic(void)
{
    uint8_t logged[6];
    uint32_t base;

    boot_armed_direct();
    turn_on(0xFEu);
    glsd301p_control_io_step(&g_ctx, true, false, 9u);
    assert(!glsd301p_uart_transport_has_pending(&g_transport));
    base = host_uart_accepted_count();

    assert(glsd301p_uart_transport_offer(&g_transport, ON_FE, 100u));
    glsd301p_control_io_step(&g_ctx, true, false, 100u);
    assert(host_uart_accepted_count() == base + 1u);

    host_uart_set_busy(true);
    glsd301p_control_io_step(&g_ctx, true, false, 101u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 0u);
    assert(glsd301p_uart_transport_offer(&g_transport, ON_FE, 131u));
    glsd301p_control_io_step(&g_ctx, true, false, 132u);
    assert(glsd301p_uart_service_deadline_faults(&g_uart) == 1u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
    assert(g_onoff == 0u);

    host_uart_set_busy(false);
    glsd301p_control_io_step(&g_ctx, true, false, 133u);
    assert(host_uart_accepted_count() == base + 2u);
    assert(host_uart_accepted_frame(base, logged));
    assert(memcmp(logged, ON_FE, sizeof(logged)) == 0);
    assert(host_uart_accepted_frame(base + 1u, logged));
    assert(memcmp(logged, OFF, sizeof(logged)) == 0);
}

int main(void)
{
    test_boot_arms_after_off_completion();
    test_boot_reject_locks_off();
    test_boot_timeout_fault();
    test_not_ready_refuses_without_latching();
    test_push_cancels_level_transition();
    test_level_target_progresses_and_completes();
    test_level_immediate_with_onoff();
    test_level_refused_when_not_ready();
    test_fault_mid_transition_stops_clean();
    test_elapsed_deadline_across_two_ticks();
    test_stale_inputs_keep_transition_intact();
    test_deadline_survives_uint32_wrap();
    test_static_io_works_with_full_pool();
    test_r1_chain_reject_accept_with_transition();
    test_r1_chain_busy_idle_wrap();
    test_r2_chain_inflight_with_fresh_traffic();
    return 0;
}
