#ifndef GLSD301P_STARTUP_H
#define GLSD301P_STARTUP_H
#include <stdbool.h>
#include <stdint.h>

#define GLSD301P_STARTUP_RECORD_SIZE 8u
#define GLSD301P_STARTUP_SETTLE_MS 1000u
#define GLSD301P_STARTUP_RETRY_MS 30000u

/* Standard StartUpOnOff: OFF, ON, toggle previous, previous. */
typedef struct {
    uint8_t policy; /* ZCL attribute storage; validator admits 0,1,2,255. */
    bool previous_on;
    uint8_t previous_level;
    uint8_t committed[GLSD301P_STARTUP_RECORD_SIZE];
    uint8_t pending[GLSD301P_STARTUP_RECORD_SIZE];
    bool pending_valid;
    bool retry_wait;
    uint32_t changed_ms;
    uint32_t failed_ms;
} glsd301p_startup_t;

typedef bool (*glsd301p_startup_write_t)(const uint8_t *record, void *context);
bool glsd301p_startup_policy_valid(uint8_t policy);
bool glsd301p_startup_record_valid(const uint8_t *record);
/* NULL/invalid record migrates to OFF/full brightness; never wipes stack NV. */
void glsd301p_startup_init(glsd301p_startup_t *state, const uint8_t *record);
bool glsd301p_startup_boot_on(const glsd301p_startup_t *state);
/* Called from the target's owned maintenance event, never IRQ context.
 * Only stable, guarded, DMA-idle output is eligible for persistence.
 * No writes while unsafe or unchanged. Failures retry after 30 seconds.
 * A cut before the settled write restores the last committed snapshot. */
bool glsd301p_startup_poll(glsd301p_startup_t *state, bool on, uint8_t level,
                          uint32_t now_ms, bool safe,
                          glsd301p_startup_write_t write, void *context);
#endif
