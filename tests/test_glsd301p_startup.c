#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "glsd301p_startup.h"

static unsigned writes;
static bool reject;
static uint8_t flash[GLSD301P_STARTUP_RECORD_SIZE];
static bool save(const uint8_t *r, void *context)
{
    assert(context == &writes); ++writes;
    if (reject) return false;
    memcpy(flash, r, sizeof(flash)); return true;
}
static bool poll(glsd301p_startup_t *s, bool on, uint8_t level, uint32_t t, bool safe)
{
    return glsd301p_startup_poll(s, on, level, t, safe, save, &writes);
}
static void defaults_and_matrix(void)
{
    const uint8_t policies[] = {0u, 1u, 2u, 255u};
    glsd301p_startup_t s, reboot;
    unsigned i, on;
    glsd301p_startup_init(&s, NULL);
    assert(!glsd301p_startup_boot_on(&s));
    assert(s.previous_level == 254u);
    for (i = 0; i < sizeof(policies); ++i) for (on = 0; on < 2u; ++on) {
        glsd301p_startup_init(&s, NULL); s.policy = policies[i];
        assert(!poll(&s, on != 0, 73u, 0u, true));
        assert(poll(&s, on != 0, 73u, 1000u, true));
        glsd301p_startup_init(&reboot, flash);
        assert(reboot.policy == policies[i]);
        assert(reboot.previous_level == 73u);
        assert(glsd301p_startup_boot_on(&reboot) ==
            (policies[i] == 1u || (policies[i] == 2u && !on) ||
             (policies[i] == 255u && on)));
    }
}
static void corruption_and_power_cut(void)
{
    glsd301p_startup_t s, reboot;
    uint8_t good[sizeof(flash)], bad[sizeof(flash)];
    unsigned i, bit;
    glsd301p_startup_init(&s, NULL); s.policy = 255u;
    assert(!poll(&s, true, 100u, 0u, true));
    assert(poll(&s, true, 100u, 1000u, true));
    memcpy(good, flash, sizeof(good));
    for (i = 0; i < sizeof(good); ++i) for (bit = 0; bit < 8u; ++bit) {
        memcpy(bad, good, sizeof(bad)); bad[i] ^= (uint8_t)(1u << bit);
        assert(!glsd301p_startup_record_valid(bad));
        glsd301p_startup_init(&reboot, bad);
        assert(!glsd301p_startup_boot_on(&reboot));
    }
    /* A cut before a stable OFF snapshot restores the committed ON state. */
    assert(!poll(&s, false, 100u, 2000u, true));
    glsd301p_startup_init(&reboot, good);
    assert(glsd301p_startup_boot_on(&reboot));
    /* Failed/torn writes do not advance the application's committed record. */
    reject = true;
    assert(!poll(&s, false, 100u, 3000u, true));
    assert(memcmp(s.committed, good, sizeof(good)) == 0);
    i = writes;
    assert(!poll(&s, false, 100u, 32999u, true)); assert(writes == i);
    reject = false;
    assert(poll(&s, false, 100u, 33000u, true));
    glsd301p_startup_init(&reboot, flash);
    assert(!glsd301p_startup_boot_on(&reboot));
}
static void wear_busy_fault_and_wrap(void)
{
    glsd301p_startup_t s;
    unsigned count;
    glsd301p_startup_init(&s, NULL); s.policy = 1u;
    count = writes;
    assert(!poll(&s, true, 90u, 0u, false)); assert(writes == count);
    assert(!poll(&s, true, 90u, 1000u, true));
    assert(!poll(&s, true, 91u, 1900u, true));
    assert(!poll(&s, true, 91u, 2000u, false));
    assert(!poll(&s, true, 91u, 3000u, true));
    assert(poll(&s, true, 91u, 4000u, true));
    count = writes;
    assert(!poll(&s, true, 91u, 100000u, true)); assert(writes == count);
    /* Faulted OFF is unsafe and cannot overwrite the last intended ON. */
    assert(!poll(&s, false, 91u, 101000u, false)); assert(s.previous_on);
    /* Dim-to-zero OFF keeps the previous valid nonzero level. */
    assert(!poll(&s, false, 0u, 0xfffffff0u, true));
    assert(poll(&s, false, 0u, 984u, true));
    assert(!s.previous_on && s.previous_level == 91u);
    s.policy = 3u; count = writes;
    assert(!poll(&s, true, 91u, 2000u, true)); assert(writes == count);
    assert(!glsd301p_startup_boot_on(&s));
    assert(!poll(&s, true, 255u, 3000u, true));
}
int main(void)
{
    defaults_and_matrix(); corruption_and_power_cut(); wear_busy_fault_and_wrap();
    assert(!glsd301p_startup_record_valid(NULL));
    assert(!glsd301p_startup_boot_on(NULL));
    return 0;
}
