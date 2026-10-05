/*
 * R26 allocation-request observation seam (host harness only, never
 * linked into firmware). The dispatch binaries link with
 * -Wl,--wrap=ev_buf_allocate so every pool request — including ones
 * the pool refuses — is recorded with its exact requested size
 * before the real allocator runs. Boundary tests assert the
 * recorded requests against the layout's computed sizes; a length
 * narrowing mutant requests the wrapped value and fails. Passive:
 * allocation behavior is unchanged.
 */
#include <stddef.h>
#include <stdint.h>

#include "alloc_observe.h"

#define GLSD_ALLOC_OBSERVE_MAX 256u

static uint16_t s_requests[GLSD_ALLOC_OBSERVE_MAX];
static unsigned s_count;

extern uint8_t *__real_ev_buf_allocate(uint16_t size);

uint8_t *__wrap_ev_buf_allocate(uint16_t size)
{
    if (s_count < GLSD_ALLOC_OBSERVE_MAX) {
        s_requests[s_count++] = size;
    }
    return __real_ev_buf_allocate(size);
}

void glsd_alloc_observe_reset(void)
{
    unsigned i;

    for (i = 0u; i < GLSD_ALLOC_OBSERVE_MAX; i++) {
        s_requests[i] = 0u;
    }
    s_count = 0u;
}

unsigned glsd_alloc_observe_count(void)
{
    return s_count;
}

unsigned glsd_alloc_observe_count_size(uint16_t size)
{
    unsigned i;
    unsigned n = 0u;

    for (i = 0u; i < s_count; i++) {
        if (s_requests[i] == size) {
            n++;
        }
    }
    return n;
}
