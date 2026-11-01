#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host_types.h"
#include "ev_timer.h"
#include "ev.h"

#include "glsd301p_timebase.h"
#include "glsd301p_timer_events.h"

#include "hw_stub.h"

static unsigned g_io_fires;
static unsigned g_level_fires;
static unsigned g_retry_fires;
static unsigned g_dummy_fires;

static int io_counter_cb(void *data)
{
    (void)data;
    g_io_fires++;
    return 0;
}

static int level_counter_cb(void *data)
{
    (void)data;
    g_level_fires++;
    return 0;
}

static int retry_oneshot_cb(void *data)
{
    (void)data;
    g_retry_fires++;
    glsd301p_timer_retry_stop();
    return -1;
}

static int dummy_cb(void *data)
{
    (void)data;
    g_dummy_fires++;
    return -1;
}

static unsigned g_oneshot_fires;
static unsigned g_oneshot_rearms;

static int oneshot_rearm_cb(void *data)
{
    (void)data;
    g_oneshot_fires++;
    if (g_oneshot_rearms > 0u) {
        g_oneshot_rearms--;
        /* Re-arm via the return value: no re-register, no fault. */
        return (int)GLSD301P_TIMER_RETRY_MS;
    }
    return -1;
}

static void test_setup(uint32_t start_tick)
{
    host_stub_reset();
    host_clock_set(start_tick);
    ev_timer_init();
    ev_timer_setPrevSysTick(start_tick);
    glsd301p_timebase_init();
    glsd301p_timer_events_init();
    g_io_fires = 0u;
    g_level_fires = 0u;
    g_retry_fires = 0u;
    g_dummy_fires = 0u;
    g_oneshot_fires = 0u;
    g_oneshot_rearms = 0u;
}

/* Pump the real SDK scheduler forward by whole milliseconds. */
static void pump_ms(uint32_t ms)
{
    uint32_t i;

    for (i = 0u; i < ms; i++) {
        host_clock_advance(HOST_TICKS_PER_MS);
        ev_timer_process();
    }
}

static void test_pool_exhaustion_returns_null(void)
{
    ev_timer_event_t *held[24];
    ev_timer_event_t *overflow;
    unsigned i;

    test_setup(0u);

    /* F1 trigger reproduction against the real pool: 24 posts succeed. */
    for (i = 0u; i < 24u; i++) {
        held[i] = ev_timer_taskPost(dummy_cb, NULL, 0xFFFFFFu);
        assert(held[i] != NULL);
    }
    assert(!ev_timer_enough());
    assert(host_exception_count() == 0u);

    /* The 25th post fails and reports through the SDK exception path. */
    overflow = ev_timer_taskPost(dummy_cb, NULL, 1u);
    assert(overflow == NULL);
    assert(host_exception_count() == 1u);
    assert(host_exception_code() == SYS_EXCEPTTION_COMMON_TIMER_EVEVT);

    /* Static lifelines register and fire while the pool stays full. */
    assert(glsd301p_timer_io_start(io_counter_cb, NULL));
    assert(glsd301p_timer_level_start(level_counter_cb, NULL));
    assert(glsd301p_timer_io_registered());
    assert(glsd301p_timer_level_registered());
    pump_ms(250u);
    assert(g_io_fires == 250u);
    assert(g_level_fires == 2u);
    assert(glsd301p_timebase_now_ms() == 250u);

    /* A duplicate start faults without disturbing the live registration. */
    assert(!glsd301p_timer_io_start(io_counter_cb, NULL));
    assert(glsd301p_timer_reg_faults() == 1u);
    assert(glsd301p_timer_io_registered());
    pump_ms(10u);
    assert(g_io_fires == 260u);

    /* Release the pool; static events keep working afterwards. */
    for (i = 0u; i < 24u; i++) {
        (void)ev_timer_taskCancel(&held[i]);
        assert(held[i] == NULL);
    }
    assert(g_dummy_fires == 0u);
    assert(ev_timer_enough());
    pump_ms(10u);
    assert(g_io_fires == 270u);
    assert(g_level_fires == 2u);

    glsd301p_timer_io_stop();
    glsd301p_timer_level_stop();
    assert(!glsd301p_timer_io_registered());
    assert(!glsd301p_timer_level_registered());
    pump_ms(10u);
    assert(g_io_fires == 270u);
}

static void test_stop_is_idempotent_and_restart_clean(void)
{
    test_setup(100000u);

    glsd301p_timer_level_stop();
    assert(!glsd301p_timer_level_registered());
    assert(glsd301p_timer_level_start(level_counter_cb, NULL));
    glsd301p_timer_level_stop();
    glsd301p_timer_level_stop();
    assert(!glsd301p_timer_level_registered());
    pump_ms(200u);
    assert(g_level_fires == 0u);

    /* Restart after stop works exactly once per period. */
    assert(glsd301p_timer_level_start(level_counter_cb, NULL));
    pump_ms(200u);
    assert(g_level_fires == 2u);
    glsd301p_timer_level_stop();
    assert(glsd301p_timer_reg_faults() == 0u);
}

static void test_pooled_cancel_wrapper_refuses_static_events(void)
{
    ev_timer_event_t *alias;

    test_setup(200000u);
    assert(glsd301p_timer_io_start(io_counter_cb, NULL));

    /*
     * The pooled taskCancel wrapper keys off the pool `used` flag, which
     * static events never carry: it must refuse (nonzero) and leave the
     * live registration untouched. Direct ev_unon_timer remains the only
     * valid static path (exercised by every stop above).
     */
    /* Reach the static event through the SDK existence check only. */
    alias = ev_timer_nearestGet();
    assert(alias != NULL);
    assert(ev_timer_taskCancel(&alias) != 0u);
    assert(alias != NULL);
    assert(glsd301p_timer_io_registered());
    pump_ms(5u);
    assert(g_io_fires == 5u);
    glsd301p_timer_io_stop();
}

static void test_oneshot_retry_fires_once(void)
{
    test_setup(300000u);

    assert(glsd301p_timer_retry_start(retry_oneshot_cb, NULL));
    assert(glsd301p_timer_retry_registered());
    pump_ms(4999u);
    assert(g_retry_fires == 0u);
    pump_ms(1u);
    assert(g_retry_fires == 1u);
    assert(!glsd301p_timer_retry_registered());
    pump_ms(5000u);
    assert(g_retry_fires == 1u);
    assert(glsd301p_timer_reg_faults() == 0u);
}

static void test_wrap_and_delayed_process(void)
{
    test_setup(0xFFFF0000u);

    assert(glsd301p_timer_io_start(io_counter_cb, NULL));

    /* Hardware tick wrap mid-stream: firing stays exact. */
    pump_ms(10u);
    assert(g_io_fires == 10u);
    assert(glsd301p_timebase_now_ms() == 10u);

    /*
     * One delayed 50 ms scheduler pass fires once (no catch-up fan-out)
     * while the timebase hook still receives the full elapsed 50 ms.
     */
    host_clock_advance(50u * HOST_TICKS_PER_MS);
    ev_timer_process();
    assert(g_io_fires == 11u);
    assert(glsd301p_timebase_now_ms() == 60u);

    /* Whole-millisecond pumping stays exact after the jump. */
    pump_ms(40u);
    assert(g_io_fires == 51u);
    assert(glsd301p_timebase_now_ms() == 100u);
    glsd301p_timer_io_stop();
}

static void test_fractional_ticks_accumulate(void)
{
    test_setup(0x12340000u);

    assert(glsd301p_timer_io_start(io_counter_cb, NULL));

    /*
     * Two 0.5 ms scheduler passes carry the fractional remainder inside
     * the SDK and deliver exactly one whole millisecond to the hook. All
     * earlier tests in this binary advance whole milliseconds only, so the
     * SDK sub-millisecond remainder is deterministically zero here.
     */
    host_clock_advance(HOST_TICKS_PER_MS / 2u);
    ev_timer_process();
    assert(glsd301p_timebase_now_ms() == 0u);
    host_clock_advance(HOST_TICKS_PER_MS / 2u);
    ev_timer_process();
    assert(glsd301p_timebase_now_ms() == 1u);
    assert(g_io_fires == 1u);
    glsd301p_timer_io_stop();
}

static void test_retry_oneshot_wrapper(void)
{
    test_setup(400000u);

    /* One fire, then the SDK unregisters and the flag clears itself. */
    assert(glsd301p_timer_retry_start_oneshot(oneshot_rearm_cb, NULL));
    assert(glsd301p_timer_retry_registered());
    pump_ms(4999u);
    assert(g_oneshot_fires == 0u);
    pump_ms(1u);
    assert(g_oneshot_fires == 1u);
    assert(!glsd301p_timer_retry_registered());
    pump_ms(5000u);
    assert(g_oneshot_fires == 1u);
    assert(glsd301p_timer_reg_faults() == 0u);

    /* A fresh one-shot can arm immediately after the self-unregister. */
    assert(glsd301p_timer_retry_start_oneshot(oneshot_rearm_cb, NULL));
    assert(glsd301p_timer_retry_registered());
    pump_ms(5000u);
    assert(g_oneshot_fires == 2u);
    assert(!glsd301p_timer_retry_registered());
    assert(glsd301p_timer_reg_faults() == 0u);
}

static void test_retry_oneshot_return_rearm(void)
{
    test_setup(500000u);
    g_oneshot_rearms = 2u;

    /* Two return-value re-arms, then a final unregister: three fires. */
    assert(glsd301p_timer_retry_start_oneshot(oneshot_rearm_cb, NULL));
    pump_ms(5000u);
    assert(g_oneshot_fires == 1u);
    assert(glsd301p_timer_retry_registered());
    pump_ms(5000u);
    assert(g_oneshot_fires == 2u);
    assert(glsd301p_timer_retry_registered());
    pump_ms(5000u);
    assert(g_oneshot_fires == 3u);
    assert(!glsd301p_timer_retry_registered());
    pump_ms(5000u);
    assert(g_oneshot_fires == 3u);
    assert(glsd301p_timer_reg_faults() == 0u);
}

static void test_retry_oneshot_stop_cancels(void)
{
    test_setup(600000u);

    /* Stop from outside cancels the pending one-shot. */
    assert(glsd301p_timer_retry_start_oneshot(oneshot_rearm_cb, NULL));
    glsd301p_timer_retry_stop();
    assert(!glsd301p_timer_retry_registered());
    pump_ms(6000u);
    assert(g_oneshot_fires == 0u);

    /* A duplicate start while pending faults and keeps the first arming. */
    assert(glsd301p_timer_retry_start_oneshot(oneshot_rearm_cb, NULL));
    assert(!glsd301p_timer_retry_start_oneshot(oneshot_rearm_cb, NULL));
    assert(glsd301p_timer_reg_faults() == 1u);
    assert(glsd301p_timer_retry_registered());
    pump_ms(5000u);
    assert(g_oneshot_fires == 1u);
    glsd301p_timer_retry_stop();
}

int main(void)
{
    test_pool_exhaustion_returns_null();
    test_stop_is_idempotent_and_restart_clean();
    test_pooled_cancel_wrapper_refuses_static_events();
    test_oneshot_retry_fires_once();
    test_retry_oneshot_wrapper();
    test_retry_oneshot_return_rearm();
    test_retry_oneshot_stop_cancels();
    test_wrap_and_delayed_process();
    test_fractional_ticks_accumulate();
    return 0;
}
