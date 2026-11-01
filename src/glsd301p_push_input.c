#include "glsd301p_push_input.h"

#include <limits.h>
#include <stddef.h>

/* Semantic states for the observed PC2 pulse-presence decoder. */
enum {
    PUSH_IDLE = 0,
    PUSH_FIRST_LOW = 1,
    PUSH_INTERPULSE_GAP = 2,
    PUSH_ACTIVE = 4,
    PUSH_LONG_ACTIVE = 5,
};

enum {
    LOW_QUALIFY_TICKS = 3,
    GAP_QUALIFY_TICKS = 6,
    GAP_TIMEOUT_TICKS = 51,
    LONG_FIRST_LOW_TICKS = 1001,
    LONG_REPEAT_LOW_TICKS = 301,
};

static uint8_t sat_inc_u8(uint8_t value)
{
    return value == UINT8_MAX ? value : (uint8_t)(value + 1u);
}

static uint16_t sat_inc_u16(uint16_t value)
{
    return value == UINT16_MAX ? value : (uint16_t)(value + 1u);
}

static void reset_activity(glsd301p_push_decoder_t *decoder)
{
    decoder->state = PUSH_IDLE;
    decoder->low_run_ticks = 0u;
    decoder->high_run_ticks = 0u;
    decoder->active_low_ticks = 0u;
}

void glsd301p_push_decoder_init(glsd301p_push_decoder_t *decoder)
{
    if (decoder == NULL) {
        return;
    }

    reset_activity(decoder);
    /* The stock state is zero-initialized, so the first long activation steps
     * downward. The direction reverses when a long activation is released. */
    decoder->direction = GLSD301P_PUSH_DIM_DOWN;
}

glsd301p_push_event_t glsd301p_push_decoder_poll(glsd301p_push_decoder_t *decoder,
                                                  bool pc2_high)
{
    if (decoder == NULL) {
        return GLSD301P_PUSH_EVENT_NONE;
    }

    if (pc2_high) {
        decoder->low_run_ticks = 0u;
        decoder->high_run_ticks = sat_inc_u8(decoder->high_run_ticks);

        switch (decoder->state) {
        case PUSH_IDLE:
            decoder->active_low_ticks = 0u;
            break;

        case PUSH_FIRST_LOW:
            if (decoder->high_run_ticks >= GAP_TIMEOUT_TICKS) {
                reset_activity(decoder);
            } else if (decoder->high_run_ticks >= GAP_QUALIFY_TICKS) {
                decoder->state = PUSH_INTERPULSE_GAP;
                decoder->active_low_ticks = 0u;
            }
            break;

        case PUSH_INTERPULSE_GAP:
            if (decoder->high_run_ticks >= GAP_TIMEOUT_TICKS) {
                reset_activity(decoder);
            }
            break;

        case PUSH_ACTIVE:
            if (decoder->high_run_ticks >= GAP_TIMEOUT_TICKS) {
                reset_activity(decoder);
                return GLSD301P_PUSH_EVENT_TOGGLE;
            }
            break;

        case PUSH_LONG_ACTIVE:
            if (decoder->high_run_ticks >= GAP_TIMEOUT_TICKS) {
                decoder->direction =
                    decoder->direction == GLSD301P_PUSH_DIM_DOWN
                        ? GLSD301P_PUSH_DIM_UP
                        : GLSD301P_PUSH_DIM_DOWN;
                reset_activity(decoder);
            }
            break;

        default:
            reset_activity(decoder);
            break;
        }

        return GLSD301P_PUSH_EVENT_NONE;
    }

    decoder->high_run_ticks = 0u;
    decoder->low_run_ticks = sat_inc_u8(decoder->low_run_ticks);

    switch (decoder->state) {
    case PUSH_IDLE:
        if (decoder->low_run_ticks >= LOW_QUALIFY_TICKS) {
            decoder->state = PUSH_FIRST_LOW;
            decoder->active_low_ticks = 0u;
        }
        break;

    case PUSH_FIRST_LOW:
        break;

    case PUSH_INTERPULSE_GAP:
        if (decoder->low_run_ticks >= LOW_QUALIFY_TICKS) {
            decoder->state = PUSH_ACTIVE;
            decoder->active_low_ticks = 0u;
        }
        break;

    case PUSH_ACTIVE:
        decoder->active_low_ticks = sat_inc_u16(decoder->active_low_ticks);
        if (decoder->active_low_ticks >= LONG_FIRST_LOW_TICKS) {
            decoder->state = PUSH_LONG_ACTIVE;
            decoder->active_low_ticks = 0u;
            return GLSD301P_PUSH_EVENT_LEVEL_STEP;
        }
        break;

    case PUSH_LONG_ACTIVE:
        decoder->active_low_ticks = sat_inc_u16(decoder->active_low_ticks);
        if (decoder->active_low_ticks >= LONG_REPEAT_LOW_TICKS) {
            decoder->active_low_ticks = 0u;
            return GLSD301P_PUSH_EVENT_LEVEL_STEP;
        }
        break;

    default:
        reset_activity(decoder);
        break;
    }

    return GLSD301P_PUSH_EVENT_NONE;
}

glsd301p_push_dim_direction_t
glsd301p_push_decoder_direction(const glsd301p_push_decoder_t *decoder)
{
    return decoder == NULL ? GLSD301P_PUSH_DIM_DOWN : decoder->direction;
}

uint8_t glsd301p_push_level_step(uint8_t current_level,
                                 glsd301p_push_dim_direction_t direction)
{
    const uint8_t step = current_level > 150u ? 25u : 10u;

    if (direction == GLSD301P_PUSH_DIM_UP) {
        const unsigned candidate = (unsigned)current_level + (unsigned)step;
        return candidate > 254u ? 254u : (uint8_t)candidate;
    }

    /* Stock behavior uses 2 as the fallback only when the current level is
     * less than or equal to the chosen step. Otherwise it subtracts the step
     * directly (so e.g. 11 -> 1 with a step of 10). Preserve that observable
     * behavior rather than silently normalizing it. */
    if (current_level <= step) {
        return 2u;
    }
    return (uint8_t)(current_level - step);
}
