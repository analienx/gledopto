#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../glsd_ed_core.h"

typedef struct {
    uint8_t on;
    uint8_t level;
    unsigned calls;
    int fail;
} fake_hw_t;

static int fake_apply(void *user, uint8_t on, uint8_t level)
{
    fake_hw_t *hw = (fake_hw_t *)user;
    hw->calls++;
    if (hw->fail) {
        return -1;
    }
    hw->on = on;
    hw->level = level;
    return 0;
}

static glsd_ed_core_t make_core(fake_hw_t *fake, uint8_t on, uint8_t level)
{
    glsd_ed_core_t core;
    glsd_ed_hw_t hw = {fake, fake_apply};
    assert(glsd_ed_core_init(&core, &hw, on, level, 1, 254) == GLSD_ED_OK);
    return core;
}

static void test_onoff_preserves_level(void)
{
    fake_hw_t fake = {0};
    glsd_ed_core_t core = make_core(&fake, 1, 80);

    assert(glsd_ed_set_on(&core, 0) == GLSD_ED_OK);
    assert(core.on == 0 && core.level == 80);
    assert(fake.on == 0 && fake.level == 80);

    assert(glsd_ed_set_on(&core, 1) == GLSD_ED_OK);
    assert(core.on == 1 && core.level == 80);
    assert(fake.on == 1 && fake.level == 80);
}

static void test_level_and_with_onoff(void)
{
    fake_hw_t fake = {0};
    glsd_ed_core_t core = make_core(&fake, 0, 100);

    assert(glsd_ed_set_level(&core, 200, 0) == GLSD_ED_OK);
    assert(core.on == 0 && core.level == 200);

    assert(glsd_ed_set_level(&core, 150, 1) == GLSD_ED_OK);
    assert(core.on == 1 && core.level == 150);

    assert(glsd_ed_set_level(&core, 0, 1) == GLSD_ED_OK);
    assert(core.on == 0);
    assert(core.level == 150);
    assert(core.last_nonzero_level == 150);
}

static void test_clamp_step_and_toggle(void)
{
    fake_hw_t fake = {0};
    glsd_ed_core_t core = make_core(&fake, 1, 200);

    assert(glsd_ed_step_level(&core, 100, 0) == GLSD_ED_OK);
    assert(core.level == 254);

    assert(glsd_ed_step_level(&core, -400, 0) == GLSD_ED_OK);
    assert(core.level == 1 && core.on == 1);

    assert(glsd_ed_step_level(&core, -1, 1) == GLSD_ED_OK);
    assert(core.on == 0 && core.level == 1);

    assert(glsd_ed_toggle(&core) == GLSD_ED_OK);
    assert(core.on == 1 && core.level == 1);
}

static void test_hardware_failure_rolls_back(void)
{
    fake_hw_t fake = {0};
    glsd_ed_core_t core = make_core(&fake, 1, 90);

    fake.fail = 1;
    assert(glsd_ed_set_level(&core, 120, 1) == GLSD_ED_ERR_HW);
    assert(core.on == 1 && core.level == 90 && core.last_nonzero_level == 90);

    assert(glsd_ed_set_on(&core, 0) == GLSD_ED_ERR_HW);
    assert(core.on == 1 && core.level == 90);
}

int main(void)
{
    test_onoff_preserves_level();
    test_level_and_with_onoff();
    test_clamp_step_and_toggle();
    test_hardware_failure_rolls_back();
    puts("glsd_ed_core: PASS");
    return 0;
}
