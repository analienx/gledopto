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
 * reconciliation. It never touches polling policy. Rejoin triggers on
 * factory-new devices only count: a device without network state has
 * nothing to rejoin.
 *
 * Factory-new commissioning (network steering). The pinned Telink SDK
 * never starts steering by itself (bdb_init only reports joined/not
 * joined; the SDK samples call bdb_networkSteerStart explicitly). Without
 * an application-owned steering path a factory-new device -- after a
 * Leave, a reset gesture, or network NV the stack cannot restore -- would
 * stay off-network forever and lose OTA reachability, i.e. the only
 * wireless recovery surface. Steering is therefore level-triggered from a
 * 1 s maintenance tick: whenever BDB init has completed, the device is not
 * joined, is factory new and no connectivity operation is outstanding, a
 * steering attempt starts. Failed or refused attempts back off
 * 5 s, 10 s, 20 s ... capped at 300 s; an attempt that never reports a
 * terminal status is abandoned after GLSD301P_STEER_ACTIVE_TIMEOUT_S.
 * Rejoin and steering are mutually exclusive: one state field owns both.
 *
 * An accepted steering attempt ends only through a terminal commissioning
 * status or the watchdog. Joined observations do NOT end it early: BDB
 * reports the device joined while the Trust Center link-key exchange is
 * still running, and that exchange can still fail (the stack then leaves
 * and reports TCLK_EX_FAILURE). If the watchdog expires while joined, the
 * success callback was missed and the tick reconciles to joined.
 *
 * Stranded network state: a device that is neither joined nor factory new
 * while no attempt is outstanding (partially restored NV, an abandoned
 * attempt) gets a rejoin attempt every GLSD301P_UNJOINED_REJOIN_KICK_S, so
 * it is never left off-network waiting for an event that never comes.
 *
 * Dormant: after the coordinator removes the device (own Leave that the
 * application did not start), every application-started connectivity
 * attempt stops until the reset gesture or a power cycle, so a removed
 * device does not silently re-join on the next unrelated permit-join.
 *
 * States: IDLE (no attempt outstanding), SDK_ACTIVE (an accepted attempt is
 * under stack-owned backoff; intermediate failures must not restart it),
 * RETRY_PENDING (a rejected start waits for the owned 5 s retry event),
 * STEERING (an accepted steering attempt is in progress), STEER_WAIT (a
 * failed/refused steering attempt waits out its backoff on the 1 s tick).
 */

#define GLSD301P_STEER_BACKOFF_BASE_S 5u
#define GLSD301P_STEER_BACKOFF_MAX_S 300u
#define GLSD301P_STEER_ACTIVE_TIMEOUT_S 180u
#define GLSD301P_UNJOINED_REJOIN_KICK_S 60u

typedef enum {
    GLSD301P_REJOIN_IDLE = 0,
    GLSD301P_REJOIN_SDK_ACTIVE,
    GLSD301P_REJOIN_RETRY_PENDING,
    GLSD301P_REJOIN_STEERING,
    GLSD301P_REJOIN_STEER_WAIT
} glsd301p_rejoin_state_t;

typedef struct {
    uint8_t state;
    bool last_joined;
    uint32_t parent_losses;
    uint32_t starts;
    uint32_t failures;
    uint32_t successes;
    /* Factory-new commissioning bookkeeping. */
    bool bdb_ready;
    uint8_t steer_backoff_exp;
    uint16_t steer_wait_s;
    uint16_t steer_active_s;
    uint32_t steer_starts;
    uint32_t steer_failures;
    uint16_t unjoined_idle_s;
    bool dormant;
} glsd301p_rejoin_t;

/* What the 1 s maintenance tick asks the caller to do. */
typedef enum {
    GLSD301P_TICK_NONE = 0,
    /* Invoke bdb_networkSteerStart() now; report via
     * glsd301p_rejoin_note_steer_start_result(). */
    GLSD301P_TICK_STEER,
    /* Invoke zb_rejoinReqWithBackOff() now; report via
     * glsd301p_rejoin_note_start_result(). */
    GLSD301P_TICK_REJOIN,
    /* A join was reconciled without its callback: run join side effects
     * (OTA query) and stop any application retry. */
    GLSD301P_TICK_JOINED
} glsd301p_tick_t;

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

/* BDB initialization has reported (success or failure); steering may run. */
void glsd301p_rejoin_note_bdb_ready(glsd301p_rejoin_t *rejoin);

/*
 * 1 s maintenance tick: factory-new steering, steering backoff/watchdog,
 * and the periodic rejoin kick for stranded network state. Rejoin states
 * (SDK_ACTIVE / RETRY_PENDING) are never touched here.
 */
glsd301p_tick_t glsd301p_rejoin_tick(glsd301p_rejoin_t *rejoin, bool joined,
                                     bool factory_new);

/* Accepted -> STEERING; refused -> STEER_WAIT with backoff. */
void glsd301p_rejoin_note_steer_start_result(glsd301p_rejoin_t *rejoin,
                                             bool accepted);

/*
 * Terminal commissioning failure reported by BDB. Only an in-progress
 * steering attempt reacts (moves to STEER_WAIT with backoff); returns true
 * when it did.
 */
bool glsd301p_rejoin_note_steer_failure(glsd301p_rejoin_t *rejoin);

/*
 * Operator request (reset gesture on an already factory-new device): drop
 * any pending steering backoff so the next tick steers immediately.
 */
void glsd301p_rejoin_request_steer_now(glsd301p_rejoin_t *rejoin);

/*
 * The coordinator removed the device (own Leave confirmed that the
 * application did not start, outside steering). Enter dormant: no
 * application-started steering or rejoin until reset gesture / power cycle.
 * A later join (e.g. stack-driven rejoin) clears dormant.
 */
void glsd301p_rejoin_note_removed(glsd301p_rejoin_t *rejoin);

/*
 * Compact connectivity byte for the health snapshot: bits 0-2 state,
 * bit 3 dormant, bits 4-7 steering failures saturated at 15.
 */
uint8_t glsd301p_rejoin_health_byte(const glsd301p_rejoin_t *rejoin);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_REJOIN_H */
