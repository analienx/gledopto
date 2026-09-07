#ifndef GLSD301P_POWER_STAGE_POLICY_H
#define GLSD301P_POWER_STAGE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "glsd301p_uart_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Encode the stock firmware's normal On/Off + Level output path.
 *
 * Confirmed external behavior:
 *   off -> A5 5A 01 00 04 AA
 *   on  -> A5 5A 01 LL 04 AA
 *
 * For ordinary operation LL is current_level, clamped to minimum_output when
 * below the configured minimum.  The explicit allow_below_min flag models the
 * stock firmware's separately controlled bypass state without assigning a
 * guessed high-level meaning to that state.
 *
 * This function does not implement family-0x02 operations or the special
 * currentLevel>>1 path.
 */
bool glsd301p_normal_output_frame_encode(bool output_enabled,
                                         uint8_t current_level,
                                         uint8_t minimum_output,
                                         bool allow_below_min,
                                         uint8_t out[GLSD301P_CONTROL_FRAME_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_POWER_STAGE_POLICY_H */
