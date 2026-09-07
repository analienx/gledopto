#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "glsd301p_power_stage_policy.h"
#include "glsd301p_uart_frame.h"

static void expect_frame(const uint8_t actual[GLSD301P_CONTROL_FRAME_SIZE],
                         const uint8_t expected[GLSD301P_CONTROL_FRAME_SIZE])
{
    assert(memcmp(actual, expected, GLSD301P_CONTROL_FRAME_SIZE) == 0);
}

int main(void)
{
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE] = {0};

    {
        const uint8_t expected[] = {0xA5, 0x5A, 0x01, 0x7F, 0x04, 0xAA};
        assert(glsd301p_control_frame_encode(GLSD301P_CONTROL_FAMILY_LEVEL, 0x7F, out));
        expect_frame(out, expected);
    }

    {
        const uint8_t expected[] = {0xA5, 0x5A, 0x02, 0x03, 0x04, 0xAA};
        assert(glsd301p_control_frame_encode(GLSD301P_CONTROL_FAMILY_OPERATION, 0x03, out));
        expect_frame(out, expected);
    }

    assert(!glsd301p_control_frame_encode(0x7E, 0x00, out));
    assert(!glsd301p_control_frame_encode(GLSD301P_CONTROL_FAMILY_LEVEL, 0x00, NULL));

    /* Confirmed normal stock OFF path. */
    {
        const uint8_t expected[] = {0xA5, 0x5A, 0x01, 0x00, 0x04, 0xAA};
        assert(glsd301p_normal_output_frame_encode(false, 0xFE, 0x02, false, out));
        expect_frame(out, expected);
    }

    /* Confirmed normal ON/Level path. */
    {
        const uint8_t expected[] = {0xA5, 0x5A, 0x01, 0x80, 0x04, 0xAA};
        assert(glsd301p_normal_output_frame_encode(true, 0x80, 0x02, false, out));
        expect_frame(out, expected);
    }

    /* Normal path clamps below configured minimum. */
    {
        const uint8_t expected[] = {0xA5, 0x5A, 0x01, 0x02, 0x04, 0xAA};
        assert(glsd301p_normal_output_frame_encode(true, 0x01, 0x02, false, out));
        expect_frame(out, expected);
    }

    /* The independently exposed stock bypass state preserves the low level. */
    {
        const uint8_t expected[] = {0xA5, 0x5A, 0x01, 0x01, 0x04, 0xAA};
        assert(glsd301p_normal_output_frame_encode(true, 0x01, 0x02, true, out));
        expect_frame(out, expected);
    }

    return 0;
}
