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
    assert(glsd301p_uart_transport_offer(&transport, first, 0u));
    assert(glsd301p_uart_transport_offer(&transport, latest, 1u));
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
        assert(glsd301p_uart_transport_offer(&transport, pb4_half, 2u));
    }
    assert(glsd301p_uart_transport_offer(&transport, off, 3u));
    for (unsigned i = 0; i < 100u; ++i) {
        assert(glsd301p_uart_transport_offer(&transport, later_on, 4u));
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
    assert(glsd301p_uart_transport_offer(&transport, stale_on, 5u));
    assert(glsd301p_uart_transport_offer(&transport, off, 3u));
    assert(glsd301p_uart_transport_peek(&transport, out, &is_off));
    assert(is_off);
    expect_frame(out, off);
    glsd301p_uart_transport_commit_sent(&transport, true);
    assert(!glsd301p_uart_transport_has_pending(&transport));
}

static void test_coalescing_preserves_oldest_queued_age(void)
{
    glsd301p_uart_transport_t transport;
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE];
    bool is_off = false;
    uint32_t oldest_ms = 0u;
    const uint8_t first[] = {0xA5, 0x5A, 0x01, 0x40, 0x04, 0xAA};
    const uint8_t latest[] = {0xA5, 0x5A, 0x01, 0x64, 0x04, 0xAA};
    const uint8_t off[] = {0xA5, 0x5A, 0x01, 0x00, 0x04, 0xAA};

    glsd301p_uart_transport_init(&transport);
    assert(!glsd301p_uart_transport_oldest_ms(&transport, &oldest_ms));
    assert(!glsd301p_uart_transport_oldest_ms(&transport, NULL));
    assert(!glsd301p_uart_transport_oldest_ms(NULL, &oldest_ms));

    assert(glsd301p_uart_transport_offer(&transport, first, 100u));
    assert(glsd301p_uart_transport_oldest_ms(&transport, &oldest_ms));
    assert(oldest_ms == 100u);

    /* Coalesced offer refreshes the frame but keeps the original stamp. */
    assert(glsd301p_uart_transport_offer(&transport, latest, 150u));
    assert(glsd301p_uart_transport_peek(&transport, out, &is_off));
    assert(!is_off);
    expect_frame(out, latest);
    assert(glsd301p_uart_transport_oldest_ms(&transport, &oldest_ms));
    assert(oldest_ms == 100u);

    /* OFF preempts the normal slot; its own stamp governs afterwards. */
    assert(glsd301p_uart_transport_offer(&transport, off, 160u));
    assert(glsd301p_uart_transport_oldest_ms(&transport, &oldest_ms));
    assert(oldest_ms == 160u);
    assert(glsd301p_uart_transport_offer(&transport, off, 170u));
    assert(glsd301p_uart_transport_oldest_ms(&transport, &oldest_ms));
    assert(oldest_ms == 160u);

    /* A later normal offer waits behind OFF; oldest stays the OFF stamp. */
    assert(glsd301p_uart_transport_offer(&transport, latest, 180u));
    assert(glsd301p_uart_transport_oldest_ms(&transport, &oldest_ms));
    assert(oldest_ms == 160u);

    /* After the OFF send, the surviving normal stamp is the oldest. */
    glsd301p_uart_transport_commit_sent(&transport, true);
    assert(glsd301p_uart_transport_oldest_ms(&transport, &oldest_ms));
    assert(oldest_ms == 180u);
    glsd301p_uart_transport_commit_sent(&transport, false);
    assert(!glsd301p_uart_transport_has_pending(&transport));
}

static void test_oldest_ms_survives_timebase_wrap(void)
{
    glsd301p_uart_transport_t transport;
    uint32_t oldest_ms = 0u;
    const uint8_t normal[] = {0xA5, 0x5A, 0x01, 0x40, 0x04, 0xAA};
    const uint8_t off[] = {0xA5, 0x5A, 0x01, 0x00, 0x04, 0xAA};

    glsd301p_uart_transport_init(&transport);
    assert(glsd301p_uart_transport_offer(&transport, off, 0xFFFFFFF0u));
    /* Normal waits behind OFF: both slots pending across the wrap, and the
     * pre-wrap OFF stamp is oldest. */
    assert(glsd301p_uart_transport_offer(&transport, normal, 0x00000005u));
    assert(glsd301p_uart_transport_oldest_ms(&transport, &oldest_ms));
    assert(oldest_ms == 0xFFFFFFF0u);
}

int main(void)
{
    test_latest_normal_coalesces_while_busy();
    test_off_preempts_busy_pb4_burst();
    test_off_drops_older_unsent_normal();
    test_coalescing_preserves_oldest_queued_age();
    test_oldest_ms_survives_timebase_wrap();
    return 0;
}