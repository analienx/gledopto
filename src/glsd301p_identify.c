#include "glsd301p_identify.h"

#include <stddef.h>

#include "glsd301p_timebase.h"

/* Single production write-observer binding (target or harness). */
static glsd301p_identify_t *s_obs_st;
static uint16_t *s_obs_store;
static uint8_t s_obs_ep;
static bool s_obs_bound;

void glsd301p_identify_init(glsd301p_identify_t *st)
{
    if (st == NULL) {
        return;
    }
    st->countdown = 0u;
    st->second_mark_ms = 0u;
    st->second_mark_valid = false;
}

static void glsd301p_identify_restart(glsd301p_identify_t *st,
                                      uint16_t seconds, uint16_t *store,
                                      uint32_t now_ms)
{
    st->countdown = seconds;
    st->second_mark_ms = now_ms;
    st->second_mark_valid = true;
    if (store != NULL) {
        *store = seconds;
    }
}

void glsd301p_identify_on_identify(glsd301p_identify_t *st, uint16_t seconds,
                                   uint16_t *store, uint32_t now_ms)
{
    if (st == NULL) {
        return;
    }
    glsd301p_identify_restart(st, seconds, store, now_ms);
}

void glsd301p_identify_on_accepted_write(glsd301p_identify_t *st,
                                         uint16_t seconds, uint16_t *store,
                                         uint32_t now_ms)
{
    if (st == NULL) {
        return;
    }
    glsd301p_identify_restart(st, seconds, store, now_ms);
}

void glsd301p_identify_tick(glsd301p_identify_t *st, uint16_t *store,
                            uint32_t now_ms)
{
    uint32_t age;
    uint32_t whole;

    if (st == NULL || store == NULL) {
        return;
    }
    if (!st->second_mark_valid) {
        st->second_mark_ms = now_ms;
        st->second_mark_valid = true;
        return;
    }
    /* Disabled countdown: no catch-up work at all. */
    if (st->countdown == 0u) {
        return;
    }
    age = glsd301p_timebase_age_ms(st->second_mark_ms, now_ms);
    if (age < 1000u) {
        return;
    }
    /*
     * O(1): whole elapsed seconds advance the phase, keeping the
     * residual sub-second phase for the next boundary. whole <=
     * (2^32-1)/1000, so whole*1000 never overflows u32; the decrement
     * saturates at zero instead of underflowing.
     */
    whole = age / 1000u;
    st->second_mark_ms += whole * 1000u;
    if (whole >= st->countdown) {
        st->countdown = 0u;
    } else {
        st->countdown -= (uint16_t)whole;
    }
    *store = st->countdown;
}

glsd301p_identify_cmd_result_t glsd301p_identify_cluster_command(
    glsd301p_identify_t *st, uint16_t *store, uint8_t dst_ep,
    uint8_t app_ep, uint8_t cmd_id, uint16_t identify_time,
    uint8_t effect_id, uint8_t effect_variant, uint32_t now_ms)
{
    (void)effect_id;
    (void)effect_variant;

    if (st == NULL || store == NULL) {
        return GLSD301P_IDENTIFY_CMD_INVALID;
    }
    if (dst_ep != app_ep) {
        return GLSD301P_IDENTIFY_CMD_INVALID;
    }
    if (cmd_id == GLSD301P_ZCL_CMD_IDENTIFY) {
        glsd301p_identify_on_identify(st, identify_time, store, now_ms);
        return GLSD301P_IDENTIFY_CMD_OK;
    }
    if (cmd_id == GLSD301P_ZCL_CMD_TRIGGER_EFFECT) {
        /*
         * R17: every Trigger Effect is unsupported in this scope.
         * Truthful rejection before any state change; no program, no
         * restore, no preemption, no output effect of any kind.
         */
        return GLSD301P_IDENTIFY_CMD_INVALID;
    }
    /* Query and anything else: no app action (SDK answers Query). */
    return GLSD301P_IDENTIFY_CMD_OK;
}

void glsd301p_identify_write_observer_bind(glsd301p_identify_t *st,
                                           uint16_t *store, uint8_t endpoint)
{
    s_obs_st = st;
    s_obs_store = store;
    s_obs_ep = endpoint;
    s_obs_bound = (st != NULL && store != NULL);
}

void glsd301p_sdk_write_observer(uint8_t endpoint, uint16_t cluster_id,
                                 uint16_t attr_id, uint8_t data_type,
                                 const uint8_t *attr_data)
{
    uint16_t seconds;

    if (!s_obs_bound || s_obs_st == NULL || s_obs_store == NULL) {
        return;
    }
    /* The SDK reports only successful store updates; still filter to
     * the bound IdentifyTime exactly (endpoint/cluster/attr/type). */
    if (endpoint != s_obs_ep) {
        return;
    }
    if (cluster_id != GLSD301P_ZCL_CLUSTER_IDENTIFY ||
        attr_id != GLSD301P_ZCL_ATTR_IDENTIFY_TIME) {
        return;
    }
    if (data_type != GLSD301P_ZCL_TYPE_UINT16 || attr_data == NULL) {
        return;
    }
    seconds = (uint16_t)attr_data[0] | ((uint16_t)attr_data[1] << 8);
    glsd301p_identify_on_accepted_write(s_obs_st, seconds, s_obs_store,
                                        glsd301p_timebase_now_ms());
}
