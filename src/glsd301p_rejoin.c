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
    rejoin->bdb_ready = false;
    rejoin->steer_backoff_exp = 0u;
    rejoin->steer_wait_s = 0u;
    rejoin->steer_active_s = 0u;
    rejoin->steer_starts = 0u;
    rejoin->steer_failures = 0u;
}

static bool glsd301p_rejoin_is_steer_state(uint8_t state)
{
    return state == GLSD301P_REJOIN_STEERING ||
           state == GLSD301P_REJOIN_STEER_WAIT;
}

/* Backoff for the n-th consecutive failure: 5 s << n, capped at 300 s. */
static uint16_t glsd301p_rejoin_steer_backoff_s(uint8_t exp)
{
    uint32_t wait = GLSD301P_STEER_BACKOFF_BASE_S;
    uint8_t i;

    for (i = 0u; i < exp && wait < GLSD301P_STEER_BACKOFF_MAX_S; i++) {
        wait <<= 1;
    }
    if (wait > GLSD301P_STEER_BACKOFF_MAX_S) {
        wait = GLSD301P_STEER_BACKOFF_MAX_S;
    }
    return (uint16_t)wait;
}

static void glsd301p_rejoin_enter_steer_wait(glsd301p_rejoin_t *rejoin)
{
    rejoin->steer_failures = glsd301p_sat_inc_u32(rejoin->steer_failures);
    rejoin->steer_wait_s = glsd301p_rejoin_steer_backoff_s(rejoin->steer_backoff_exp);
    if (rejoin->steer_backoff_exp < 16u) {
        rejoin->steer_backoff_exp++;
    }
    rejoin->steer_active_s = 0u;
    rejoin->state = GLSD301P_REJOIN_STEER_WAIT;
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
    /* R3: observed loss invalidates the cached joined edge, so the next
     * authoritative success reconciles instead of looking like a
     * duplicate of an older join. */
    rejoin->last_joined = false;
    rejoin->parent_losses = glsd301p_sat_inc_u32(rejoin->parent_losses);
    return glsd301p_rejoin_decide_attempt(rejoin, factory_new);
}

bool glsd301p_rejoin_note_rejoin_failure(glsd301p_rejoin_t *rejoin,
                                         bool factory_new)
{
    if (rejoin == NULL) {
        return false;
    }
    rejoin->last_joined = false;
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
    rejoin->last_joined = false;
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
    if (!joined) {
        rejoin->last_joined = false;
        return false;
    }
    /*
     * R3: authoritative joined evidence always reconciles to IDLE and
     * tells the caller to stop any application retry, even if the edge
     * bookkeeping somehow already reads joined. Success counting stays
     * edge-deduplicated separately.
     */
    edge = !rejoin->last_joined;
    rejoin->last_joined = true;
    /* A successful join ends every commissioning backoff. */
    rejoin->steer_backoff_exp = 0u;
    rejoin->steer_wait_s = 0u;
    rejoin->steer_active_s = 0u;
    if (edge) {
        rejoin->successes = glsd301p_sat_inc_u32(rejoin->successes);
    }
    if (rejoin->state != GLSD301P_REJOIN_IDLE) {
        rejoin->state = GLSD301P_REJOIN_IDLE;
        return true;
    }
    return edge;
}

uint8_t glsd301p_rejoin_state(const glsd301p_rejoin_t *rejoin)
{
    return rejoin == NULL ? GLSD301P_REJOIN_IDLE : rejoin->state;
}

void glsd301p_rejoin_note_bdb_ready(glsd301p_rejoin_t *rejoin)
{
    if (rejoin != NULL) {
        rejoin->bdb_ready = true;
    }
}

bool glsd301p_rejoin_steer_tick(glsd301p_rejoin_t *rejoin, bool joined,
                                bool factory_new)
{
    if (rejoin == NULL || !rejoin->bdb_ready) {
        return false;
    }

    if (joined) {
        /* Authoritative joined state ends any steering bookkeeping even if
         * the commissioning callback was missed. Rejoin states belong to
         * the callback reconciliation path and are left alone. */
        if (glsd301p_rejoin_is_steer_state(rejoin->state)) {
            (void)glsd301p_rejoin_note_joined(rejoin, true);
        }
        return false;
    }

    switch (rejoin->state) {
    case GLSD301P_REJOIN_IDLE:
        return factory_new;
    case GLSD301P_REJOIN_STEER_WAIT:
        if (!factory_new) {
            /* Network state appeared without a join observation: hand the
             * device back to the rejoin path, never steer over it. */
            rejoin->state = GLSD301P_REJOIN_IDLE;
            rejoin->steer_wait_s = 0u;
            return false;
        }
        if (rejoin->steer_wait_s > 1u) {
            rejoin->steer_wait_s--;
            return false;
        }
        rejoin->steer_wait_s = 0u;
        return true;
    case GLSD301P_REJOIN_STEERING:
        if (rejoin->steer_active_s < 0xFFFFu) {
            rejoin->steer_active_s++;
        }
        if (rejoin->steer_active_s >= GLSD301P_STEER_ACTIVE_TIMEOUT_S) {
            glsd301p_rejoin_enter_steer_wait(rejoin);
        }
        return false;
    default:
        return false;
    }
}

void glsd301p_rejoin_note_steer_start_result(glsd301p_rejoin_t *rejoin,
                                             bool accepted)
{
    if (rejoin == NULL) {
        return;
    }
    rejoin->steer_starts = glsd301p_sat_inc_u32(rejoin->steer_starts);
    if (accepted) {
        rejoin->state = GLSD301P_REJOIN_STEERING;
        rejoin->steer_active_s = 0u;
        rejoin->steer_wait_s = 0u;
    } else {
        glsd301p_rejoin_enter_steer_wait(rejoin);
    }
}

bool glsd301p_rejoin_note_steer_failure(glsd301p_rejoin_t *rejoin)
{
    if (rejoin == NULL || rejoin->state != GLSD301P_REJOIN_STEERING) {
        return false;
    }
    rejoin->last_joined = false;
    glsd301p_rejoin_enter_steer_wait(rejoin);
    return true;
}

void glsd301p_rejoin_request_steer_now(glsd301p_rejoin_t *rejoin)
{
    if (rejoin == NULL) {
        return;
    }
    rejoin->steer_backoff_exp = 0u;
    if (rejoin->state == GLSD301P_REJOIN_STEER_WAIT) {
        rejoin->steer_wait_s = 0u;
    }
}
