#ifndef GLSD301P_UART_SERVICE_H
#define GLSD301P_UART_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "glsd301p_uart_frame.h"
#include "glsd301p_uart_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Elapsed-time UART service policy for the 9600 8N1 power-stage link.
 *
 * One service step performs bounded O(1) work: at most one busy probe and one
 * DMA start attempt. Deadlines measure actual elapsed milliseconds on the
 * shared timebase, never IO callback counts.
 *
 * A transfer attempt or completion reported here is NOT acknowledged physical
 * output state; the power stage never confirms receipt.
 */

#define GLSD301P_UART_DEADLINE_MS 32u

typedef struct {
    bool fault_latched;
    bool inflight_active;
    bool inflight_seen_busy;
    uint32_t inflight_since_ms;
    bool boot_pending;
    uint32_t boot_since_ms;
    bool boot_complete;
    bool boot_failed;
    uint32_t deadline_faults;
} glsd301p_uart_service_t;

typedef struct {
    bool fault_raised;
    bool frame_sent;
    bool boot_completed;
    bool boot_failed;
} glsd301p_uart_service_event_t;

void glsd301p_uart_service_init(glsd301p_uart_service_t *service);

/*
 * Record that the boot OFF frame was accepted by DMA. Completion is observed
 * by later service steps; the caller passes the acceptance timestamp.
 */
void glsd301p_uart_service_note_boot_sent(glsd301p_uart_service_t *service,
                                          uint32_t now_ms);

/*
 * Run one service step. On fault_raised the caller must latch the runtime
 * fault, cancel transitions, prioritize OFF and sync mirrors; the OFF frame
 * itself is queued by the caller through the runtime latch path.
 */
glsd301p_uart_service_event_t glsd301p_uart_service_step(
    glsd301p_uart_service_t *service,
    glsd301p_uart_transport_t *transport,
    uint32_t now_ms);

uint32_t glsd301p_uart_service_deadline_faults(
    const glsd301p_uart_service_t *service);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_UART_SERVICE_H */
