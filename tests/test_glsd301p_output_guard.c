#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "glsd301p_output_guard.h"

static void expect_frame(const uint8_t actual[GLSD301P_CONTROL_FRAME_SIZE],
                         const uint8_t expected[GLSD301P_CONTROL_FRAME_SIZE])
{
    assert(memcmp(actual, expected, GLSD301P_CONTROL_FRAME_SIZE) == 0);
}

static void expect_off(const uint8_t actual[GLSD301P_CONTROL_FRAME_SIZE])
{
    const uint8_t expected[] = {0xA5, 0x5A, 0x01, 0x00, 0x04, 0xAA};
    expect_frame(actual, expected);
}

int main(void)
{
    glsd301p_output_guard_t guard;
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE] = {0};

    glsd301p_output_guard_init(&guard);
    assert(!glsd301p_output_guard_is_ready(&guard));

    /* Boot-locked state cannot energize the load. */
    assert(glsd301p_output_guard_encode_normal(&guard, true, 0x80, 0x02, false, out) ==
           GLSD301P_OUTPUT_GUARD_FORCED_OFF_NOT_READY);
    expect_off(out);

    /* OFF remains available even with unknown/corrupt state. */
    assert(glsd301p_output_guard_encode_normal(&guard, false, 0xFF, 0xFF, false, out) ==
           GLSD301P_OUTPUT_GUARD_OK);
    expect_off(out);

    /* Unknown ZCL currentLevel or active unknown minimum blocks arming. */
    assert(!glsd301p_output_guard_arm(&guard, 0xFF, 0x02, false));
    assert(!glsd301p_output_guard_arm(&guard, 0x80, 0xFF, false));
    assert(!glsd301p_output_guard_is_ready(&guard));

    /* If minimum clamping is explicitly bypassed, the minimum value is unused. */
    assert(glsd301p_output_guard_arm(&guard, 0x01, 0xFF, true));
    assert(glsd301p_output_guard_is_ready(&guard));
    {
        const uint8_t expected[] = {0xA5, 0x5A, 0x01, 0x01, 0x04, 0xAA};
        assert(glsd301p_output_guard_encode_normal(&guard, true, 0x01, 0xFF, true, out) ==
               GLSD301P_OUTPUT_GUARD_OK);
        expect_frame(out, expected);
    }

    /* A runtime unknown level fails closed even after successful arming. */
    assert(glsd301p_output_guard_encode_normal(&guard, true, 0xFF, 0x02, false, out) ==
           GLSD301P_OUTPUT_GUARD_FORCED_OFF_INVALID_LEVEL);
    expect_off(out);

    /* Re-arm with ordinary valid state and verify normal output. */
    glsd301p_output_guard_clear_fault_for_reinit(&guard);
    assert(!glsd301p_output_guard_is_ready(&guard));
    assert(glsd301p_output_guard_arm(&guard, 0xC8, 0x02, false));
    {
        const uint8_t expected[] = {0xA5, 0x5A, 0x01, 0xC8, 0x04, 0xAA};
        assert(glsd301p_output_guard_encode_normal(&guard, true, 0xC8, 0x02, false, out) ==
               GLSD301P_OUTPUT_GUARD_OK);
        expect_frame(out, expected);
    }

    /* PB4 may derate while ON, but must never energize from logical OFF. */
    {
        const uint8_t expected_half[] = {0xA5, 0x5A, 0x01, 0x64, 0x04, 0xAA};
        assert(glsd301p_output_guard_encode_pb4(&guard, true, 0xC8, out) ==
               GLSD301P_OUTPUT_GUARD_OK);
        expect_frame(out, expected_half);

        assert(glsd301p_output_guard_encode_pb4(&guard, false, 0xC8, out) ==
               GLSD301P_OUTPUT_GUARD_OK);
        expect_off(out);
    }

    /* Fault is latched and forces all energizing requests OFF. */
    glsd301p_output_guard_latch_fault(&guard);
    assert(!glsd301p_output_guard_is_ready(&guard));
    assert(glsd301p_output_guard_encode_normal(&guard, true, 0x80, 0x02, false, out) ==
           GLSD301P_OUTPUT_GUARD_FORCED_OFF_FAULT_LATCHED);
    expect_off(out);
    assert(glsd301p_output_guard_encode_pb4(&guard, true, 0x80, out) ==
           GLSD301P_OUTPUT_GUARD_FORCED_OFF_FAULT_LATCHED);
    expect_off(out);

    /* Clearing a fault never resumes output by itself. */
    glsd301p_output_guard_clear_fault_for_reinit(&guard);
    assert(!glsd301p_output_guard_is_ready(&guard));
    assert(glsd301p_output_guard_encode_normal(&guard, true, 0x80, 0x02, false, out) ==
           GLSD301P_OUTPUT_GUARD_FORCED_OFF_NOT_READY);
    expect_off(out);
    assert(glsd301p_output_guard_arm(&guard, 0x80, 0x02, false));
    assert(glsd301p_output_guard_is_ready(&guard));

    assert(glsd301p_output_guard_encode_normal(NULL, true, 0x80, 0x02, false, out) ==
           GLSD301P_OUTPUT_GUARD_INVALID_ARGUMENT);
    assert(glsd301p_output_guard_encode_normal(&guard, true, 0x80, 0x02, false, NULL) ==
           GLSD301P_OUTPUT_GUARD_INVALID_ARGUMENT);

    return 0;
}
