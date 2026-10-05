#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "glsd301p_runtime_core.h"

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

static glsd301p_runtime_result_t feed_push(glsd301p_runtime_core_t *core,
                                           bool high,
                                           unsigned ticks,
                                           uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    glsd301p_runtime_result_t result = GLSD301P_RUNTIME_NO_FRAME;

    for (unsigned i = 0; i < ticks; ++i) {
        glsd301p_runtime_result_t current =
            glsd301p_runtime_core_poll_push(core, high, out);
        if (current != GLSD301P_RUNTIME_NO_FRAME) {
            assert(result == GLSD301P_RUNTIME_NO_FRAME);
            result = current;
        }
    }

    return result;
}

static void qualify_push(glsd301p_runtime_core_t *core,
                         uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    assert(feed_push(core, false, 3, out) == GLSD301P_RUNTIME_NO_FRAME);
    assert(feed_push(core, true, 6, out) == GLSD301P_RUNTIME_NO_FRAME);
    assert(feed_push(core, false, 3, out) == GLSD301P_RUNTIME_NO_FRAME);
}

static void test_boot_and_restore_gate(void)
{
    glsd301p_runtime_core_t core;
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE] = {0};

    glsd301p_runtime_core_init(&core);
    assert(!glsd301p_runtime_core_is_ready(&core));

    assert(glsd301p_runtime_core_boot_off(&core, out) == GLSD301P_RUNTIME_FRAME_READY);
    expect_off(out);

    /* Direct application requests cannot bypass state restoration. */
    assert(glsd301p_runtime_core_apply_state(&core, true, 0x80, 0x02, false, out) ==
           GLSD301P_RUNTIME_FORCED_OFF);
    expect_off(out);

    /* A qualified physical PUSH before restore also cannot energize output. */
    qualify_push(&core, out);
    assert(feed_push(&core, true, 51, out) == GLSD301P_RUNTIME_FORCED_OFF);
    expect_off(out);
    assert(!core.logical_output_enabled);

    /* Invalid persisted state stays locked and OFF. */
    assert(glsd301p_runtime_core_restore_state(&core, true, 0xFF, 0x02, false, out) ==
           GLSD301P_RUNTIME_FORCED_OFF);
    expect_off(out);
    assert(!glsd301p_runtime_core_is_ready(&core));

    /* Valid state explicitly arms the runtime. */
    assert(glsd301p_runtime_core_restore_state(&core, true, 0x80, 0x02, false, out) ==
           GLSD301P_RUNTIME_FRAME_READY);
    {
        const uint8_t expected[] = {0xA5, 0x5A, 0x01, 0x80, 0x04, 0xAA};
        expect_frame(out, expected);
    }
    assert(glsd301p_runtime_core_is_ready(&core));
    assert(core.logical_output_enabled);
}

static void test_normal_and_pb4_paths_share_guard(void)
{
    glsd301p_runtime_core_t core;
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE] = {0};

    glsd301p_runtime_core_init(&core);
    assert(glsd301p_runtime_core_restore_state(&core, true, 0xC8, 0x02, false, out) ==
           GLSD301P_RUNTIME_FRAME_READY);

    /* PB4 remains the solved half-scale compatibility path while ON. */
    for (unsigned i = 0; i < 10; ++i) {
        assert(glsd301p_runtime_core_poll_pb4(&core, true, out) ==
               GLSD301P_RUNTIME_NO_FRAME);
    }
    assert(glsd301p_runtime_core_poll_pb4(&core, true, out) ==
           GLSD301P_RUNTIME_FRAME_READY);
    {
        const uint8_t expected_half[] = {0xA5, 0x5A, 0x01, 0x64, 0x04, 0xAA};
        expect_frame(out, expected_half);
    }
    assert(core.current_level == 0xC8);

    /* Logical OFF is committed through the same guard. */
    assert(glsd301p_runtime_core_apply_state(&core, false, 0xC8, 0x02, false, out) ==
           GLSD301P_RUNTIME_FRAME_READY);
    expect_off(out);
    assert(!core.logical_output_enabled);

    /* Re-qualify PB4 from low; it must remain electrical OFF. */
    assert(glsd301p_runtime_core_poll_pb4(&core, false, out) ==
           GLSD301P_RUNTIME_NO_FRAME);
    for (unsigned i = 0; i < 10; ++i) {
        assert(glsd301p_runtime_core_poll_pb4(&core, true, out) ==
               GLSD301P_RUNTIME_NO_FRAME);
    }
    assert(glsd301p_runtime_core_poll_pb4(&core, true, out) ==
           GLSD301P_RUNTIME_FRAME_READY);
    expect_off(out);
    assert(!core.logical_output_enabled);
}

static void test_push_updates_guarded_logical_state(void)
{
    glsd301p_runtime_core_t core;
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE] = {0};

    glsd301p_runtime_core_init(&core);
    assert(glsd301p_runtime_core_restore_state(&core, false, 100u, 2u, false, out) ==
           GLSD301P_RUNTIME_FRAME_READY);
    expect_off(out);

    /* Short PUSH toggles OFF -> ON using the remembered valid level. */
    qualify_push(&core, out);
    assert(feed_push(&core, true, 51, out) == GLSD301P_RUNTIME_FRAME_READY);
    {
        const uint8_t expected_on[] = {0xA5, 0x5A, 0x01, 100u, 0x04, 0xAA};
        expect_frame(out, expected_on);
    }
    assert(core.logical_output_enabled);

    /* Fresh long activation: first stock step is DOWN, 100 -> 90. */
    qualify_push(&core, out);
    glsd301p_runtime_result_t result = GLSD301P_RUNTIME_NO_FRAME;
    unsigned low_samples = 0;
    while (result == GLSD301P_RUNTIME_NO_FRAME) {
        for (unsigned i = 0; i < 10 && result == GLSD301P_RUNTIME_NO_FRAME; ++i) {
            result = glsd301p_runtime_core_poll_push(&core, false, out);
            ++low_samples;
        }
        if (result == GLSD301P_RUNTIME_NO_FRAME) {
            assert(feed_push(&core, true, 10, out) == GLSD301P_RUNTIME_NO_FRAME);
        }
    }
    assert(low_samples == 1001u);
    assert(result == GLSD301P_RUNTIME_FRAME_READY);
    {
        const uint8_t expected_step[] = {0xA5, 0x5A, 0x01, 90u, 0x04, 0xAA};
        expect_frame(out, expected_step);
    }
    assert(core.current_level == 90u);
}


static void test_push_takeover_metadata_covers_off_and_local_dimming(void)
{
    glsd301p_runtime_core_t core;
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE] = {0};
    bool took_control = false;
    glsd301p_runtime_result_t result = GLSD301P_RUNTIME_NO_FRAME;

    glsd301p_runtime_core_init(&core);
    assert(glsd301p_runtime_core_restore_state(&core, true, 100u, 2u, false, out) ==
           GLSD301P_RUNTIME_FRAME_READY);

    /* A decoded short PUSH takes ownership and toggles ON -> OFF. */
    for (unsigned i = 0; i < 3u; ++i) {
        assert(glsd301p_runtime_core_poll_push_ex(&core, false, &took_control, out) ==
               GLSD301P_RUNTIME_NO_FRAME);
        assert(!took_control);
    }
    for (unsigned i = 0; i < 6u; ++i) {
        assert(glsd301p_runtime_core_poll_push_ex(&core, true, &took_control, out) ==
               GLSD301P_RUNTIME_NO_FRAME);
        assert(!took_control);
    }
    for (unsigned i = 0; i < 3u; ++i) {
        assert(glsd301p_runtime_core_poll_push_ex(&core, false, &took_control, out) ==
               GLSD301P_RUNTIME_NO_FRAME);
        assert(!took_control);
    }
    for (unsigned i = 0; i < 51u; ++i) {
        result = glsd301p_runtime_core_poll_push_ex(&core, true, &took_control, out);
    }
    assert(result == GLSD301P_RUNTIME_FRAME_READY);
    assert(took_control);
    expect_off(out);
    assert(!core.logical_output_enabled);

    /* A later local level semantic action still takes ownership while OFF even
     * though stock behavior intentionally emits no level frame. This is the
     * signal the target needs to cancel a stale remote target/move transition. */
    took_control = false;
    for (unsigned i = 0; i < 3u; ++i) {
        assert(glsd301p_runtime_core_poll_push_ex(&core, false, &took_control, out) ==
               GLSD301P_RUNTIME_NO_FRAME);
    }
    for (unsigned i = 0; i < 6u; ++i) {
        assert(glsd301p_runtime_core_poll_push_ex(&core, true, &took_control, out) ==
               GLSD301P_RUNTIME_NO_FRAME);
    }
    for (unsigned i = 0; i < 3u; ++i) {
        assert(glsd301p_runtime_core_poll_push_ex(&core, false, &took_control, out) ==
               GLSD301P_RUNTIME_NO_FRAME);
    }

    result = GLSD301P_RUNTIME_NO_FRAME;
    unsigned low_samples = 0u;
    while (!took_control) {
        for (unsigned i = 0; i < 10u && !took_control; ++i) {
            result = glsd301p_runtime_core_poll_push_ex(&core, false, &took_control, out);
            ++low_samples;
        }
        if (!took_control) {
            for (unsigned i = 0; i < 10u; ++i) {
                assert(glsd301p_runtime_core_poll_push_ex(&core, true, &took_control, out) ==
                       GLSD301P_RUNTIME_NO_FRAME);
                assert(!took_control);
            }
        }
    }
    assert(low_samples == 1001u);
    assert(took_control);
    assert(result == GLSD301P_RUNTIME_NO_FRAME);
    assert(!core.logical_output_enabled);
    assert(core.current_level == 100u);
}

static void test_fault_invalidates_state_and_requires_restore(void)
{
    glsd301p_runtime_core_t core;
    uint8_t out[GLSD301P_CONTROL_FRAME_SIZE] = {0};

    glsd301p_runtime_core_init(&core);
    assert(glsd301p_runtime_core_restore_state(&core, true, 0x80, 0x02, false, out) ==
           GLSD301P_RUNTIME_FRAME_READY);

    assert(glsd301p_runtime_core_latch_fault(&core, out) ==
           GLSD301P_RUNTIME_FORCED_OFF);
    expect_off(out);
    assert(!glsd301p_runtime_core_is_ready(&core));
    assert(!core.state_restored);
    assert(!core.logical_output_enabled);

    assert(glsd301p_runtime_core_apply_state(&core, true, 0x80, 0x02, false, out) ==
           GLSD301P_RUNTIME_FORCED_OFF);
    expect_off(out);

    glsd301p_runtime_core_clear_fault_for_reinit(&core);
    assert(!glsd301p_runtime_core_is_ready(&core));

    /* Clearing the latch alone is intentionally insufficient. */
    assert(glsd301p_runtime_core_apply_state(&core, true, 0x80, 0x02, false, out) ==
           GLSD301P_RUNTIME_FORCED_OFF);
    expect_off(out);

    assert(glsd301p_runtime_core_restore_state(&core, true, 0x80, 0x02, false, out) ==
           GLSD301P_RUNTIME_FRAME_READY);
    assert(glsd301p_runtime_core_is_ready(&core));
}

int main(void)
{
    test_boot_and_restore_gate();
    test_normal_and_pb4_paths_share_guard();
    test_push_updates_guarded_logical_state();
    test_push_takeover_metadata_covers_off_and_local_dimming();
    test_fault_invalidates_state_and_requires_restore();

    return 0;
}
