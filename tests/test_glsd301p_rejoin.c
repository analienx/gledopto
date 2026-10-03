#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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

int main(void)
{
    test_init();
    test_single_outstanding_attempt();
    test_rejected_start_paced_retry();
    test_joined_reconciliation();
    test_factory_new_keeps_stock_startup();
    test_null_safety();
    printf("GLSD301P_REJOIN=PASS\n");
    return 0;
}
