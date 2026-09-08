#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "glsd301p_uart_transport.h"

static void expect_frame(const uint8_t actual[GLSD301P_CONTROL_FRAME_SIZE],
                         const uint8_t expected[GLSD301P_CONTROL_FRAME_SIZE])
{
    assert(memcmp(actual, expected, GLSD301P_CONTROL_FRAME_SIZE) == 0);
}

static void test_latest_normal_coalesces_while_busy(void)
{
    glsd301p_uart_transport_t transport;
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE];
    bool is_off = true;
    const uint8_t first[] = {0xA5, 0x5A, 0x01, 0x40, 0x04, 0xAA};
    const uint8_t latest[] = {0xA5, 0x5A, 0x01, 0x64, 0x04, 0xAA};

    glsd301p_uart_transport_init(&transport);
    assert(glsd301p_uart_transport_offer(&transport, first));
    assert(glsd301p_uart_transport_offer(&transport, latest));
    assert(glsd301p_uart_transport_peek(&transport, out, &is_off));
    assert(!is_off);
    expect_frame(out, latest);
    glsd301p_uart_transport_commit_sent(&transport, false);
    assert(!glsd301p_uart_transport_has_pending(&transport));
}

static void test_off_preempts_busy_pb4_burst(void)
{
    glsd301p_uart_transport_t transport;
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE];
    bool is_off = false;
    const uint8_t pb4_half[] = {0xA5, 0x5A, 0x01, 0x64, 0x04, 0xAA};
    const uint8_t off[] = {0xA5, 0x5A, 0x01, 0x00, 0x04, 0xAA};
    const uint8_t later_on[] = {0xA5, 0x5A, 0x01, 0x50, 0x04, 0xAA};

    glsd301p_uart_transport_init(&transport);
    for (unsigned i = 0; i < 100u; ++i) {
        assert(glsd301p_uart_transport_offer(&transport, pb4_half));
    }
    assert(glsd301p_uart_transport_offer(&transport, off));
    for (unsigned i = 0; i < 100u; ++i) {
        assert(glsd301p_uart_transport_offer(&transport, later_on));
    }
    assert(glsd301p_uart_transport_peek(&transport, out, &is_off));
    assert(is_off);
    expect_frame(out, off);

    glsd301p_uart_transport_commit_sent(&transport, true);
    assert(glsd301p_uart_transport_peek(&transport, out, &is_off));
    assert(!is_off);
    expect_frame(out, later_on);
}

static void test_off_drops_older_unsent_normal(void)
{
    glsd301p_uart_transport_t transport;
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE];
    bool is_off = false;
    const uint8_t stale_on[] = {0xA5, 0x5A, 0x01, 0xFE, 0x04, 0xAA};
    const uint8_t off[] = {0xA5, 0x5A, 0x01, 0x00, 0x04, 0xAA};

    glsd301p_uart_transport_init(&transport);
    assert(glsd301p_uart_transport_offer(&transport, stale_on));
    assert(glsd301p_uart_transport_offer(&transport, off));
    assert(glsd301p_uart_transport_peek(&transport, out, &is_off));
    assert(is_off);
    expect_frame(out, off);
    glsd301p_uart_transport_commit_sent(&transport, true);
    assert(!glsd301p_uart_transport_has_pending(&transport));
}

int main(void)
{
    test_latest_normal_coalesces_while_busy();
    test_off_preempts_busy_pb4_burst();
    test_off_drops_older_unsent_normal();
    return 0;
}