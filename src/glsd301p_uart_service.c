#include "glsd301p_uart_service.h"

#include <stddef.h>

#include "glsd301p_hw_io.h"
#include "glsd301p_timebase.h"

static glsd301p_uart_service_event_t glsd301p_uart_no_event(void)
{
    glsd301p_uart_service_event_t event;

    event.fault_raised = false;
    event.frame_sent = false;
    event.boot_completed = false;
    event.boot_failed = false;
    return event;
}

void glsd301p_uart_service_init(glsd301p_uart_service_t *service)
{
    if (service == NULL) {
        return;
    }

    service->fault_latched = false;
    service->inflight_active = false;
    service->inflight_seen_busy = false;
    service->inflight_since_ms = 0u;
    service->boot_pending = false;
    service->boot_since_ms = 0u;
    service->boot_complete = false;
    service->boot_failed = false;
    service->deadline_faults = 0u;
}

void glsd301p_uart_service_note_boot_sent(glsd301p_uart_service_t *service,
                                          uint32_t now_ms)
{
    if (service == NULL) {
        return;
    }

    service->boot_pending = true;
    service->boot_since_ms = now_ms;
    service->boot_complete = false;
    service->boot_failed = false;
}

glsd301p_uart_service_event_t glsd301p_uart_service_step(
    glsd301p_uart_service_t *service,
    glsd301p_uart_transport_t *transport,
    uint32_t now_ms)
{
    glsd301p_uart_service_event_t event = glsd301p_uart_no_event();
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];
    uint32_t oldest_ms;
    bool is_off;

    if (service == NULL || transport == NULL) {
        return event;
    }

    /* Boot OFF completion (or its 32 ms timeout) is observed first. */
    if (service->boot_pending) {
        if (!glsd301p_hw_uart_busy()) {
            service->boot_pending = false;
            service->boot_complete = true;
            event.boot_completed = true;
        } else if (glsd301p_timebase_age_ms(service->boot_since_ms, now_ms) >=
                   GLSD301P_UART_DEADLINE_MS) {
            service->boot_pending = false;
            service->boot_failed = true;
            if (!service->fault_latched) {
                service->fault_latched = true;
                service->deadline_faults =
                    glsd301p_sat_inc_u32(service->deadline_faults);
                event.fault_raised = true;
            }
            event.boot_failed = true;
        }
    }

    if (glsd301p_uart_transport_has_pending(transport)) {
        /* A stuck queue trips the deadline even while DMA stays busy. */
        if (glsd301p_uart_transport_oldest_ms(transport, &oldest_ms) &&
            !service->fault_latched &&
            glsd301p_timebase_age_ms(oldest_ms, now_ms) >=
                GLSD301P_UART_DEADLINE_MS) {
            service->fault_latched = true;
            service->deadline_faults =
                glsd301p_sat_inc_u32(service->deadline_faults);
            event.fault_raised = true;
        }

        /* The static DMA buffer must never be rewritten while owned. */
        if (glsd301p_hw_uart_busy()) {
            return event;
        }
        if (!glsd301p_uart_transport_peek(transport, frame, &is_off)) {
            return event;
        }

        /*
         * Nonblocking start: rejection is retried on a later step with
         * bounded work. Permanent rejection trips the deadline above.
         */
        if (glsd301p_hw_uart_send_frame(frame)) {
            glsd301p_uart_transport_commit_sent(transport, is_off);
            service->inflight_active = true;
            service->inflight_seen_busy = false;
            service->inflight_since_ms = now_ms;
            event.frame_sent = true;
        }
        return event;
    }

    /* Empty queue: still track the in-flight transfer to completion. */
    if (service->inflight_active) {
        if (!glsd301p_hw_uart_busy()) {
            /*
             * Completion counts only after hardware was observed busy;
             * otherwise a stale idle flag could retire a fresh transfer.
             */
            if (service->inflight_seen_busy) {
                service->inflight_active = false;
            }
        } else {
            service->inflight_seen_busy = true;
        }

        if (service->inflight_active && !service->fault_latched &&
            glsd301p_timebase_age_ms(service->inflight_since_ms, now_ms) >=
                GLSD301P_UART_DEADLINE_MS) {
            service->fault_latched = true;
            service->deadline_faults =
                glsd301p_sat_inc_u32(service->deadline_faults);
            event.fault_raised = true;
        }
    }

    return event;
}

uint32_t glsd301p_uart_service_deadline_faults(
    const glsd301p_uart_service_t *service)
{
    return service == NULL ? 0u : service->deadline_faults;
}
