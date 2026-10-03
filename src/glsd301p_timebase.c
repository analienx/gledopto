#include "glsd301p_timebase.h"

#include <stddef.h>

static uint32_t g_now_ms;

void glsd301p_timebase_init(void)
{
    g_now_ms = 0u;
}

void glsd301p_timebase_advance(uint32_t elapsed_ms)
{
    g_now_ms += elapsed_ms;
}

uint32_t glsd301p_timebase_now_ms(void)
{
    return g_now_ms;
}

uint32_t glsd301p_timebase_age_ms(uint32_t earlier_ms, uint32_t now_ms)
{
    return now_ms - earlier_ms;
}

uint32_t glsd301p_sat_inc_u32(uint32_t value)
{
    return value == 0xFFFFFFFFu ? value : (uint32_t)(value + 1u);
}
