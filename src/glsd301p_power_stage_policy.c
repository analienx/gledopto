#include "glsd301p_power_stage_policy.h"

bool glsd301p_normal_output_frame_encode(bool output_enabled,
                                         uint8_t current_level,
                                         uint8_t minimum_output,
                                         bool allow_below_min,
                                         uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    uint8_t wire_level = 0u;

    if (output_enabled) {
        wire_level = current_level;
        if (!allow_below_min && wire_level < minimum_output) {
            wire_level = minimum_output;
        }
    }

    return glsd301p_control_frame_encode(GLSD301P_CONTROL_FAMILY_LEVEL,
                                         wire_level,
                                         out);
}
