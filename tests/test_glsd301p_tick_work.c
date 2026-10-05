/*
 * R25 work-oracle driver: exactly one shared production identify tick
 * with a caller-given gap. The hosted oracle (tools/glsd301p_work_oracle.py)
 * builds this with coverage, runs a small and a large gap, and proves the
 * per-line execution counts of glsd301p_identify.c are identical — actual
 * observed work, independent of the gap. A slow-loop mutant executes its
 * loop body gap/1000 times and fails the comparison. End states pin the
 * O(1) behavior the counting run must preserve.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "glsd301p_identify.h"

int main(int argc, char **argv)
{
    glsd301p_identify_t st;
    uint16_t store = 0u;
    uint32_t gap;
    uint32_t whole;

    if (argc != 2) {
        return 2;
    }
    gap = (uint32_t)strtoul(argv[1], NULL, 10);
    whole = gap / 1000u;
    if (whole == 0u || whole >= 0xFFFFu) {
        return 2;
    }
    glsd301p_identify_init(&st);
    glsd301p_identify_on_identify(&st, 0xFFFFu, &store, 0u);
    if (store != 0xFFFFu) {
        return 1;
    }
    glsd301p_identify_tick(&st, &store, gap);
    printf("R25_TICK gap=%u store=%u\n", gap, store);
    if (store != (uint16_t)(0xFFFFu - whole)) {
        return 1;
    }
    return 0;
}
