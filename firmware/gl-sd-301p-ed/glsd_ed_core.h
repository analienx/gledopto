#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *user;
    int (*apply_output)(void *user, uint8_t on, uint8_t level);
} glsd_ed_hw_t;

typedef struct {
    glsd_ed_hw_t hw;
    uint8_t on;
    uint8_t level;
    uint8_t last_nonzero_level;
    uint8_t min_level;
    uint8_t max_level;
} glsd_ed_core_t;

typedef enum {
    GLSD_ED_OK = 0,
    GLSD_ED_ERR_ARG = -1,
    GLSD_ED_ERR_HW = -2,
} glsd_ed_status_t;

int glsd_ed_core_init(
    glsd_ed_core_t *core,
    const glsd_ed_hw_t *hw,
    uint8_t initial_on,
    uint8_t initial_level,
    uint8_t min_level,
    uint8_t max_level
);

int glsd_ed_set_on(glsd_ed_core_t *core, uint8_t on);
int glsd_ed_toggle(glsd_ed_core_t *core);
int glsd_ed_set_level(glsd_ed_core_t *core, uint8_t level, uint8_t with_onoff);
int glsd_ed_step_level(glsd_ed_core_t *core, int16_t delta, uint8_t with_onoff);

#ifdef __cplusplus
}
#endif
