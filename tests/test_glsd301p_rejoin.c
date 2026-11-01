#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "glsd301p_bdb_adapter.h"
#include "glsd301p_rejoin.h"

static void test_init(void)
{
    glsd301p_rejoin_t r;

    glsd301p_rejoin_init(&r);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
    assert(r.last_joined == false);
    assert(r.parent_losses == 0u);
    assert(r.starts == 0u);
    assert(r.failures == 0u);
    assert(r.successes == 0u);
    glsd301p_rejoin_init(NULL);
    assert(glsd301p_rejoin_state(NULL) == GLSD301P_REJOIN_IDLE);
}

static void test_single_outstanding_attempt(void)
{
    glsd301p_rejoin_t r;

    glsd301p_rejoin_init(&r);
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == true);
    assert(r.parent_losses == 1u);
    glsd301p_rejoin_note_start_result(&r, true);
    assert(r.starts == 1u);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_SDK_ACTIVE);

    /* Intermediate failure of the accepted attempt: counted, never restarted. */
    assert(glsd301p_rejoin_note_rejoin_failure(&r, false) == false);
    assert(r.failures == 1u);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_SDK_ACTIVE);

    /* Further triggers while active never start a second attempt. */
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == false);
    assert(r.parent_losses == 2u);
    assert(glsd301p_rejoin_note_init_failure(&r, false) == false);
    assert(r.failures == 2u);
    assert(r.starts == 1u);
}

static void test_rejected_start_paced_retry(void)
{
    glsd301p_rejoin_t r;

    glsd301p_rejoin_init(&r);
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == true);
    glsd301p_rejoin_note_start_result(&r, false);
    assert(r.starts == 1u);
    assert(r.failures == 1u);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_RETRY_PENDING);

    /* Triggers while pending never start; the retry event owns the re-attempt. */
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == false);
    assert(glsd301p_rejoin_note_rejoin_failure(&r, false) == false);
    assert(glsd301p_rejoin_note_init_failure(&r, false) == false);
    assert(r.starts == 1u);

    assert(glsd301p_rejoin_note_retry_fire(&r) == true);
    glsd301p_rejoin_note_start_result(&r, true);
    assert(r.starts == 2u);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_SDK_ACTIVE);

    /* A stale fire outside RETRY_PENDING returns false. */
    assert(glsd301p_rejoin_note_retry_fire(&r) == false);
}

static void test_joined_reconciliation(void)
{
    glsd301p_rejoin_t r;

    glsd301p_rejoin_init(&r);
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == true);
    glsd301p_rejoin_note_start_result(&r, true);

    /* Each false->true edge counts once and returns to IDLE. */
    assert(glsd301p_rejoin_note_joined(&r, true) == true);
    assert(r.successes == 1u);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
    assert(glsd301p_rejoin_note_joined(&r, true) == false);
    assert(r.successes == 1u);
    assert(glsd301p_rejoin_note_joined(&r, false) == false);
    assert(glsd301p_rejoin_note_joined(&r, true) == true);
    assert(r.successes == 2u);

    /* Recovery is possible after reconciliation. */
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == true);
}

static void test_factory_new_never_rejoins(void)
{
    glsd301p_rejoin_t r;

    glsd301p_rejoin_init(&r);
    assert(glsd301p_rejoin_note_parent_lost(&r, true) == false);
    assert(glsd301p_rejoin_note_rejoin_failure(&r, true) == false);
    assert(glsd301p_rejoin_note_init_failure(&r, true) == false);
    assert(r.starts == 0u);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
    /* Parent losses still count on factory-new devices for diagnostics. */
    assert(r.parent_losses == 1u);
}

static void test_null_safety(void)
{
    assert(glsd301p_rejoin_note_parent_lost(NULL, false) == false);
    assert(glsd301p_rejoin_note_rejoin_failure(NULL, false) == false);
    assert(glsd301p_rejoin_note_init_failure(NULL, false) == false);
    assert(glsd301p_rejoin_note_retry_fire(NULL) == false);
    glsd301p_rejoin_note_start_result(NULL, true);
    assert(glsd301p_rejoin_note_joined(NULL, true) == false);
}

/*
 * R3: two complete join -> loss -> accepted recovery -> success cycles
 * through the shared target/harness adapter. The second loss must start
 * recovery again: a stale cached joined edge must not strand SDK_ACTIVE.
 */
static void test_adapter_two_accepted_cycles(void)
{
    glsd301p_rejoin_t r;
    glsd301p_bdb_action_t action;
    int cycle;

    glsd301p_rejoin_init(&r);
    action = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_INIT_JOINED,
                                       false);
    assert(action.stop_retry == true);
    assert(action.start_attempt == false);
    assert(r.successes == 1u);

    for (cycle = 0; cycle < 2; cycle++) {
        action = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_PARENT_LOST,
                                           false);
        assert(action.start_attempt == true);
        glsd301p_rejoin_note_start_result(&r, true);
        assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_SDK_ACTIVE);

        /* Duplicate loss observations never start a second attempt. */
        action = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_PARENT_LOST,
                                           false);
        assert(action.start_attempt == false);

        action = glsd301p_bdb_handle_event(
            &r, GLSD301P_BDB_COMMISSION_JOINED, false);
        assert(action.stop_retry == true);
        assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
    }
    assert(r.starts == 2u);
    assert(r.successes == 3u);
}

/*
 * R3: a rejected start strands RETRY_PENDING; authoritative joined
 * evidence arriving before the owned retry fires must reconcile to IDLE
 * and stop the retry.
 */
static void test_adapter_rejected_then_external_success(void)
{
    glsd301p_rejoin_t r;
    glsd301p_bdb_action_t action;

    glsd301p_rejoin_init(&r);
    action = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_INIT_JOINED,
                                       false);
    assert(action.stop_retry == true);

    action = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_PARENT_LOST,
                                       false);
    assert(action.start_attempt == true);
    glsd301p_rejoin_note_start_result(&r, false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_RETRY_PENDING);

    action = glsd301p_bdb_handle_event(
        &r, GLSD301P_BDB_COMMISSION_JOINED, false);
    assert(action.stop_retry == true);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);

    /* A stale retry fire afterwards does nothing. */
    assert(glsd301p_rejoin_note_retry_fire(&r) == false);
}

/* Duplicate SUCCESS observations count once and request no retry stop. */
static void test_adapter_duplicate_success_dedup(void)
{
    glsd301p_rejoin_t r;
    glsd301p_bdb_action_t action;

    glsd301p_rejoin_init(&r);
    action = glsd301p_bdb_handle_event(
        &r, GLSD301P_BDB_COMMISSION_JOINED, false);
    assert(action.stop_retry == true);
    assert(r.successes == 1u);
    action = glsd301p_bdb_handle_event(
        &r, GLSD301P_BDB_COMMISSION_JOINED, false);
    assert(action.stop_retry == false);
    assert(r.successes == 1u);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
}

static void test_adapter_startup_factory_and_other(void)
{
    glsd301p_rejoin_t r;
    glsd301p_bdb_action_t action;

    /* Non-factory startup failure starts recovery. */
    glsd301p_rejoin_init(&r);
    action = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_INIT_FAILURE,
                                       false);
    assert(action.start_attempt == true);
    assert(r.failures == 1u);

    /* Factory-new startup/loss observations never start. */
    glsd301p_rejoin_init(&r);
    action = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_INIT_FAILURE,
                                       true);
    assert(action.start_attempt == false);
    assert(r.failures == 0u);
    action = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_PARENT_LOST,
                                       true);
    assert(action.start_attempt == false);
    assert(r.starts == 0u);

    /* SUCCESS without joined evidence only records the observation. */
    glsd301p_rejoin_init(&r);
    action = glsd301p_bdb_handle_event(
        &r, GLSD301P_BDB_COMMISSION_NOT_JOINED, false);
    assert(action.start_attempt == false);
    assert(action.stop_retry == false);
    assert(r.successes == 0u);

    /* Other commission statuses are ignored, like the target today. */
    action = glsd301p_bdb_handle_event(
        &r, GLSD301P_BDB_COMMISSION_OTHER, false);
    assert(action.start_attempt == false);
    assert(action.stop_retry == false);

    /* NULL rejoin is a safe no-op. */
    action = glsd301p_bdb_handle_event(NULL, GLSD301P_BDB_PARENT_LOST,
                                       false);
    assert(action.start_attempt == false);
    assert(action.stop_retry == false);
}

/* ---- Factory-new commissioning (network steering) ---- */

/* Drive the 1 s tick until it asks to steer; returns ticks consumed. */
static unsigned ticks_until_steer(glsd301p_rejoin_t *r, unsigned limit)
{
    unsigned n;

    for (n = 1u; n <= limit; n++) {
        if (glsd301p_bdb_tick(r, false, true).start_steer) {
            return n;
        }
    }
    return 0u;
}

static void test_steer_waits_for_bdb_init(void)
{
    glsd301p_rejoin_t r;
    glsd301p_bdb_action_t a;

    glsd301p_rejoin_init(&r);
    /* Before any BDB init report, never steer (bdb_init still running). */
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == false);
    a = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_INIT_NOT_JOINED, true);
    assert(a.start_attempt == false && a.start_steer == false);
    assert(r.bdb_ready == true);
    /* First tick after init: factory new and unjoined -> steer. */
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);

    /* Init failure also completes init. */
    glsd301p_rejoin_init(&r);
    (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_INIT_FAILURE, true);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
}

static void test_steer_never_on_joined_or_non_factory_new(void)
{
    glsd301p_rejoin_t r;

    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    /* Joined: never steer, regardless of the FN flag. */
    assert(glsd301p_bdb_tick(&r, true, true).start_steer == false);
    assert(glsd301p_bdb_tick(&r, true, false).start_steer == false);
    /* Unjoined but holding network state: rejoin's job, never steering
     * over preserved state. */
    assert(glsd301p_bdb_tick(&r, false, false).start_steer == false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);

    /* Rejoin in progress is never overlapped by steering. */
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == true);
    glsd301p_rejoin_note_start_result(&r, true);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_SDK_ACTIVE);
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == true);
    glsd301p_rejoin_note_start_result(&r, false);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_RETRY_PENDING);
}

static void test_steer_accepted_success(void)
{
    glsd301p_rejoin_t r;
    glsd301p_bdb_action_t a;

    glsd301p_rejoin_init(&r);
    (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_INIT_NOT_JOINED, true);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, true);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEERING);
    assert(r.steer_starts == 1u);
    /* While steering, the tick never starts another attempt. */
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == false);
    a = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_COMMISSION_JOINED, false);
    assert(a.stop_retry == true);
    assert(a.joined == true);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
    assert(r.successes == 1u);
    assert(glsd301p_bdb_tick(&r, true, false).start_steer == false);
}

static void test_steer_failure_backoff_and_cap(void)
{
    static const unsigned expected[] = {5u, 10u, 20u, 40u, 80u, 160u, 300u, 300u};
    glsd301p_rejoin_t r;
    unsigned i;

    glsd301p_rejoin_init(&r);
    (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_INIT_NOT_JOINED, true);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    for (i = 0u; i < sizeof(expected) / sizeof(expected[0]); i++) {
        glsd301p_rejoin_note_steer_start_result(&r, true);
        (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_STEER_FAILURE, true);
        assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEER_WAIT);
        assert(ticks_until_steer(&r, 1000u) == expected[i]);
    }
    assert(r.steer_failures == sizeof(expected) / sizeof(expected[0]));

    /* Joining resets the backoff: the next failure waits 5 s again. */
    glsd301p_rejoin_note_steer_start_result(&r, true);
    (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_COMMISSION_JOINED, false);
    (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_INIT_NOT_JOINED, true);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, true);
    (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_STEER_FAILURE, true);
    assert(ticks_until_steer(&r, 1000u) == 5u);
}

static void test_steer_refused_and_not_joined_success(void)
{
    glsd301p_rejoin_t r;

    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    /* SDK busy: refused start backs off like a failure. */
    glsd301p_rejoin_note_steer_start_result(&r, false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEER_WAIT);
    assert(ticks_until_steer(&r, 1000u) == 5u);

    /* SUCCESS status without joined evidence ends the attempt too. */
    glsd301p_rejoin_note_steer_start_result(&r, true);
    (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_COMMISSION_NOT_JOINED, true);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEER_WAIT);

    /* A steer failure outside STEERING changes nothing. */
    glsd301p_rejoin_init(&r);
    assert(glsd301p_rejoin_note_steer_failure(&r) == false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
}

static void test_steer_watchdog(void)
{
    glsd301p_rejoin_t r;
    unsigned i;

    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, true);
    for (i = 1u; i < GLSD301P_STEER_ACTIVE_TIMEOUT_S; i++) {
        assert(glsd301p_bdb_tick(&r, false, true).start_steer == false);
        assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEERING);
    }
    /* No terminal status ever arrived: abandon and back off. */
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEER_WAIT);
    assert(r.steer_failures == 1u);
    assert(ticks_until_steer(&r, 1000u) == 5u);
}

static void test_steer_missed_join_callback_reconciles(void)
{
    glsd301p_rejoin_t r;
    glsd301p_bdb_action_t a;
    unsigned i;

    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, true);
    /*
     * Joined observed by the tick without a commissioning callback: not
     * trusted until the watchdog (TCLK may still be running), then
     * reconciled with the full join side effects.
     */
    for (i = 1u; i < GLSD301P_STEER_ACTIVE_TIMEOUT_S; i++) {
        a = glsd301p_bdb_tick(&r, true, false);
        assert(!a.start_steer && !a.start_attempt && !a.joined);
        assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEERING);
    }
    a = glsd301p_bdb_tick(&r, true, false);
    assert(a.joined == true && a.stop_retry == true);
    assert(a.start_steer == false && a.start_attempt == false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
    assert(r.successes == 1u);
    assert(r.steer_failures == 0u);

    /* Joined observed while waiting out a backoff: reconciled at once. */
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEER_WAIT);
    a = glsd301p_bdb_tick(&r, true, false);
    assert(a.joined == true && a.stop_retry == true);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);

    /* Network state appearing during the wait hands back to rejoin. */
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, false);
    assert(glsd301p_bdb_tick(&r, false, false).start_steer == false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
}

static void test_steer_now_request(void)
{
    glsd301p_rejoin_t r;
    unsigned i;

    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    for (i = 0u; i < 6u; i++) {
        glsd301p_rejoin_note_steer_start_result(&r, false);
        assert(ticks_until_steer(&r, 1000u) != 0u);
    }
    glsd301p_rejoin_note_steer_start_result(&r, false); /* long wait now */
    glsd301p_rejoin_request_steer_now(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    /* Backoff restarted from the base as well. */
    glsd301p_rejoin_note_steer_start_result(&r, false);
    assert(ticks_until_steer(&r, 1000u) == 5u);

    glsd301p_rejoin_request_steer_now(NULL);
    assert(glsd301p_bdb_tick(NULL, false, true).start_steer == false);
    assert(glsd301p_rejoin_tick(NULL, false, true) == GLSD301P_TICK_NONE);
    glsd301p_rejoin_note_removed(NULL);
    assert(glsd301p_rejoin_health_byte(NULL) == 0u);
    glsd301p_rejoin_note_steer_start_result(NULL, true);
    glsd301p_rejoin_note_bdb_ready(NULL);
}

static void test_rejoin_paths_ignore_steering_state(void)
{
    glsd301p_rejoin_t r;

    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, true);
    /* Rejoin triggers during steering never start a rejoin attempt. */
    assert(glsd301p_rejoin_note_parent_lost(&r, true) == false);
    assert(glsd301p_rejoin_note_rejoin_failure(&r, false) == false);
    assert(glsd301p_rejoin_note_init_failure(&r, false) == false);
    assert(glsd301p_rejoin_note_retry_fire(&r) == false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEERING);
}

/*
 * Review finding 2: BDB reports joined while the TC link-key exchange is
 * still running. A TCLK failure after that must still count as a steering
 * failure with growing backoff, not as a success.
 */
static void test_steer_tclk_window_failure_backs_off(void)
{
    glsd301p_rejoin_t r;
    unsigned cycle;
    unsigned i;
    static const unsigned expected[] = {5u, 10u, 20u};

    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    for (cycle = 0u; cycle < 3u; cycle++) {
        glsd301p_rejoin_note_steer_start_result(&r, true);
        /* Associated: joined=true, still factory new, TCLK in flight. */
        for (i = 0u; i < 10u; i++) {
            glsd301p_bdb_action_t a = glsd301p_bdb_tick(&r, true, true);
            assert(!a.joined && !a.start_steer && !a.start_attempt);
        }
        assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEERING);
        /* TCLK fails, the stack leaves, then reports the failure. */
        (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_STEER_FAILURE, true);
        assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEER_WAIT);
        assert(ticks_until_steer(&r, 1000u) == expected[cycle]);
    }
    assert(r.successes == 0u);
    assert(r.steer_failures == 3u);
}

/*
 * Review finding 3: network state that is neither joined nor factory new,
 * with no attempt outstanding, gets a periodic rejoin kick.
 */
static void test_stranded_network_state_rejoin_kick(void)
{
    glsd301p_rejoin_t r;
    glsd301p_bdb_action_t a;
    unsigned i;

    glsd301p_rejoin_init(&r);
    (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_INIT_NOT_JOINED, false);
    for (i = 1u; i < GLSD301P_UNJOINED_REJOIN_KICK_S; i++) {
        a = glsd301p_bdb_tick(&r, false, false);
        assert(!a.start_attempt && !a.start_steer && !a.joined);
    }
    a = glsd301p_bdb_tick(&r, false, false);
    assert(a.start_attempt == true && a.start_steer == false);
    /* Accepted attempt: stack-owned, no further kicks while active. */
    glsd301p_rejoin_note_start_result(&r, true);
    for (i = 0u; i < 3u * GLSD301P_UNJOINED_REJOIN_KICK_S; i++) {
        assert(glsd301p_bdb_tick(&r, false, false).start_attempt == false);
    }
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_SDK_ACTIVE);

    /* A joined observation in between restarts the grace period. */
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    for (i = 1u; i < GLSD301P_UNJOINED_REJOIN_KICK_S; i++) {
        assert(glsd301p_bdb_tick(&r, false, false).start_attempt == false);
    }
    assert(glsd301p_bdb_tick(&r, true, false).start_attempt == false);
    for (i = 1u; i < GLSD301P_UNJOINED_REJOIN_KICK_S; i++) {
        assert(glsd301p_bdb_tick(&r, false, false).start_attempt == false);
    }
    assert(glsd301p_bdb_tick(&r, false, false).start_attempt == true);

    /* Abandoned steering that left network state hands over to the kick. */
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, false);
    assert(glsd301p_bdb_tick(&r, false, false).start_steer == false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
    for (i = 1u; i < GLSD301P_UNJOINED_REJOIN_KICK_S; i++) {
        assert(glsd301p_bdb_tick(&r, false, false).start_attempt == false);
    }
    assert(glsd301p_bdb_tick(&r, false, false).start_attempt == true);
}

/*
 * Review finding 9: after a coordinator removal the device stays dormant
 * (no steering, no rejoin) until a later join clears it.
 */
static void test_removed_device_goes_dormant(void)
{
    glsd301p_rejoin_t r;
    glsd301p_bdb_action_t a;
    unsigned i;

    /* Own Leave outside steering = coordinator removal: dormant at once. */
    glsd301p_rejoin_init(&r);
    (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_INIT_JOINED, false);
    glsd301p_rejoin_note_own_leave(&r);
    assert(r.dormant == true);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
    /* Factory new after the Leave: never steers while dormant. */
    for (i = 0u; i < 1000u; i++) {
        a = glsd301p_bdb_tick(&r, false, true);
        assert(!a.start_steer && !a.start_attempt && !a.joined);
    }

    /*
     * Follow-up review B1: dormant never blocks a rejoin. A Leave with
     * rejoin=1 keeps network state (not factory new); the device must get
     * back on through every rejoin path.
     */
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == true);
    glsd301p_rejoin_note_start_result(&r, false);
    assert(glsd301p_rejoin_note_retry_fire(&r) == true);
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    glsd301p_rejoin_note_own_leave(&r);
    for (i = 1u; i < GLSD301P_UNJOINED_REJOIN_KICK_S; i++) {
        assert(glsd301p_bdb_tick(&r, false, false).start_attempt == false);
    }
    assert(glsd301p_bdb_tick(&r, false, false).start_attempt == true);

    /* Removal while a rejoin is outstanding leaves the rejoin alone. */
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == true);
    glsd301p_rejoin_note_start_result(&r, false);
    glsd301p_rejoin_note_removed(&r);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_RETRY_PENDING);
    assert(glsd301p_rejoin_note_retry_fire(&r) == true);

    /* Removal during a steering backoff ends it. */
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, false);
    glsd301p_rejoin_note_removed(&r);
    assert(ticks_until_steer(&r, 1000u) == 0u);

    /* A later join (stack-driven) clears dormant. */
    a = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_COMMISSION_JOINED, false);
    assert(a.joined == true);
    assert(r.dormant == false);
}

/*
 * Follow-up review B2: an own Leave during steering is BDB's TCLK-failure
 * Leave only if the terminal failure follows within the settle time;
 * otherwise it was a removal. Steering is held meanwhile.
 */
static void test_own_leave_during_steering(void)
{
    glsd301p_rejoin_t r;
    glsd301p_bdb_action_t a;
    unsigned i;

    /* Explained by TCLK_EX_FAILURE a few seconds later: normal backoff. */
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, true);
    glsd301p_rejoin_note_own_leave(&r);
    assert(r.dormant == false);
    for (i = 0u; i < 3u; i++) {
        a = glsd301p_bdb_tick(&r, false, true);
        assert(!a.start_steer && !a.start_attempt);
    }
    (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_STEER_FAILURE, true);
    assert(r.dormant == false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_STEER_WAIT);
    assert(ticks_until_steer(&r, 1000u) == 5u);

    /* Unexplained (coordinator removed it mid-commissioning): dormant. */
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, true);
    glsd301p_rejoin_note_own_leave(&r);
    for (i = 0u; i < GLSD301P_STEER_LEAVE_SETTLE_S; i++) {
        a = glsd301p_bdb_tick(&r, false, true);
        assert(!a.start_steer && !a.start_attempt && !a.joined);
        assert(r.dormant == false);
    }
    a = glsd301p_bdb_tick(&r, false, true);
    assert(!a.start_steer);
    assert(r.dormant == true);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
    assert(ticks_until_steer(&r, 1000u) == 0u);

    /* Even the steering watchdog cannot restart steering in the window. */
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, true);
    r.steer_active_s = GLSD301P_STEER_ACTIVE_TIMEOUT_S - 1u;
    glsd301p_rejoin_note_own_leave(&r);
    for (i = 0u; i <= GLSD301P_STEER_LEAVE_SETTLE_S; i++) {
        assert(glsd301p_bdb_tick(&r, false, true).start_steer == false);
    }
    assert(r.dormant == true);
}

/* Follow-up review B5: a stalled stack-owned rejoin is abandoned. */
static void test_sdk_active_watchdog(void)
{
    glsd301p_rejoin_t r;
    glsd301p_bdb_action_t a;
    unsigned i;
    uint32_t failures;

    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == true);
    glsd301p_rejoin_note_start_result(&r, true);
    failures = r.failures;
    for (i = 1u; i < GLSD301P_REJOIN_ACTIVE_TIMEOUT_S; i++) {
        /* Intermediate failures keep it stack-owned. */
        if (i % 100u == 0u) {
            assert(glsd301p_rejoin_note_rejoin_failure(&r, false) == false);
        }
        a = glsd301p_bdb_tick(&r, false, false);
        assert(!a.start_attempt && !a.start_steer);
        assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_SDK_ACTIVE);
    }
    (void)glsd301p_bdb_tick(&r, false, false);
    assert(glsd301p_rejoin_state(&r) == GLSD301P_REJOIN_IDLE);
    assert(r.failures > failures);
    /* The stranded kick then starts a fresh attempt. */
    for (i = 1u; i < GLSD301P_UNJOINED_REJOIN_KICK_S; i++) {
        assert(glsd301p_bdb_tick(&r, false, false).start_attempt == false);
    }
    assert(glsd301p_bdb_tick(&r, false, false).start_attempt == true);

    /* A join before the timeout ends it normally. */
    glsd301p_rejoin_init(&r);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_rejoin_note_parent_lost(&r, false) == true);
    glsd301p_rejoin_note_start_result(&r, true);
    for (i = 0u; i < 100u; i++) {
        (void)glsd301p_bdb_tick(&r, false, false);
    }
    a = glsd301p_bdb_handle_event(&r, GLSD301P_BDB_COMMISSION_JOINED, false);
    assert(a.joined && a.stop_retry);
    assert(r.sdk_active_s == 0u);
}

static void test_health_byte(void)
{
    glsd301p_rejoin_t r;
    unsigned i;

    glsd301p_rejoin_init(&r);
    assert(glsd301p_rejoin_health_byte(&r) == 0u);
    glsd301p_rejoin_note_bdb_ready(&r);
    assert(glsd301p_bdb_tick(&r, false, true).start_steer == true);
    glsd301p_rejoin_note_steer_start_result(&r, true);
    assert(glsd301p_rejoin_health_byte(&r) == GLSD301P_REJOIN_STEERING);
    (void)glsd301p_bdb_handle_event(&r, GLSD301P_BDB_STEER_FAILURE, true);
    assert(glsd301p_rejoin_health_byte(&r) ==
           (uint8_t)(GLSD301P_REJOIN_STEER_WAIT | 0x10u));
    for (i = 0u; i < 40u; i++) {
        glsd301p_rejoin_note_steer_start_result(&r, false);
    }
    glsd301p_rejoin_note_removed(&r);
    /* Failures saturate at 15, dormant bit set, state idle. */
    assert(glsd301p_rejoin_health_byte(&r) == 0xF8u);
}

int main(void)
{
    test_init();
    test_single_outstanding_attempt();
    test_rejected_start_paced_retry();
    test_joined_reconciliation();
    test_factory_new_never_rejoins();
    test_null_safety();
    test_adapter_two_accepted_cycles();
    test_adapter_rejected_then_external_success();
    test_adapter_duplicate_success_dedup();
    test_adapter_startup_factory_and_other();
    test_steer_waits_for_bdb_init();
    test_steer_never_on_joined_or_non_factory_new();
    test_steer_accepted_success();
    test_steer_failure_backoff_and_cap();
    test_steer_refused_and_not_joined_success();
    test_steer_watchdog();
    test_steer_missed_join_callback_reconciles();
    test_steer_now_request();
    test_rejoin_paths_ignore_steering_state();
    test_steer_tclk_window_failure_backs_off();
    test_stranded_network_state_rejoin_kick();
    test_removed_device_goes_dormant();
    test_own_leave_during_steering();
    test_sdk_active_watchdog();
    test_health_byte();
    printf("GLSD301P_REJOIN=PASS\n");
    return 0;
}
