#include "glsd301p_uart_transport.h"

#include <stddef.h>
#include <string.h>

static const uint8_t g_confirmed_off[GLSD301P_CONTROL_FRAME_SIZE] = {
    0xA5u, 0x5Au, 0x01u, 0x00u, 0x04u, 0xAAu
};

static bool glsd301p_uart_transport_is_off(
    const uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE])
{
    return frame != NULL &&
           memcmp(frame, g_confirmed_off, GLSD301P_CONTROL_FRAME_SIZE) == 0;
}

void glsd301p_uart_transport_init(glsd301p_uart_transport_t *transport)
{
    if (transport == NULL) {
        return;
    }
    memset(transport, 0, sizeof(*transport));
}

bool glsd301p_uart_transport_offer(
    glsd301p_uart_transport_t *transport,
    const uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE])
{
    if (transport == NULL || frame == NULL) {
        return false;
    }

    if (glsd301p_uart_transport_is_off(frame)) {
        transport->off_pending = true;
        transport->normal_pending = false;
        return true;
    }

    memcpy(transport->normal_frame, frame, GLSD301P_CONTROL_FRAME_SIZE);
    transport->normal_pending = true;
    return true;
}

bool glsd301p_uart_transport_peek(
    const glsd301p_uart_transport_t *transport,
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE],
    bool *is_off)
{
    if (transport == NULL || out == NULL || is_off == NULL) {
        return false;
    }

    if (transport->off_pending) {
        memcpy(out, g_confirmed_off, GLSD301P_CONTROL_FRAME_SIZE);
        *is_off = true;
        return true;
    }

    if (transport->normal_pending) {
        memcpy(out, transport->normal_frame, GLSD301P_CONTROL_FRAME_SIZE);
        *is_off = false;
        return true;
    }

    return false;
}

void glsd301p_uart_transport_commit_sent(
    glsd301p_uart_transport_t *transport,
    bool was_off)
{
    if (transport == NULL) {
        return;
    }

    if (was_off) {
        transport->off_pending = false;
    } else {
        transport->normal_pending = false;
    }
}

bool glsd301p_uart_transport_has_pending(
    const glsd301p_uart_transport_t *transport)
{
    return transport != NULL &&
           (transport->off_pending || transport->normal_pending);
}