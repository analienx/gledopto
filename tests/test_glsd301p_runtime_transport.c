#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "glsd301p_runtime_core.h"
#include "glsd301p_uart_transport.h"

static void offer_result(glsd301p_uart_transport_t *transport,
                         glsd301p_runtime_result_t result,
                         uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE])
{
    if (result == GLSD301P_RUNTIME_FRAME_READY ||
        result == GLSD301P_RUNTIME_FORCED_OFF) {
        assert(glsd301p_uart_transport_offer(transport, frame));
    }
}

static bool sample_busy_tick(glsd301p_runtime_core_t *core,
                             glsd301p_uart_transport_t *transport,
                             bool pc2_high)
{
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];
    bool took_control = false;
    glsd301p_runtime_result_t result;

    result = glsd301p_runtime_core_poll_push_ex(core, pc2_high,
                                                &took_control, frame);
    offer_result(transport, result, frame);

    /* PB4 remains asserted and therefore produces a frame every 1 ms after
     * qualification. The test intentionally never commits a UART frame,
     * modelling a transport that stays busy throughout PC2 decoding. */
    result = glsd301p_runtime_core_poll_pb4(core, true, frame);
    offer_result(transport, result, frame);
    return took_control;
}

static void test_pc2_off_survives_pb4_burst_with_uart_busy(void)
{
    glsd301p_runtime_core_t core;
    glsd301p_uart_transport_t transport;
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE] = {0};
    bool took_control = false;
    bool is_off = false;
    const uint8_t off[] = {0xA5, 0x5A, 0x01, 0x00, 0x04, 0xAA};

    glsd301p_runtime_core_init(&core);
    glsd301p_uart_transport_init(&transport);
    assert(glsd301p_runtime_core_restore_state(&core, true, 200u, 2u, false,
                                                frame) ==
           GLSD301P_RUNTIME_FRAME_READY);

    for (unsigned i = 0; i < 3u; ++i) {
        assert(!sample_busy_tick(&core, &transport, false));
    }
    for (unsigned i = 0; i < 6u; ++i) {
        assert(!sample_busy_tick(&core, &transport, true));
    }
    for (unsigned i = 0; i < 3u; ++i) {
        assert(!sample_busy_tick(&core, &transport, false));
    }
    for (unsigned i = 0; i < 51u; ++i) {
        took_control = sample_busy_tick(&core, &transport, true);
    }

    assert(took_control);
    assert(!core.logical_output_enabled);
    assert(glsd301p_uart_transport_peek(&transport, frame, &is_off));
    assert(is_off);
    assert(memcmp(frame, off, sizeof(off)) == 0);
}

int main(void)
{
    test_pc2_off_survives_pb4_burst_with_uart_busy();
    return 0;
}