#include "glsd301p_push_input.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static glsd301p_push_event_t feed(glsd301p_push_decoder_t *decoder,
                                  bool high,
                                  unsigned ticks)
{
    glsd301p_push_event_t event = GLSD301P_PUSH_EVENT_NONE;
    for (unsigned i = 0; i < ticks; ++i) {
        glsd301p_push_event_t current = glsd301p_push_decoder_poll(decoder, high);
        if (current != GLSD301P_PUSH_EVENT_NONE) {
            assert(event == GLSD301P_PUSH_EVENT_NONE);
            event = current;
        }
    }
    return event;
}

static void qualify_active_waveform(glsd301p_push_decoder_t *decoder)
{
    assert(feed(decoder, false, 3) == GLSD301P_PUSH_EVENT_NONE);
    assert(feed(decoder, true, 6) == GLSD301P_PUSH_EVENT_NONE);
    assert(feed(decoder, false, 3) == GLSD301P_PUSH_EVENT_NONE);
}

static void test_incomplete_pulse_is_ignored(void)
{
    glsd301p_push_decoder_t decoder;
    glsd301p_push_decoder_init(&decoder);

    assert(feed(&decoder, false, 3) == GLSD301P_PUSH_EVENT_NONE);
    assert(feed(&decoder, true, 51) == GLSD301P_PUSH_EVENT_NONE);
    assert(feed(&decoder, true, 100) == GLSD301P_PUSH_EVENT_NONE);
}

static void test_short_push_toggles(void)
{
    glsd301p_push_decoder_t decoder;
    glsd301p_push_decoder_init(&decoder);
    qualify_active_waveform(&decoder);

    assert(feed(&decoder, true, 50) == GLSD301P_PUSH_EVENT_NONE);
    assert(feed(&decoder, true, 1) == GLSD301P_PUSH_EVENT_TOGGLE);
    assert(glsd301p_push_decoder_direction(&decoder) == GLSD301P_PUSH_DIM_DOWN);
}

static void test_long_push_steps_and_reverses_direction_on_release(void)
{
    glsd301p_push_decoder_t decoder;
    glsd301p_push_decoder_init(&decoder);
    qualify_active_waveform(&decoder);

    glsd301p_push_event_t event = GLSD301P_PUSH_EVENT_NONE;
    unsigned low_samples = 0;

    /* Model an AC-derived presence waveform: bounded high gaps keep the
     * activation alive while only low samples contribute to the stock hold
     * accumulator. */
    while (event == GLSD301P_PUSH_EVENT_NONE) {
        for (unsigned i = 0; i < 10 && event == GLSD301P_PUSH_EVENT_NONE; ++i) {
            event = glsd301p_push_decoder_poll(&decoder, false);
            ++low_samples;
        }
        if (event == GLSD301P_PUSH_EVENT_NONE) {
            assert(feed(&decoder, true, 10) == GLSD301P_PUSH_EVENT_NONE);
        }
    }

    assert(event == GLSD301P_PUSH_EVENT_LEVEL_STEP);
    assert(low_samples == 1001u);
    assert(glsd301p_push_decoder_direction(&decoder) == GLSD301P_PUSH_DIM_DOWN);

    event = GLSD301P_PUSH_EVENT_NONE;
    low_samples = 0;
    while (event == GLSD301P_PUSH_EVENT_NONE) {
        for (unsigned i = 0; i < 10 && event == GLSD301P_PUSH_EVENT_NONE; ++i) {
            event = glsd301p_push_decoder_poll(&decoder, false);
            ++low_samples;
        }
        if (event == GLSD301P_PUSH_EVENT_NONE) {
            assert(feed(&decoder, true, 10) == GLSD301P_PUSH_EVENT_NONE);
        }
    }

    assert(event == GLSD301P_PUSH_EVENT_LEVEL_STEP);
    assert(low_samples == 301u);

    assert(feed(&decoder, true, 50) == GLSD301P_PUSH_EVENT_NONE);
    assert(feed(&decoder, true, 1) == GLSD301P_PUSH_EVENT_NONE);
    assert(glsd301p_push_decoder_direction(&decoder) == GLSD301P_PUSH_DIM_UP);
}

static void test_stock_local_level_step_policy(void)
{
    assert(glsd301p_push_level_step(100u, GLSD301P_PUSH_DIM_DOWN) == 90u);
    assert(glsd301p_push_level_step(151u, GLSD301P_PUSH_DIM_DOWN) == 126u);
    assert(glsd301p_push_level_step(10u, GLSD301P_PUSH_DIM_DOWN) == 2u);
    assert(glsd301p_push_level_step(11u, GLSD301P_PUSH_DIM_DOWN) == 1u);

    assert(glsd301p_push_level_step(100u, GLSD301P_PUSH_DIM_UP) == 110u);
    assert(glsd301p_push_level_step(151u, GLSD301P_PUSH_DIM_UP) == 176u);
    assert(glsd301p_push_level_step(240u, GLSD301P_PUSH_DIM_UP) == 254u);
}

int main(void)
{
    test_incomplete_pulse_is_ignored();
    test_short_push_toggles();
    test_long_push_steps_and_reverses_direction_on_release();
    test_stock_local_level_step_policy();

    puts("GLSD301P_PUSH_INPUT_TESTS=PASS");
    return 0;
}
