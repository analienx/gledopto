#include "glsd301p_identify.h"

#include "glsd301p_timebase.h"

void glsd301p_identify_init(glsd301p_identify_t *st)
{
    if (st == NULL) {
        return;
    }
    st->countdown = 0u;
    st->shadow = 0u;
    st->second_mark_ms = 0u;
    st->second_mark_valid = false;
    st->effect_active = false;
    st->effect_id = GLSD301P_IDENTIFY_EFFECT_BLINK;
    st->effect_start_ms = 0u;
    st->effect_saved_onoff = false;
    st->effect_saved_level = 0u;
}

void glsd301p_identify_on_identify(glsd301p_identify_t *st, uint16_t seconds,
                                   uint16_t *store, uint32_t now_ms)
{
    if (st == NULL) {
        return;
    }
    st->countdown = seconds;
    st->shadow = seconds;
    st->second_mark_ms = now_ms;
    st->second_mark_valid = true;
    if (store != NULL) {
        *store = seconds;
    }
}

void glsd301p_identify_tick(glsd301p_identify_t *st, uint16_t *store,
                            uint32_t now_ms)
{
    if (st == NULL || store == NULL) {
        return;
    }
    if (!st->second_mark_valid) {
        st->second_mark_ms = now_ms;
        st->second_mark_valid = true;
        return;
    }
    while (glsd301p_timebase_age_ms(st->second_mark_ms, now_ms) >= 1000u) {
        st->second_mark_ms += 1000u;
        if (*store != st->shadow) {
            /*
             * Externally written value (IdentifyTime writes notify
             * nobody): adopt it, charging the elapsed phase as one
             * second (ceiling behavior: a value written up to a
             * second ago has a second elapsed).
             */
            st->countdown = *store;
            if (st->countdown > 0u) {
                st->countdown--;
            }
            *store = st->countdown;
            st->shadow = st->countdown;
        } else if (st->countdown > 0u) {
            st->countdown--;
            *store = st->countdown;
            st->shadow = st->countdown;
        }
    }
}

bool glsd301p_identify_effect_supported(uint8_t effect_id,
                                        uint8_t effect_variant)
{
    if (effect_variant != GLSD301P_IDENTIFY_EFFECT_VARIANT_DEFAULT) {
        return false;
    }
    return effect_id == GLSD301P_IDENTIFY_EFFECT_BLINK ||
           effect_id == GLSD301P_IDENTIFY_EFFECT_BREATHE;
}

void glsd301p_identify_effect_begin(glsd301p_identify_t *st, uint8_t effect_id,
                                    uint32_t now_ms, bool save_onoff,
                                    uint8_t save_level)
{
    if (st == NULL) {
        return;
    }
    st->effect_saved_onoff = save_onoff;
    st->effect_saved_level = save_level;
    st->effect_active = true;
    st->effect_id = effect_id;
    st->effect_start_ms = now_ms;
}

bool glsd301p_identify_effect_active(const glsd301p_identify_t *st)
{
    return st != NULL && st->effect_active;
}

bool glsd301p_identify_effect_step(const glsd301p_identify_t *st,
                                   uint32_t now_ms, uint8_t min_level,
                                   uint8_t max_level, bool *out_onoff,
                                   uint8_t *out_level)
{
    uint32_t elapsed;

    if (st == NULL || !st->effect_active || out_onoff == NULL ||
        out_level == NULL) {
        return false;
    }
    elapsed = glsd301p_timebase_age_ms(st->effect_start_ms, now_ms);
    if (st->effect_id == GLSD301P_IDENTIFY_EFFECT_BREATHE) {
        /*
         * One 2 s swell to the far extreme and back. The extreme is
         * whichever end sits farther from the saved level, so the
         * program stays visible from any starting state including OFF.
         */
        uint32_t half = GLSD301P_IDENTIFY_BREATHE_MS / 2u;
        uint32_t pos;
        uint8_t extreme;
        uint8_t from;
        uint8_t to;
        uint32_t span;
        uint32_t moved;

        if (elapsed >= GLSD301P_IDENTIFY_BREATHE_MS) {
            return false;
        }
        extreme = (st->effect_saved_level <=
                   (uint8_t)(((uint16_t)min_level + (uint16_t)max_level) /
                              2u))
                      ? max_level
                      : min_level;
        if (elapsed < half) {
            from = st->effect_saved_level;
            to = extreme;
            pos = elapsed;
        } else {
            from = extreme;
            to = st->effect_saved_level;
            pos = elapsed - half;
        }
        span = from > to ? (uint32_t)(from - to) : (uint32_t)(to - from);
        /* span <= 254, pos < 1000: span * pos < 2^32, moved <= span. */
        moved = span * pos / half;
        *out_onoff = true;
        *out_level =
            (uint8_t)(from > to ? (uint32_t)from - moved
                                : (uint32_t)from + moved);
        return true;
    }
    if (elapsed >= GLSD301P_IDENTIFY_BLINK_MS) {
        return false;
    }
    /*
     * Blink: 250 ms phases alternating full-bright ON against the
     * saved level with output cut. Unknown ids cannot reach here (the
     * caller validates), so anything non-breathe renders as blink.
     */
    if (((elapsed / GLSD301P_IDENTIFY_BLINK_PHASE_MS) % 2u) == 0u) {
        *out_onoff = true;
        *out_level = max_level;
    } else {
        *out_onoff = false;
        *out_level = st->effect_saved_level;
    }
    return true;
}

void glsd301p_identify_effect_clear(glsd301p_identify_t *st)
{
    if (st == NULL) {
        return;
    }
    st->effect_active = false;
}

bool glsd301p_identify_effect_saved_onoff(const glsd301p_identify_t *st)
{
    return st != NULL && st->effect_saved_onoff;
}

uint8_t glsd301p_identify_effect_saved_level(const glsd301p_identify_t *st)
{
    return st != NULL ? st->effect_saved_level : 0u;
}
