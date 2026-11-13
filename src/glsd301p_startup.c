#include "glsd301p_startup.h"
#include <stddef.h>
#include <string.h>

static uint16_t crc(const uint8_t *p)
{
    uint16_t value = 0xffffu;
    unsigned i, bit;
    for (i = 0u; i < 6u; ++i) {
        value ^= (uint16_t)p[i] << 8;
        for (bit = 0u; bit < 8u; ++bit) {
            value = (uint16_t)((value & 0x8000u) != 0u
                      ? ((uint32_t)value << 1) ^ 0x1021u
                      : (uint32_t)value << 1);
        }
    }
    return value;
}

bool glsd301p_startup_policy_valid(uint8_t p)
{
    return p <= 2u || p == 255u;
}

static void encode(uint8_t *r, uint8_t policy, bool on, uint8_t level)
{
    uint16_t sum;
    r[0] = 0x53u; r[1] = 1u; r[2] = policy;
    r[3] = on ? 1u : 0u; r[4] = level; r[5] = 0u;
    sum = crc(r); r[6] = (uint8_t)sum; r[7] = (uint8_t)(sum >> 8);
}

bool glsd301p_startup_record_valid(const uint8_t *r)
{
    return r != NULL && r[0] == 0x53u && r[1] == 1u &&
           glsd301p_startup_policy_valid(r[2]) && r[3] <= 1u &&
           r[4] >= 2u && r[4] <= 254u && r[5] == 0u &&
           crc(r) == ((uint16_t)r[6] | ((uint16_t)r[7] << 8));
}

void glsd301p_startup_init(glsd301p_startup_t *s, const uint8_t *r)
{
    uint8_t copy[GLSD301P_STARTUP_RECORD_SIZE];
    bool valid;
    if (s == NULL) return;
    valid = glsd301p_startup_record_valid(r);
    if (valid) memcpy(copy, r, sizeof(copy));
    memset(s, 0, sizeof(*s));
    if (valid) {
        memcpy(s->committed, copy, GLSD301P_STARTUP_RECORD_SIZE);
    } else {
        encode(s->committed, 0u, false, 254u);
    }
    s->policy = s->committed[2];
    s->previous_on = s->committed[3] != 0u;
    s->previous_level = s->committed[4];
}

bool glsd301p_startup_boot_on(const glsd301p_startup_t *s)
{
    if (s == NULL || !glsd301p_startup_policy_valid(s->policy)) return false;
    return s->policy == 1u || (s->policy == 2u && !s->previous_on) ||
           (s->policy == 255u && s->previous_on);
}

bool glsd301p_startup_poll(glsd301p_startup_t *s, bool on, uint8_t level,
                          uint32_t now, bool safe,
                          glsd301p_startup_write_t write, void *context)
{
    uint8_t record[GLSD301P_STARTUP_RECORD_SIZE];
    if (s == NULL) return false;
    if (!on && level < 2u) level = s->previous_level;
    if (!safe || write == NULL || level < 2u || level > 254u ||
        !glsd301p_startup_policy_valid(s->policy)) {
        s->pending_valid = false;
        return false;
    }
    encode(record, s->policy, on, level);
    if (memcmp(record, s->committed, sizeof(record)) == 0) {
        s->pending_valid = false;
        return false;
    }
    if (!s->pending_valid || memcmp(record, s->pending, sizeof(record)) != 0) {
        memcpy(s->pending, record, sizeof(record));
        s->changed_ms = now; s->pending_valid = true;
        return false;
    }
    if ((uint32_t)(now - s->changed_ms) < GLSD301P_STARTUP_SETTLE_MS ||
        (s->retry_wait && (uint32_t)(now - s->failed_ms) < GLSD301P_STARTUP_RETRY_MS))
        return false;
    if (!write(record, context)) {
        s->failed_ms = now; s->retry_wait = true;
        return false;
    }
    memcpy(s->committed, record, sizeof(record));
    s->previous_on = on; s->previous_level = level;
    s->pending_valid = false; s->retry_wait = false;
    return true;
}
