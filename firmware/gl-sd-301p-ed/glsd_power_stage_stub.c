#include "glsd_power_stage.h"

/*
 * Compile/link stub only. It deliberately refuses hardware activation until
 * the installed GL-SD power-stage interface is identified and implemented.
 */
int glsd_power_stage_init(void)
{
    return -1;
}

int glsd_power_stage_apply(uint8_t on, uint8_t level)
{
    (void)on;
    (void)level;
    return -1;
}

int glsd_power_stage_push_pressed(void)
{
    return 0;
}
