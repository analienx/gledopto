#ifndef GLSD301P_TIMER_EVENTS_H
#define GLSD301P_TIMER_EVENTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Callback convention shared with the SDK timer (ev_timer_callback_t is
 * int (*)(void *)): return 0 to rearm with the same period, a positive
 * value for a new period, and a negative value to unregister. This header
 * deliberately binds no SDK header so the whole control plane stays
 * SDK-independent; only glsd301p_timer_events.c sees ev_timer.h.
 */
typedef int (*glsd301p_timer_cb_t)(void *data);

/*
 * Owned static timer events for the GL-SD-301P End Device.
 *
 * The SDK owns a pool of 24 timer events and pooled scheduling
 * (TL_ZB_TIMER_SCHEDULE) can return NULL under exhaustion; the pooled
 * cancellation wrapper additionally requires the pool `used` flag, which
 * static events never carry. These four application lifelines therefore use
 * dedicated static native events with direct ev_on_timer/ev_unon_timer and
 * explicit registration flags. Registration and cancellation run only in
 * main-loop context; never rearm from an IRQ path.
 *
 * Callbacks follow the SDK convention: return 0 to rearm with the same
 * period, a positive value for a new period, and a negative value to let the
 * SDK unregister the event.
 */

#define GLSD301P_TIMER_IO_MS 1u
#define GLSD301P_TIMER_LEVEL_MS 100u
#define GLSD301P_TIMER_HEALTH_MS 1000u
#define GLSD301P_TIMER_RETRY_MS 5000u

void glsd301p_timer_events_init(void);

/*
 * Each start routine registers its dedicated static event exactly once.
 * Starting an already-registered event counts a registration fault and
 * leaves the existing registration untouched (no duplicates, no leaks).
 */
bool glsd301p_timer_io_start(glsd301p_timer_cb_t cb, void *data);
bool glsd301p_timer_level_start(glsd301p_timer_cb_t cb, void *data);
bool glsd301p_timer_health_start(glsd301p_timer_cb_t cb, void *data);
bool glsd301p_timer_retry_start(glsd301p_timer_cb_t cb, void *data);

/*
 * One-shot variant of the retry event for the rejoin 5 s pacer. The caller
 * callback follows the SDK convention: return -1 to let the event
 * unregister (the module clears the registration flag), or a positive
 * period to re-arm this same event without tripping the
 * duplicate-registration fault (re-registering from inside the callback is
 * refused; use the return value). The membership flag is reconciled
 * against the real SDK list after every fire. Stop from any other context
 * stays a plain idempotent cancel.
 */
bool glsd301p_timer_retry_start_oneshot(glsd301p_timer_cb_t cb, void *data);

/* Each stop routine is idempotent and safe for never-registered events. */
void glsd301p_timer_io_stop(void);
void glsd301p_timer_level_stop(void);
void glsd301p_timer_health_stop(void);
void glsd301p_timer_retry_stop(void);

bool glsd301p_timer_io_registered(void);
bool glsd301p_timer_level_registered(void);
bool glsd301p_timer_health_registered(void);
bool glsd301p_timer_retry_registered(void);

/* Saturating count of duplicate/failed registration attempts. */
uint32_t glsd301p_timer_reg_faults(void);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_TIMER_EVENTS_H */
