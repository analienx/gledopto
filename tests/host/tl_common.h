#pragma once

/*
 * Host-harness stand-in for the SDK proj/tl_common.h.
 *
 * CI stages a shadow tree (build/host-sdk/proj/...) holding byte-identical
 * copies of the pinned SDK bodies under test plus this file. The SDK bodies
 * keep their own relative includes ("../tl_common.h" lands here); the real
 * SDK headers (common/types.h, utility.h, bit.h, mempool.h, utlist.h and
 * os/ev.h) are used unmodified. Only narrow hardware/opaque seams are
 * stubbed, and they live in tests/host/hw_stub.* with scripted behavior
 * owned by each test.
 *
 * NOTE: the real zcl_config.h includes ../../proj/tl_common.h, so this
 * file is parsed by every translation unit that parses the ZCL shim,
 * including the -Werror test sources. Never define anything here that
 * collides with the C library (notably assert: the SDK no-op assert is
 * injected surgically with -D on the single ev_buffer.c compile lines
 * that need it, never through this header).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "common/types.h"
#include "common/utility.h"
#include "common/bit.h"
#include "common/mempool.h"

#include "common/utlist.h"
#include "os/ev.h"

/*
 * SDK compiler.h spellings (same values as the ZCL shim; verified at
 * the pinned commit). ev_buffer.c needs the aligned attribute through
 * MEMPOOL_DECLARE, and sees only this header.
 */
#ifndef _attribute_packed_
#define _attribute_packed_ __attribute__((packed))
#endif
#ifndef _attribute_aligned_
#define _attribute_aligned_(s) __attribute__((aligned(s)))
#endif

/* TLSR8258 system-timer rate: narrow hardware constant, not behavior. */
#define S_TIMER_CLOCK_1US 16u

u32 clock_time(void);
u32 drv_disable_irq(void);
void drv_restore_irq(u32 level);
void ev_rtc_update(u32 updateTime);
