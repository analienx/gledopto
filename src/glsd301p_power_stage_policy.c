#include "glsd301p_power_stage_policy.h"

#include <stddef.h>

bool glsd301p_normal_output_frame_encode(bool output_enabled,
                                         uint8_t current_level,
                                         uint8_t minimum_output,
                                         bool allow_below_min,
                                         uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    uint8_t wire_level = 0u;

    if (out == NULL) {
        return false;
    }

    /*
     * OFF is deliberately independent of persisted/configured level state.
     * Even corrupt/unknown level metadata must never prevent us from emitting
     * the confirmed electrical-OFF frame.
     */
    if (!output_enabled) {
        return glsd301p_control_frame_encode(GLSD301P_CONTROL_FAMILY_LEVEL,
                                             0u,
                                             out);
    }

    /* Zigbee Level Control reserves 0xFF as unknown/invalid currentLevel. */
    if (current_level == 0xFFu) {
        return false;
    }

    wire_level = current_level;
    if (!allow_below_min) {
        if (minimum_output == 0xFFu) {
            return false;
        }
        if (wire_level < minimum_output) {
            wire_level = minimum_output;
        }
    }

    return glsd301p_control_frame_encode(GLSD301P_CONTROL_FAMILY_LEVEL,
                                         wire_level,
                                         out);
}
