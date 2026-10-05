#include "tl_common.h"
#include "ev_timer.h"

#include "glsd301p_timer_events.h"

#include <stddef.h>
#include <string.h>

static ev_timer_event_t g_io_event;
static bool g_io_registered;
static ev_timer_event_t g_level_event;
static bool g_level_registered;
static ev_timer_event_t g_health_event;
static bool g_health_registered;
static ev_timer_event_t g_retry_event;
static bool g_retry_registered;
static glsd301p_timer_cb_t g_retry_oneshot_cb;
static void *g_retry_oneshot_data;
static uint32_t g_reg_faults;

void glsd301p_timer_events_init(void)
{
    memset(&g_io_event, 0, sizeof(g_io_event));
    g_io_registered = false;
    memset(&g_level_event, 0, sizeof(g_level_event));
    g_level_registered = false;
    memset(&g_health_event, 0, sizeof(g_health_event));
    g_health_registered = false;
    memset(&g_retry_event, 0, sizeof(g_retry_event));
    g_retry_registered = false;
    g_retry_oneshot_cb = NULL;
    g_retry_oneshot_data = NULL;
    g_reg_faults = 0u;
}

static void glsd301p_timer_note_reg_fault(void)
{
    if (g_reg_faults < 0xFFFFFFFFu) {
        g_reg_faults++;
    }
}

static bool glsd301p_timer_start_one(ev_timer_event_t *event,
                                     bool *registered,
                                     glsd301p_timer_cb_t cb,
                                     void *data,
                                     uint32_t period_ms)
{
    if (event == NULL || registered == NULL || cb == NULL) {
        glsd301p_timer_note_reg_fault();
        return false;
    }
    if (*registered) {
        glsd301p_timer_note_reg_fault();
        return false;
    }

    memset(event, 0, sizeof(*event));
    event->cb = cb;
    event->data = data;
    ev_on_timer(event, period_ms);
    if (!ev_timer_exist(event)) {
        glsd301p_timer_note_reg_fault();
        return false;
    }

    *registered = true;
    return true;
}

static void glsd301p_timer_stop_one(ev_timer_event_t *event, bool *registered)
{
    if (event == NULL || registered == NULL || !*registered) {
        return;
    }

    /*
     * Direct cancellation: the pooled taskCancel wrapper requires the pool
     * `used` flag and would refuse these static events. ev_unon_timer is
     * safe here by list-membership check; static storage is never freed.
     */
    ev_unon_timer(event);
    *registered = false;
}

bool glsd301p_timer_io_start(glsd301p_timer_cb_t cb, void *data)
{
    return glsd301p_timer_start_one(&g_io_event, &g_io_registered, cb, data,
                                    GLSD301P_TIMER_IO_MS);
}

bool glsd301p_timer_level_start(glsd301p_timer_cb_t cb, void *data)
{
    return glsd301p_timer_start_one(&g_level_event, &g_level_registered, cb,
                                    data, GLSD301P_TIMER_LEVEL_MS);
}

bool glsd301p_timer_health_start(glsd301p_timer_cb_t cb, void *data)
{
    return glsd301p_timer_start_one(&g_health_event, &g_health_registered, cb,
                                    data, GLSD301P_TIMER_HEALTH_MS);
}

bool glsd301p_timer_retry_start(glsd301p_timer_cb_t cb, void *data)
{
    return glsd301p_timer_start_one(&g_retry_event, &g_retry_registered, cb,
                                    data, GLSD301P_TIMER_RETRY_MS);
}

static int glsd301p_timer_retry_oneshot_wrap(void *data)
{
    glsd301p_timer_cb_t cb;
    void *cb_data;
    int rc;

    (void)data;
    cb = g_retry_oneshot_cb;
    cb_data = g_retry_oneshot_data;
    rc = (cb != NULL) ? cb(cb_data) : -1;
    if (rc < 0) {
        g_retry_oneshot_cb = NULL;
        g_retry_oneshot_data = NULL;
        g_retry_registered = false;
        return -1;
    }
    /*
     * Rearm request: the SDK rearms this same event with the returned
     * period, so the stash stays for the next fire. ev_timer_exist is the
     * real SDK membership check and keeps the flag honest if the caller
     * stopped the event from inside its own callback before returning.
     */
    g_retry_registered = ev_timer_exist(&g_retry_event) ? true : false;
    return rc;
}

bool glsd301p_timer_retry_start_oneshot(glsd301p_timer_cb_t cb, void *data)
{
    /*
     * The wrapper is never NULL so start_one cannot take its NULL fault
     * path; a NULL caller callback simply fires as a no-op one-shot. The
     * stash lands only on success, so a refused start leaves no residue.
     * Single-threaded main-loop servicing means no fire can interleave.
     */
    if (!glsd301p_timer_start_one(&g_retry_event, &g_retry_registered,
                                  glsd301p_timer_retry_oneshot_wrap, NULL,
                                  GLSD301P_TIMER_RETRY_MS)) {
        return false;
    }
    g_retry_oneshot_cb = cb;
    g_retry_oneshot_data = data;
    return true;
}

void glsd301p_timer_io_stop(void)
{
    glsd301p_timer_stop_one(&g_io_event, &g_io_registered);
}

void glsd301p_timer_level_stop(void)
{
    glsd301p_timer_stop_one(&g_level_event, &g_level_registered);
}

void glsd301p_timer_health_stop(void)
{
    glsd301p_timer_stop_one(&g_health_event, &g_health_registered);
}

void glsd301p_timer_retry_stop(void)
{
    glsd301p_timer_stop_one(&g_retry_event, &g_retry_registered);
}

bool glsd301p_timer_io_registered(void)
{
    return g_io_registered;
}

bool glsd301p_timer_level_registered(void)
{
    return g_level_registered;
}

bool glsd301p_timer_health_registered(void)
{
    return g_health_registered;
}

bool glsd301p_timer_retry_registered(void)
{
    return g_retry_registered;
}

uint32_t glsd301p_timer_reg_faults(void)
{
    return g_reg_faults;
}
