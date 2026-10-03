#include <assert.h>
#include <stdint.h>

#include "glsd301p_timebase.h"

static void test_init_advance_now(void)
{
    glsd301p_timebase_init();
    assert(glsd301p_timebase_now_ms() == 0u);
    glsd301p_timebase_advance(1u);
    assert(glsd301p_timebase_now_ms() == 1u);
    glsd301p_timebase_advance(0u);
    assert(glsd301p_timebase_now_ms() == 1u);
    glsd301p_timebase_advance(0xFFFFFFFEu);
    assert(glsd301p_timebase_now_ms() == 0xFFFFFFFFu);
    glsd301p_timebase_init();
    assert(glsd301p_timebase_now_ms() == 0u);
}

static void test_age_across_uint32_wrap(void)
{
    assert(glsd301p_timebase_age_ms(100u, 132u) == 32u);
    assert(glsd301p_timebase_age_ms(100u, 100u) == 0u);
    assert(glsd301p_timebase_age_ms(0xFFFFFFF0u, 0x00000010u) == 0x20u);
    assert(glsd301p_timebase_age_ms(0xFFFFFFFFu, 0x00000000u) == 1u);
    assert(glsd301p_timebase_age_ms(0u, 0xFFFFFFFFu) == 0xFFFFFFFFu);
}

static void test_sat_inc(void)
{
    assert(glsd301p_sat_inc_u32(0u) == 1u);
    assert(glsd301p_sat_inc_u32(41u) == 42u);
    assert(glsd301p_sat_inc_u32(0xFFFFFFFEu) == 0xFFFFFFFFu);
    assert(glsd301p_sat_inc_u32(0xFFFFFFFFu) == 0xFFFFFFFFu);
}

int main(void)
{
    test_init_advance_now();
    test_age_across_uint32_wrap();
    test_sat_inc();
    return 0;
}
