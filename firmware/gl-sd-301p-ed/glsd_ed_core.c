#include "glsd_ed_core.h"

static uint8_t clamp_level(const glsd_ed_core_t *core, uint8_t level)
{
    if (level < core->min_level) {
        return core->min_level;
    }
    if (level > core->max_level) {
        return core->max_level;
    }
    return level;
}

static int apply(glsd_ed_core_t *core)
{
    if (!core->hw.apply_output) {
        return GLSD_ED_ERR_HW;
    }
    return core->hw.apply_output(core->hw.user, core->on, core->level) == 0
        ? GLSD_ED_OK
        : GLSD_ED_ERR_HW;
}

int glsd_ed_core_init(
    glsd_ed_core_t *core,
    const glsd_ed_hw_t *hw,
    uint8_t initial_on,
    uint8_t initial_level,
    uint8_t min_level,
    uint8_t max_level
)
{
    if (!core || !hw || !hw->apply_output || min_level == 0 || min_level > max_level) {
        return GLSD_ED_ERR_ARG;
    }

    core->hw = *hw;
    core->min_level = min_level;
    core->max_level = max_level;
    core->level = clamp_level(core, initial_level ? initial_level : min_level);
    core->last_nonzero_level = core->level;
    core->on = initial_on ? 1u : 0u;

    return apply(core);
}

int glsd_ed_set_on(glsd_ed_core_t *core, uint8_t on)
{
    uint8_t old_on;
    uint8_t old_level;

    if (!core) {
        return GLSD_ED_ERR_ARG;
    }

    old_on = core->on;
    old_level = core->level;

    core->on = on ? 1u : 0u;
    if (core->on && core->level < core->min_level) {
        core->level = core->last_nonzero_level >= core->min_level
            ? core->last_nonzero_level
            : core->min_level;
    }

    if (apply(core) != GLSD_ED_OK) {
        core->on = old_on;
        core->level = old_level;
        return GLSD_ED_ERR_HW;
    }
    return GLSD_ED_OK;
}

int glsd_ed_toggle(glsd_ed_core_t *core)
{
    if (!core) {
        return GLSD_ED_ERR_ARG;
    }
    return glsd_ed_set_on(core, core->on ? 0u : 1u);
}

int glsd_ed_set_level(glsd_ed_core_t *core, uint8_t level, uint8_t with_onoff)
{
    uint8_t old_on;
    uint8_t old_level;
    uint8_t old_last;

    if (!core) {
        return GLSD_ED_ERR_ARG;
    }

    old_on = core->on;
    old_level = core->level;
    old_last = core->last_nonzero_level;

    if (with_onoff && level == 0) {
        core->on = 0u;
    } else {
        core->level = clamp_level(core, level);
        core->last_nonzero_level = core->level;
        if (with_onoff) {
            core->on = 1u;
        }
    }

    if (apply(core) != GLSD_ED_OK) {
        core->on = old_on;
        core->level = old_level;
        core->last_nonzero_level = old_last;
        return GLSD_ED_ERR_HW;
    }
    return GLSD_ED_OK;
}

int glsd_ed_step_level(glsd_ed_core_t *core, int16_t delta, uint8_t with_onoff)
{
    int16_t next;

    if (!core) {
        return GLSD_ED_ERR_ARG;
    }

    next = (int16_t)core->level + delta;
    if (next <= 0 && with_onoff) {
        return glsd_ed_set_level(core, 0, 1);
    }
    if (next < core->min_level) {
        next = core->min_level;
    }
    if (next > core->max_level) {
        next = core->max_level;
    }
    return glsd_ed_set_level(core, (uint8_t)next, with_onoff);
}
