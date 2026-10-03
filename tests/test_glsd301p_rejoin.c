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

static void test_factory_new_keeps_stock_startup(void)
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

int main(void)
{
    test_init();
    test_single_outstanding_attempt();
    test_rejected_start_paced_retry();
    test_joined_reconciliation();
    test_factory_new_keeps_stock_startup();
    test_null_safety();
    test_adapter_two_accepted_cycles();
    test_adapter_rejected_then_external_success();
    test_adapter_duplicate_success_dedup();
    test_adapter_startup_factory_and_other();
    printf("GLSD301P_REJOIN=PASS\n");
    return 0;
}
