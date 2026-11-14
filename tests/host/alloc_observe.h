#ifndef GLSD301P_ALLOC_OBSERVE_H
#define GLSD301P_ALLOC_OBSERVE_H

#include <stdint.h>

/* R26 allocation-request observation (host harness only). */
void glsd_alloc_observe_reset(void);
unsigned glsd_alloc_observe_count(void);
/* Number of recorded requests with exactly this size. */
unsigned glsd_alloc_observe_count_size(uint16_t size);

#endif
