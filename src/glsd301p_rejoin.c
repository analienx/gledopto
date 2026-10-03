#include "glsd301p_rejoin.h"

#include <stddef.h>

#include "glsd301p_timebase.h"

void glsd301p_rejoin_init(glsd301p_rejoin_t *rejoin)
{
    if (rejoin == NULL) {
        return;
    }

    rejoin->state = GLSD301P_REJOIN_IDLE;
    rejoin->last_joined = false;
    rejoin->parent_losses = 0u;
    rejoin->starts = 0u;
    rejoin->failures = 0u;
    rejoin->successes = 0u;
}

static bool glsd301p_rejoin_decide_attempt(glsd301p_rejoin_t *rejoin,
                                           bool factory_new)
{
    if (rejoin == NULL || factory_new) {
        return false;
    }
    return rejoin->state == GLSD301P_REJOIN_IDLE;
}

bool glsd301p_rejoin_note_parent_lost(glsd301p_rejoin_t *rejoin,
                                      bool factory_new)
{
    if (rejoin == NULL) {
        return false;
    }
    rejoin->parent_losses = glsd301p_sat_inc_u32(rejoin->parent_losses);
    return glsd301p_rejoin_decide_attempt(rejoin, factory_new);
}

bool glsd301p_rejoin_note_rejoin_failure(glsd301p_rejoin_t *rejoin,
                                         bool factory_new)
{
    if (rejoin == NULL) {
        return false;
    }
    if (rejoin->state == GLSD301P_REJOIN_SDK_ACTIVE) {
        /*
         * Intermediate failure of an accepted attempt: count it, keep the
         * stack-owned backoff untouched, never restart here.
         */
        rejoin->failures = glsd301p_sat_inc_u32(rejoin->failures);
        return false;
    }
    if (factory_new) {
        return false;
    }
    return rejoin->state == GLSD301P_REJOIN_IDLE;
}

bool glsd301p_rejoin_note_init_failure(glsd301p_rejoin_t *rejoin,
                                       bool factory_new)
{
    if (rejoin == NULL) {
        return false;
    }
    if (!factory_new) {
        rejoin->failures = glsd301p_sat_inc_u32(rejoin->failures);
    }
    return glsd301p_rejoin_decide_attempt(rejoin, factory_new);
}

bool glsd301p_rejoin_note_retry_fire(glsd301p_rejoin_t *rejoin)
{
    if (rejoin == NULL) {
        return false;
    }
    return rejoin->state == GLSD301P_REJOIN_RETRY_PENDING;
}

void glsd301p_rejoin_note_start_result(glsd301p_rejoin_t *rejoin,
                                       bool accepted)
{
    if (rejoin == NULL) {
        return;
    }
    rejoin->starts = glsd301p_sat_inc_u32(rejoin->starts);
    if (accepted) {
        rejoin->state = GLSD301P_REJOIN_SDK_ACTIVE;
    } else {
        rejoin->failures = glsd301p_sat_inc_u32(rejoin->failures);
        rejoin->state = GLSD301P_REJOIN_RETRY_PENDING;
    }
}

bool glsd301p_rejoin_note_joined(glsd301p_rejoin_t *rejoin, bool joined)
{
    bool edge;

    if (rejoin == NULL) {
        return false;
    }
    edge = joined && !rejoin->last_joined;
    rejoin->last_joined = joined;
    if (edge) {
        rejoin->successes = glsd301p_sat_inc_u32(rejoin->successes);
        rejoin->state = GLSD301P_REJOIN_IDLE;
        return true;
    }
    return false;
}

uint8_t glsd301p_rejoin_state(const glsd301p_rejoin_t *rejoin)
{
    return rejoin == NULL ? GLSD301P_REJOIN_IDLE : rejoin->state;
}
