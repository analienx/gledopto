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
    const uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE],
    uint32_t now_ms)
{
    if (transport == NULL || frame == NULL) {
        return false;
    }

    if (glsd301p_uart_transport_is_off(frame)) {
        if (!transport->off_pending) {
            transport->off_queued_ms = now_ms;
        }
        transport->off_pending = true;
        transport->normal_pending = false;
        return true;
    }

    if (!transport->normal_pending) {
        transport->normal_queued_ms = now_ms;
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

bool glsd301p_uart_transport_oldest_ms(
    const glsd301p_uart_transport_t *transport,
    uint32_t *out_ms)
{
    bool have_off;
    bool have_normal;

    if (transport == NULL || out_ms == NULL) {
        return false;
    }
    have_off = transport->off_pending;
    have_normal = transport->normal_pending;
    if (!have_off && !have_normal) {
        return false;
    }
    if (have_off && have_normal) {
        /*
         * Oldest first. Both stamps are live queue entries, so they are far
         * less than 2^31 apart: off_queued predates normal_queued exactly
         * when (off_queued - normal_queued) wraps above 2^31 - 1.
         */
        uint32_t off_minus_normal =
            transport->off_queued_ms - transport->normal_queued_ms;
        *out_ms = (off_minus_normal > 0x7FFFFFFFu)
                      ? transport->off_queued_ms
                      : transport->normal_queued_ms;
        return true;
    }
    *out_ms = have_off ? transport->off_queued_ms : transport->normal_queued_ms;
    return true;
}