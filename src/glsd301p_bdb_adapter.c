#include "glsd301p_bdb_adapter.h"

#include <stddef.h>

glsd301p_bdb_action_t glsd301p_bdb_handle_event(glsd301p_rejoin_t *rejoin,
                                               glsd301p_bdb_event_t event,
                                               bool factory_new)
{
    glsd301p_bdb_action_t action;

    action.start_attempt = false;
    action.stop_retry = false;
    if (rejoin == NULL) {
        return action;
    }

    switch (event) {
    case GLSD301P_BDB_INIT_JOINED:
    case GLSD301P_BDB_COMMISSION_JOINED:
        action.stop_retry = glsd301p_rejoin_note_joined(rejoin, true);
        break;
    case GLSD301P_BDB_INIT_NOT_JOINED:
    case GLSD301P_BDB_COMMISSION_NOT_JOINED:
        (void)glsd301p_rejoin_note_joined(rejoin, false);
        break;
    case GLSD301P_BDB_INIT_FAILURE:
        action.start_attempt =
            glsd301p_rejoin_note_init_failure(rejoin, factory_new);
        break;
    case GLSD301P_BDB_PARENT_LOST:
        action.start_attempt =
            glsd301p_rejoin_note_parent_lost(rejoin, factory_new);
        break;
    case GLSD301P_BDB_REJOIN_FAILURE:
        action.start_attempt =
            glsd301p_rejoin_note_rejoin_failure(rejoin, factory_new);
        break;
    case GLSD301P_BDB_COMMISSION_OTHER:
    default:
        break;
    }
    return action;
}
