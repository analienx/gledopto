#pragma once

/*
 * Host-harness stand-in for the SDK proj/tl_common.h.
 *
 * CI stages a shadow tree (build/host-sdk/proj/...) holding byte-identical
 * copies of the pinned SDK bodies under test plus this file. The SDK bodies
 * keep their own relative includes ("../tl_common.h" lands here); the real
 * SDK headers (ev_timer.h, ev_rtc.h, ev.h, common/utlist.h) are used
 * unmodified. Only narrow hardware/opaque seams are stubbed, and they live
 * in tests/host/hw_stub.* with scripted behavior owned by each test.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "host_types.h"

#include "common/utlist.h"
#include "os/ev.h"

/* TLSR8258 system-timer rate: narrow hardware constant, not behavior. */
#define S_TIMER_CLOCK_1US 16u

u32 clock_time(void);
u32 drv_disable_irq(void);
void drv_restore_irq(u32 level);
void ev_rtc_update(u32 updateTime);
