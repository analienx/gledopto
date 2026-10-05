#include "glsd301p_pb4_compat.h"

#include <stddef.h>

void glsd301p_pb4_compat_init(glsd301p_pb4_compat_t *compat)
{
    if (compat == NULL) {
        return;
    }

    compat->consecutive_high_polls = 0u;
}

bool glsd301p_pb4_compat_poll(glsd301p_pb4_compat_t *compat, bool pb4_high)
{
    if (compat == NULL) {
        return false;
    }

    if (!pb4_high) {
        compat->consecutive_high_polls = 0u;
        return false;
    }

    if (compat->consecutive_high_polls < GLSD301P_PB4_QUALIFY_HIGH_POLLS) {
        compat->consecutive_high_polls++;
    }

    return compat->consecutive_high_polls >= GLSD301P_PB4_QUALIFY_HIGH_POLLS;
}

uint8_t glsd301p_pb4_compat_level(uint8_t current_level)
{
    return (uint8_t)(current_level >> 1);
}
