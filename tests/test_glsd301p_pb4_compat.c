#include "glsd301p_pb4_compat.h"
#include "glsd301p_uart_frame.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static void test_qualification_and_repeat(void)
{
    glsd301p_pb4_compat_t compat;
    glsd301p_pb4_compat_init(&compat);

    for (unsigned i = 0; i < 10u; ++i) {
        assert(!glsd301p_pb4_compat_poll(&compat, true));
    }

    assert(glsd301p_pb4_compat_poll(&compat, true)); /* 11th consecutive high */
    assert(glsd301p_pb4_compat_poll(&compat, true)); /* repeat while held high */
    assert(glsd301p_pb4_compat_poll(&compat, true));
}

static void test_low_resets_qualification(void)
{
    glsd301p_pb4_compat_t compat;
    glsd301p_pb4_compat_init(&compat);

    for (unsigned i = 0; i < 10u; ++i) {
        assert(!glsd301p_pb4_compat_poll(&compat, true));
    }
    assert(!glsd301p_pb4_compat_poll(&compat, false));

    for (unsigned i = 0; i < 10u; ++i) {
        assert(!glsd301p_pb4_compat_poll(&compat, true));
    }
    assert(glsd301p_pb4_compat_poll(&compat, true));
}

static void test_half_scale_mapping(void)
{
    assert(glsd301p_pb4_compat_level(0u) == 0u);
    assert(glsd301p_pb4_compat_level(1u) == 0u);
    assert(glsd301p_pb4_compat_level(2u) == 1u);
    assert(glsd301p_pb4_compat_level(128u) == 64u);
    assert(glsd301p_pb4_compat_level(254u) == 127u);
}

static void test_resulting_wire_vector(void)
{
    uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE];
    static const uint8_t expected[GLSD301P_CONTROL_FRAME_SIZE] = {
        0xA5u, 0x5Au, 0x01u, 0x64u, 0x04u, 0xAAu,
    };

    const uint8_t logical_level = 200u;
    const uint8_t physical_request = glsd301p_pb4_compat_level(logical_level);

    assert(physical_request == 100u);
    assert(glsd301p_control_frame_encode(GLSD301P_CONTROL_FAMILY_LEVEL,
                                         physical_request,
                                         frame));
    assert(memcmp(frame, expected, sizeof(expected)) == 0);
    /* logical_level remains unchanged by the compatibility layer. */
    assert(logical_level == 200u);
}

static void test_null_decoder_fails_closed(void)
{
    assert(!glsd301p_pb4_compat_poll(NULL, true));
}

int main(void)
{
    test_qualification_and_repeat();
    test_low_resets_qualification();
    test_half_scale_mapping();
    test_resulting_wire_vector();
    test_null_decoder_fails_closed();
    return 0;
}
