#ifndef GLSD301P_PB4_COMPAT_H
#define GLSD301P_PB4_COMPAT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PB4 is an auxiliary active-high input in the stock GL-SD-301P application.
 * Its business meaning is intentionally unnamed.  The compatibility contract
 * is only the observed behavior: after 11 consecutive high 1 ms polls, request
 * a family-0x01 refresh at currentLevel >> 1 on every poll while PB4 remains
 * high.  Any low sample resets qualification. */
#define GLSD301P_PB4_QUALIFY_HIGH_POLLS 11u

typedef struct {
    uint8_t consecutive_high_polls;
} glsd301p_pb4_compat_t;

void glsd301p_pb4_compat_init(glsd301p_pb4_compat_t *compat);

/* Returns true when the application should issue the stock-compatible
 * half-scale family-0x01 refresh for this poll. */
bool glsd301p_pb4_compat_poll(glsd301p_pb4_compat_t *compat, bool pb4_high);

/* Compute the value byte used by the PB4 compatibility refresh.
 * This does not modify the logical Zigbee currentLevel. */
uint8_t glsd301p_pb4_compat_level(uint8_t current_level);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_PB4_COMPAT_H */
