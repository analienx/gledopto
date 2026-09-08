#ifndef GLSD301P_PUSH_INPUT_H
#define GLSD301P_PUSH_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The stock GL-SD-301P polls PC2 every 1 ms. PC2 is treated as a pulse train,
 * not as a simple steady-state pushbutton level. This decoder models only the
 * externally relevant behavior recovered for interoperability.
 */

typedef enum {
    GLSD301P_PUSH_EVENT_NONE = 0,
    GLSD301P_PUSH_EVENT_TOGGLE,
    GLSD301P_PUSH_EVENT_LEVEL_STEP,
} glsd301p_push_event_t;

typedef enum {
    GLSD301P_PUSH_DIM_DOWN = 0,
    GLSD301P_PUSH_DIM_UP = 1,
} glsd301p_push_dim_direction_t;

typedef struct {
    uint8_t state;
    uint8_t low_run_ticks;
    uint8_t high_run_ticks;
    uint16_t active_low_ticks;
    glsd301p_push_dim_direction_t direction;
} glsd301p_push_decoder_t;

void glsd301p_push_decoder_init(glsd301p_push_decoder_t *decoder);

/*
 * Feed one PC2 sample per millisecond.
 *
 * pc2_high=true means the sampled GPIO is high. The active waveform contains
 * recurring low periods separated by bounded high gaps. A completed short
 * activation emits TOGGLE. A sustained valid waveform emits LEVEL_STEP and
 * repeats LEVEL_STEP while held. Releasing after a long activation reverses
 * the dim direction for the next long activation.
 */
glsd301p_push_event_t glsd301p_push_decoder_poll(glsd301p_push_decoder_t *decoder,
                                                  bool pc2_high);

glsd301p_push_dim_direction_t
glsd301p_push_decoder_direction(const glsd301p_push_decoder_t *decoder);

/* Stock local-hold level policy. These helpers operate only on the logical ZCL
 * level; transport framing remains in glsd301p_uart_frame.*. */
uint8_t glsd301p_push_level_step(uint8_t current_level,
                                 glsd301p_push_dim_direction_t direction);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_PUSH_INPUT_H */
