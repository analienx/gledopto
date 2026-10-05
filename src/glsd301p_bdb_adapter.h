#ifndef GLSD301P_BDB_ADAPTER_H
#define GLSD301P_BDB_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>

#include "glsd301p_rejoin.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hostable BDB/rejoin integration: the exact callback-to-rejoin decision
 * sequence, shared by the Telink target and the hosted harness.
 *
 * The target maps SDK BDB statuses to these events (thin mapping,
 * reviewed with the target diff); everything else — attempt ownership,
 * joined reconciliation, retry actions — lives here, identically on
 * target and host. Tests drive this adapter, never a reordered or
 * shortened copy of the target's observations.
 */

typedef enum {
    GLSD301P_BDB_INIT_JOINED = 0,
    GLSD301P_BDB_INIT_NOT_JOINED,
    GLSD301P_BDB_INIT_FAILURE,
    GLSD301P_BDB_COMMISSION_JOINED,
    GLSD301P_BDB_COMMISSION_NOT_JOINED,
    GLSD301P_BDB_PARENT_LOST,
    GLSD301P_BDB_REJOIN_FAILURE,
    GLSD301P_BDB_COMMISSION_OTHER
} glsd301p_bdb_event_t;

typedef struct {
    /* Caller must invoke zb_rejoinReqWithBackOff now and report the
     * outcome through glsd301p_rejoin_note_start_result(). */
    bool start_attempt;
    /* Caller must stop the owned 5 s retry event (idempotent). */
    bool stop_retry;
} glsd301p_bdb_action_t;

glsd301p_bdb_action_t glsd301p_bdb_handle_event(glsd301p_rejoin_t *rejoin,
                                               glsd301p_bdb_event_t event,
                                               bool factory_new);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_BDB_ADAPTER_H */
