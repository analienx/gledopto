#ifndef GLSD301P_UART_TRANSPORT_H
#define GLSD301P_UART_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "glsd301p_uart_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool off_pending;
    bool normal_pending;
    uint8_t normal_frame[GLSD301P_CONTROL_FRAME_SIZE];
} glsd301p_uart_transport_t;

void glsd301p_uart_transport_init(glsd301p_uart_transport_t *transport);

/**
 * Offer one already-guarded frame without blocking. Confirmed electrical OFF
 * has strict priority: it drops older unsent non-OFF output, while a later
 * non-OFF request may wait behind it. Repeated non-OFF offers coalesce latest.
 */
bool glsd301p_uart_transport_offer(
    glsd301p_uart_transport_t *transport,
    const uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE]);

bool glsd301p_uart_transport_peek(
    const glsd301p_uart_transport_t *transport,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE],
    bool *is_off);

void glsd301p_uart_transport_commit_sent(
    glsd301p_uart_transport_t *transport,
    bool was_off);

bool glsd301p_uart_transport_has_pending(
    const glsd301p_uart_transport_t *transport);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_UART_TRANSPORT_H */