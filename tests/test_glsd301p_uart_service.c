#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "glsd301p_timebase.h"
#include "glsd301p_uart_service.h"
#include "glsd301p_uart_transport.h"

#include "hw_stub.h"

static const uint8_t NORMAL_ON[] = {0xA5, 0x5A, 0x01, 0xC8, 0x04, 0xAA};
static const uint8_t OFF[] = {0xA5, 0x5A, 0x01, 0x00, 0x04, 0xAA};

static void setup(glsd301p_uart_service_t *svc,
                  glsd301p_uart_transport_t *transport)
{
    host_stub_reset();
    host_clock_set(0u);
    glsd301p_timebase_init();
    glsd301p_uart_service_init(svc);
    glsd301p_uart_transport_init(transport);
}

static void test_idle_step_is_quiet(void)
{
    glsd301p_uart_service_t svc;
    glsd301p_uart_transport_t transport;
    glsd301p_uart_service_event_t ev;

    setup(&svc, &transport);
    ev = glsd301p_uart_service_step(&svc, &transport, 1000u);
    assert(!ev.fault_raised && !ev.frame_sent);
    assert(!ev.boot_completed && !ev.boot_failed);
    assert(host_uart_send_attempts() == 0u);
    assert(glsd301p_uart_service_deadline_faults(&svc) == 0u);
}

static void test_send_accepted_commits_and_goes_inflight(void)
{
    glsd301p_uart_service_t svc;
    glsd301p_uart_transport_t transport;
    glsd301p_uart_service_event_t ev;
    uint8_t last[6];

    setup(&svc, &transport);
    assert(glsd301p_uart_transport_offer(&transport, NORMAL_ON, 0u));

    ev = glsd301p_uart_service_step(&svc, &transport, 0u);
    assert(ev.frame_sent && !ev.fault_raised);
    assert(!glsd301p_uart_transport_has_pending(&transport));
    assert(host_uart_send_attempts() == 1u);
    assert(host_uart_last_frame(last));
    assert(memcmp(last, NORMAL_ON, sizeof(last)) == 0);

    /* Fresh transfer completes only after hardware was observed busy. */
    ev = glsd301p_uart_service_step(&svc, &transport, 1u);
    assert(!ev.fault_raised);
    host_uart_set_busy(true);
    ev = glsd301p_uart_service_step(&svc, &transport, 2u);
    host_uart_set_busy(false);
    ev = glsd301p_uart_service_step(&svc, &transport, 8u);
    assert(!ev.fault_raised);
    /* In-flight cleared: a later stuck busy starts a new episode, un faulted. */
    host_uart_set_busy(true);
    ev = glsd301p_uart_service_step(&svc, &transport, 9u);
    assert(!ev.fault_raised);
    assert(glsd301p_uart_service_deadline_faults(&svc) == 0u);
}

static void test_busy_defers_send_without_attempt(void)
{
    glsd301p_uart_service_t svc;
    glsd301p_uart_transport_t transport;
    glsd301p_uart_service_event_t ev;

    setup(&svc, &transport);
    assert(glsd301p_uart_transport_offer(&transport, NORMAL_ON, 0u));
    host_uart_set_busy(true);

    ev = glsd301p_uart_service_step(&svc, &transport, 10u);
    assert(!ev.frame_sent && !ev.fault_raised);
    assert(host_uart_send_attempts() == 0u);
    assert(glsd301p_uart_transport_has_pending(&transport));
}

static void test_dma_rejection_retries_with_bounded_work(void)
{
    glsd301p_uart_service_t svc;
    glsd301p_uart_transport_t transport;
    glsd301p_uart_service_event_t ev;

    setup(&svc, &transport);
    assert(glsd301p_uart_transport_offer(&transport, NORMAL_ON, 0u));
    host_uart_set_send_accepts(false);

    /* One attempt per step, no fault before the elapsed deadline. */
    for (uint32_t t = 0u; t < 31u; t++) {
        ev = glsd301p_uart_service_step(&svc, &transport, t);
        assert(!ev.frame_sent && !ev.fault_raised);
    }
    assert(host_uart_send_attempts() == 31u);
    assert(glsd301p_uart_transport_has_pending(&transport));

    ev = glsd301p_uart_service_step(&svc, &transport, 32u);
    assert(ev.fault_raised);
    assert(glsd301p_uart_service_deadline_faults(&svc) == 1u);
}

static void test_pending_deadline_uses_elapsed_time(void)
{
    glsd301p_uart_service_t svc;
    glsd301p_uart_transport_t transport;
    glsd301p_uart_service_event_t ev;

    setup(&svc, &transport);
    assert(glsd301p_uart_transport_offer(&transport, NORMAL_ON, 0u));
    host_uart_set_busy(true);

    /* Two service calls 31 ms apart: no fault (a callback counter would
     * agree here, so this alone proves nothing about the basis). */
    ev = glsd301p_uart_service_step(&svc, &transport, 0u);
    assert(!ev.fault_raised);
    ev = glsd301p_uart_service_step(&svc, &transport, 31u);
    assert(!ev.fault_raised);

    /* A single further step past the elapsed deadline faults even though
     * only three callbacks ever ran. */
    ev = glsd301p_uart_service_step(&svc, &transport, 32u);
    assert(ev.fault_raised);
    assert(glsd301p_uart_service_deadline_faults(&svc) == 1u);

    /* Latched faults never re-raise. */
    ev = glsd301p_uart_service_step(&svc, &transport, 100u);
    assert(!ev.fault_raised);
    assert(glsd301p_uart_service_deadline_faults(&svc) == 1u);
}

static void test_empty_queue_inflight_timeout(void)
{
    glsd301p_uart_service_t svc;
    glsd301p_uart_transport_t transport;
    glsd301p_uart_service_event_t ev;

    setup(&svc, &transport);
    assert(glsd301p_uart_transport_offer(&transport, NORMAL_ON, 0u));
    ev = glsd301p_uart_service_step(&svc, &transport, 0u);
    assert(ev.frame_sent);
    assert(!glsd301p_uart_transport_has_pending(&transport));

    /* Transfer never completes although the queue is empty. */
    host_uart_set_busy(true);
    ev = glsd301p_uart_service_step(&svc, &transport, 31u);
    assert(!ev.fault_raised);
    ev = glsd301p_uart_service_step(&svc, &transport, 32u);
    assert(ev.fault_raised);
    assert(glsd301p_uart_service_deadline_faults(&svc) == 1u);
}

static void test_boot_observe_complete_and_timeout(void)
{
    glsd301p_uart_service_t svc;
    glsd301p_uart_transport_t transport;
    glsd301p_uart_service_event_t ev;

    /* Completion path. */
    setup(&svc, &transport);
    glsd301p_uart_service_note_boot_sent(&svc, 0u);
    host_uart_set_busy(true);
    ev = glsd301p_uart_service_step(&svc, &transport, 1u);
    assert(!ev.boot_completed && !ev.fault_raised);
    host_uart_set_busy(false);
    ev = glsd301p_uart_service_step(&svc, &transport, 7u);
    assert(ev.boot_completed && !ev.fault_raised && !ev.boot_failed);

    /* Timeout path: stuck busy trips fault and boot failure together. */
    setup(&svc, &transport);
    glsd301p_uart_service_note_boot_sent(&svc, 100u);
    host_uart_set_busy(true);
    ev = glsd301p_uart_service_step(&svc, &transport, 131u);
    assert(!ev.boot_failed && !ev.fault_raised);
    ev = glsd301p_uart_service_step(&svc, &transport, 132u);
    assert(ev.boot_failed && ev.fault_raised);
    assert(glsd301p_uart_service_deadline_faults(&svc) == 1u);
}

static void test_off_frame_uses_same_service_path(void)
{
    glsd301p_uart_service_t svc;
    glsd301p_uart_transport_t transport;
    glsd301p_uart_service_event_t ev;
    uint8_t last[6];

    setup(&svc, &transport);
    assert(glsd301p_uart_transport_offer(&transport, OFF, 0u));
    ev = glsd301p_uart_service_step(&svc, &transport, 0u);
    assert(ev.frame_sent);
    assert(host_uart_last_frame(last));
    assert(memcmp(last, OFF, sizeof(last)) == 0);
}

int main(void)
{
    test_idle_step_is_quiet();
    test_send_accepted_commits_and_goes_inflight();
    test_busy_defers_send_without_attempt();
    test_dma_rejection_retries_with_bounded_work();
    test_pending_deadline_uses_elapsed_time();
    test_empty_queue_inflight_timeout();
    test_boot_observe_complete_and_timeout();
    test_off_frame_uses_same_service_path();
    return 0;
}
