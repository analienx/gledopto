#ifndef GLSD301P_TIMEBASE_H
#define GLSD301P_TIMEBASE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Monotonic millisecond timebase for the GL-SD-301P End Device.
 *
 * The SDK main loop derives whole elapsed milliseconds from the wrapping
 * hardware tick counter on every iteration (unsigned delta, fractional ticks
 * retained inside the SDK). A narrow hosted-build patch feeds that exact
 * elapsed value here via glsd301p_timebase_advance(), so application
 * deadlines measure real elapsed time rather than callback counts.
 *
 * glsd301p_timebase_advance() runs inside the SDK timer update with IRQs
 * locked; it is a single O(1) addition and must stay that way. All readers
 * run in main-loop context. The counter wraps modulo 2^32; all age
 * computations use unsigned subtraction and remain correct across the wrap.
 */

void glsd301p_timebase_init(void);

/* SDK hook. Adds elapsed whole milliseconds to the monotonic counter. */
void glsd301p_timebase_advance(uint32_t elapsed_ms);

uint32_t glsd301p_timebase_now_ms(void);

/* Wrap-safe age in milliseconds: (now_ms - earlier_ms) modulo 2^32. */
uint32_t glsd301p_timebase_age_ms(uint32_t earlier_ms, uint32_t now_ms);

/* Saturating increment for RAM-only diagnostic counters. */
uint32_t glsd301p_sat_inc_u32(uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_TIMEBASE_H */
