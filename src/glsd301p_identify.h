#ifndef GLSD301P_IDENTIFY_H
#define GLSD301P_IDENTIFY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R13 Identify adapter: shared IdentifyTime countdown plus the bounded
 * Trigger Effect programs. SDK-independent (no ZCL types): the target and
 * the hosted fixture bind the same ZCL IdentifyTime attribute store and
 * call the same entry points, so the countdown the tests observe is the
 * countdown the firmware runs.
 *
 * Countdown: the household 1 ms IO tick calls glsd301p_identify_tick().
 * Each whole-second boundary either adopts an externally written store
 * value (the SDK applies attribute writes with no app notification, so
 * adoption is by shadow comparison), charging the elapsed phase as one
 * second, or decrements the running countdown back into the store. The
 * SDK's Query handler reads that same store, so
 * Query answers stay honest with no app frame code. Delayed ticks chase
 * whole boundaries (no drift, no skip); a NULL store disables the tick.
 *
 * Effects: only Blink (0x00) and Breathe (0x01) with variant 0x00 are
 * supported; anything else is rejected by the caller with INVALID_FIELD
 * before any state change. A started effect cancels any running Level
 * transition, renders through the guarded emit path on the owned Level
 * timer, then restores the pre-effect output. Any remote OnOff/Level
 * command, local push takeover, or fault aborts the effect; the aborting
 * cause always emits, so no stale override survives (the emit-less Stop
 * path restores explicitly).
 */

#define GLSD301P_IDENTIFY_EFFECT_BLINK 0x00u
#define GLSD301P_IDENTIFY_EFFECT_BREATHE 0x01u
#define GLSD301P_IDENTIFY_EFFECT_VARIANT_DEFAULT 0x00u

#define GLSD301P_IDENTIFY_BLINK_MS 1500u
#define GLSD301P_IDENTIFY_BLINK_PHASE_MS 250u
#define GLSD301P_IDENTIFY_BREATHE_MS 2000u

typedef struct {
    uint16_t countdown;
    uint16_t shadow;
    uint32_t second_mark_ms;
    bool second_mark_valid;
    bool effect_active;
    uint8_t effect_id;
    uint32_t effect_start_ms;
    bool effect_saved_onoff;
    uint8_t effect_saved_level;
} glsd301p_identify_t;

void glsd301p_identify_init(glsd301p_identify_t *st);

/*
 * Identify command: the store, shadow, and countdown all take the new
 * value (0 stops); the second phase restarts at now_ms.
 */
void glsd301p_identify_on_identify(glsd301p_identify_t *st, uint16_t seconds,
                                   uint16_t *store, uint32_t now_ms);

/* Household-tick countdown processing described above. */
void glsd301p_identify_tick(glsd301p_identify_t *st, uint16_t *store,
                            uint32_t now_ms);

bool glsd301p_identify_effect_supported(uint8_t effect_id,
                                        uint8_t effect_variant);

/*
 * Begin an effect program with an explicit restore point. The caller
 * passes the live output for a fresh start, or the kept saved values
 * for a re-trigger, so re-triggering mid-effect restores the original
 * output instead of a blink phase.
 */
void glsd301p_identify_effect_begin(glsd301p_identify_t *st, uint8_t effect_id,
                                    uint32_t now_ms, bool save_onoff,
                                    uint8_t save_level);

bool glsd301p_identify_effect_active(const glsd301p_identify_t *st);

/*
 * Compute the current override. Returns true while the program runs
 * (override outputs valid); false once elapsed (caller restores the
 * saved output and clears). Pure: advances no state.
 */
bool glsd301p_identify_effect_step(const glsd301p_identify_t *st,
                                   uint32_t now_ms, uint8_t min_level,
                                   uint8_t max_level, bool *out_onoff,
                                   uint8_t *out_level);

void glsd301p_identify_effect_clear(glsd301p_identify_t *st);

bool glsd301p_identify_effect_saved_onoff(const glsd301p_identify_t *st);
uint8_t glsd301p_identify_effect_saved_level(const glsd301p_identify_t *st);

#ifdef __cplusplus
}
#endif

#endif
