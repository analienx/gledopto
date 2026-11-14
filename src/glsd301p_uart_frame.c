#include "glsd301p_uart_frame.h"

bool glsd301p_control_frame_encode(uint8_t family,
                                   uint8_t value,
                                   uint8_t out[GLSD301P_CONTROL_FRAME_SIZE])
{
    if (out == NULL) {
        return false;
    }

    if (family != GLSD301P_CONTROL_FAMILY_LEVEL &&
        family != GLSD301P_CONTROL_FAMILY_OPERATION) {
        return false;
    }

    out[0] = 0xA5u;
    out[1] = 0x5Au;
    out[2] = family;
    out[3] = value;
    out[4] = 0x04u;
    out[5] = 0xAAu;
    return true;
}
