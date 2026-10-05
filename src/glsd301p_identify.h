#ifndef GLSD301P_IDENTIFY_H
#define GLSD301P_IDENTIFY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * R17/R18/R19 Identify adapter: RAM-only IdentifyTime countdown shared by
 * the target and the hosted fixture. SDK-independent (no ZCL types): the
 * target and the harness bind the same ZCL IdentifyTime attribute store
 * and call the same entry points, so the countdown the tests observe is
 * the countdown the firmware runs.
 *
 * OUTPUT AUTHORITY: Identify is commissioning state only. The Identify
 * command and IdentifyTime writes arm/restart a RAM countdown; the SDK's
 * Query handler and Add-Group-If-Identifying read that same store, so no
 * app frame code exists. Trigger Effect is unsupported in this scope:
 * every effect id/variant is rejected before any state change, and no
 * effect program, saved-output restore, or preemption path exists. This
 * device has no separate identification output, and modulating the mains
 * load to identify is explicitly out of scope, so there is no visible
 * identification behavior; commissioners observe IdentifyTime/Query only.
 *
 * Countdown: the household 1 ms IO tick calls glsd301p_identify_tick().
 * Accepted IdentifyTime writes are reported by the SDK write path
 * through glsd301p_sdk_write_observer() (P5 patch call sites after a
 * successful zcl_attrWrite), so their receipt time and value — including
 * same-value writes — restart the phase exactly like the Identify
 * command. Rejected records never notify. The tick itself does O(1)
 * elapsed whole-second arithmetic with residual phase and saturation,
 * and does no catch-up work for a disabled countdown.
 */

/* ZCL Identify cluster/command/attribute/type codes (ZCL r6 §3.5). The
 * hosted harness statically asserts these against the pinned SDK. */
#define GLSD301P_ZCL_CLUSTER_IDENTIFY 0x0003u
#define GLSD301P_ZCL_ATTR_IDENTIFY_TIME 0x0000u
#define GLSD301P_ZCL_TYPE_UINT16 0x21u
#define GLSD301P_ZCL_CMD_IDENTIFY 0x00u
#define GLSD301P_ZCL_CMD_TRIGGER_EFFECT 0x40u

typedef struct {
    uint16_t countdown;
    uint32_t second_mark_ms;
    bool second_mark_valid;
    /*
     * R19 diagnostic: maximum per-tick catch-up steps executed by
     * glsd301p_identify_tick(). RAM-only, never affects behavior; the
     * O(1) path executes no catch-up steps, so this stays 0 and trips
     * only if a per-second loop is ever reintroduced.
     */
    uint32_t tick_steps_max;
} glsd301p_identify_t;

typedef enum {
    GLSD301P_IDENTIFY_CMD_OK = 0,
    GLSD301P_IDENTIFY_CMD_INVALID = 1
} glsd301p_identify_cmd_result_t;

void glsd301p_identify_init(glsd301p_identify_t *st);

/*
 * Identify command receipt: the store and countdown take the new value
 * (0 stops); the second phase restarts at now_ms.
 */
void glsd301p_identify_on_identify(glsd301p_identify_t *st, uint16_t seconds,
                                   uint16_t *store, uint32_t now_ms);

/*
 * Accepted IdentifyTime write receipt (via the observer below): same
 * restart semantics as the command, including same-value writes.
 */
void glsd301p_identify_on_accepted_write(glsd301p_identify_t *st,
                                         uint16_t seconds, uint16_t *store,
                                         uint32_t now_ms);

/*
 * Household-tick countdown processing: O(1) whole-second progress with
 * residual phase, saturating at zero. A disabled countdown costs
 * nothing. Wrap-safe; never touches output, timers, or the transport.
 */
void glsd301p_identify_tick(glsd301p_identify_t *st, uint16_t *store,
                            uint32_t now_ms);

/*
 * Shared Identify cluster-command adapter. The target callback and the
 * hosted fixture call this with the SDK-decoded fields, so the tested
 * decision/state path IS the production path. Validates the endpoint,
 * applies Identify via on_identify, and rejects every Trigger Effect
 * before any state change. Query and any other command need no app
 * action (the SDK answers Query from the bound store). The caller maps
 * OK/INVALID onto the ZCL SUCCESS/INVALID_FIELD statuses.
 */
glsd301p_identify_cmd_result_t glsd301p_identify_cluster_command(
    glsd301p_identify_t *st, uint16_t *store, uint8_t dst_ep,
    uint8_t app_ep, uint8_t cmd_id, uint16_t identify_time,
    uint8_t effect_id, uint8_t effect_variant, uint32_t now_ms);

/*
 * Bind the single production write observer (target and harness each
 * bind their own state once at init). The SDK write path calls
 * glsd301p_sdk_write_observer() after every successful attribute
 * store update; the observer restarts the countdown only for accepted
 * IdentifyTime writes on the bound endpoint, at receipt time.
 */
void glsd301p_identify_write_observer_bind(glsd301p_identify_t *st,
                                           uint16_t *store, uint8_t endpoint);
void glsd301p_sdk_write_observer(uint8_t endpoint, uint16_t cluster_id,
                                 uint16_t attr_id, uint8_t data_type,
                                 const uint8_t *attr_data);

#ifdef __cplusplus
}
#endif

#endif
