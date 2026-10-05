#ifndef GLSD301P_REJOIN_H
#define GLSD301P_REJOIN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Application-owned rejoin attempt ownership for the GL-SD-301P End Device.
 *
 * The stack owns backoff for an accepted rejoin attempt; this module owns
 * everything around it: exactly one outstanding attempt at a time, a paced
 * retry no sooner than 5 s after a rejected start, and joined-state
 * reconciliation. It never touches polling policy. Factory-new devices
 * keep the stock commissioning startup untouched (triggers only count).
 *
 * States: IDLE (no attempt outstanding), SDK_ACTIVE (an accepted attempt is
 * under stack-owned backoff; intermediate failures must not restart it),
 * RETRY_PENDING (a rejected start waits for the owned 5 s retry event).
 */

typedef enum {
    GLSD301P_REJOIN_IDLE = 0,
    GLSD301P_REJOIN_SDK_ACTIVE,
    GLSD301P_REJOIN_RETRY_PENDING
} glsd301p_rejoin_state_t;

typedef struct {
    uint8_t state;
    bool last_joined;
    uint32_t parent_losses;
    uint32_t starts;
    uint32_t failures;
    uint32_t successes;
} glsd301p_rejoin_t;

void glsd301p_rejoin_init(glsd301p_rejoin_t *rejoin);

/*
 * Trigger inputs. Each returns true when the caller must invoke
 * zb_rejoinReqWithBackOff immediately and report the outcome through
 * glsd301p_rejoin_note_start_result(). factory_new preserves the stock
 * commissioning startup: triggers count but never attempt.
 */
bool glsd301p_rejoin_note_parent_lost(glsd301p_rejoin_t *rejoin,
                                      bool factory_new);
bool glsd301p_rejoin_note_rejoin_failure(glsd301p_rejoin_t *rejoin,
                                         bool factory_new);
bool glsd301p_rejoin_note_init_failure(glsd301p_rejoin_t *rejoin,
                                       bool factory_new);

/*
 * Owned 5 s retry event fired. Returns true only from RETRY_PENDING; a
 * stale fire returns false and the caller must leave the event stopped.
 */
bool glsd301p_rejoin_note_retry_fire(glsd301p_rejoin_t *rejoin);

/*
 * Outcome of an invoked start. Accepted starts move to SDK_ACTIVE under
 * stack-owned backoff; rejected starts move to RETRY_PENDING (the caller
 * arms the 5 s retry event) and count a failure.
 */
void glsd301p_rejoin_note_start_result(glsd301p_rejoin_t *rejoin,
                                       bool accepted);

/*
 * Authoritative joined-state reconciliation. A true observation always
 * returns to IDLE and returns true so the caller stops any application
 * retry; each false->true edge additionally counts once as a success.
 * Duplicate true observations while already IDLE return false. Loss and
 * failure observations invalidate the cached joined edge.
 */
bool glsd301p_rejoin_note_joined(glsd301p_rejoin_t *rejoin, bool joined);

uint8_t glsd301p_rejoin_state(const glsd301p_rejoin_t *rejoin);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_REJOIN_H */
