#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zcl_include.h"
#include "ev_timer.h"
#include "ev.h"

#include "glsd301p_control.h"
#include "glsd301p_hw_io.h"
#include "glsd301p_runtime_core.h"
#include "glsd301p_timebase.h"
#include "glsd301p_timer_events.h"
#include "glsd301p_uart_service.h"
#include "glsd301p_uart_transport.h"
#include "glsd301p_zcl_commands.h"

#include "hw_stub.h"

#define DISPATCH_EP 0x0Bu
#define DISPATCH_MIN_LEVEL 0x02u
#define DISPATCH_MAX_LEVEL 0xFEu

static const uint8_t ON_FE[] = {0xA5, 0x5A, 0x01, 0xFE, 0x04, 0xAA};

/* ---- scripted ZCL stack seams (the only SDK surface stubbed) ---- */

#define ATTR_SLOTS 12u

typedef struct {
    u16 cluster;
    u16 attr;
    u8 len;
    u8 val[8];
} attr_slot_t;

static attr_slot_t g_attrs[ATTR_SLOTS];
static unsigned g_attr_n;
static unsigned g_send_cmd_calls;
static unsigned g_level_cb_calls;
static unsigned g_onoff_cb_calls;
static cluster_cmdHdlr_t g_level_hdlr;
static cluster_cmdHdlr_t g_onoff_hdlr;
static cluster_forAppCb_t g_level_app_cb;
static cluster_forAppCb_t g_onoff_app_cb;

static void attr_store_reset(void)
{
    memset(g_attrs, 0, sizeof(g_attrs));
    g_attr_n = 0u;
}

static void attr_set(u16 cluster, u16 attr, const u8 *val, u8 len)
{
    unsigned i;

    assert(len <= (u8)sizeof(g_attrs[0].val));
    for (i = 0u; i < g_attr_n; i++) {
        if (g_attrs[i].cluster == cluster && g_attrs[i].attr == attr) {
            g_attrs[i].len = len;
            memcpy(g_attrs[i].val, val, len);
            return;
        }
    }
    assert(g_attr_n < ATTR_SLOTS);
    g_attrs[g_attr_n].cluster = cluster;
    g_attrs[g_attr_n].attr = attr;
    g_attrs[g_attr_n].len = len;
    memcpy(g_attrs[g_attr_n].val, val, len);
    g_attr_n++;
}

static void attr_set_u8(u16 cluster, u16 attr, u8 val)
{
    attr_set(cluster, attr, &val, 1u);
}

static void attr_set_u16(u16 cluster, u16 attr, u16 val)
{
    u8 raw[2];

    raw[0] = (u8)(val & 0xFFu);
    raw[1] = (u8)((val >> 8) & 0xFFu);
    attr_set(cluster, attr, raw, 2u);
}

status_t zcl_registerCluster(u8 endpoint, u16 clusterId, u16 manuCode,
                             u8 attrNum, const zclAttrInfo_t *pAttrTbl,
                             cluster_cmdHdlr_t cmdHdlrFn,
                             cluster_forAppCb_t cb)
{
    (void)endpoint;
    (void)manuCode;
    (void)attrNum;
    (void)pAttrTbl;
    if (clusterId == ZCL_CLUSTER_GEN_LEVEL_CONTROL) {
        g_level_hdlr = cmdHdlrFn;
        g_level_app_cb = cb;
    } else if (clusterId == ZCL_CLUSTER_GEN_ON_OFF) {
        g_onoff_hdlr = cmdHdlrFn;
        g_onoff_app_cb = cb;
    } else {
        return ZCL_STA_FAILURE;
    }
    return ZCL_STA_SUCCESS;
}

status_t zcl_getAttrVal(u8 endpoint, u16 clusterId, u16 attrId, u16 *len,
                        u8 *val)
{
    unsigned i;

    if (endpoint != DISPATCH_EP || len == NULL || val == NULL) {
        return ZCL_STA_FAILURE;
    }
    for (i = 0u; i < g_attr_n; i++) {
        if (g_attrs[i].cluster == clusterId && g_attrs[i].attr == attrId) {
            memcpy(val, g_attrs[i].val, g_attrs[i].len);
            *len = g_attrs[i].len;
            return ZCL_STA_SUCCESS;
        }
    }
    return ZCL_STA_FAILURE;
}

status_t zcl_setAttrVal(u8 endpoint, u16 clusterId, u16 attrId, u8 *val)
{
    unsigned i;

    if (endpoint != DISPATCH_EP || val == NULL) {
        return ZCL_STA_FAILURE;
    }
    for (i = 0u; i < g_attr_n; i++) {
        if (g_attrs[i].cluster == clusterId && g_attrs[i].attr == attrId) {
            memcpy(g_attrs[i].val, val, g_attrs[i].len);
            return ZCL_STA_SUCCESS;
        }
    }
    return ZCL_STA_FAILURE;
}

status_t zcl_sendCmd(u8 srcEp, epInfo_t *pDstEpInfo, u16 clusterId, u8 cmd,
                     u8 specific, u8 direction, u8 disableDefaultRsp,
                     u16 manuCode, u8 seqNo, u16 cmdPldLen, u8 *cmdPld)
{
    (void)srcEp;
    (void)pDstEpInfo;
    (void)clusterId;
    (void)cmd;
    (void)specific;
    (void)direction;
    (void)disableDefaultRsp;
    (void)manuCode;
    (void)seqNo;
    (void)cmdPldLen;
    (void)cmdPld;
    g_send_cmd_calls++;
    return ZCL_STA_SUCCESS;
}

/* ---- shared command policy under test, driven through real dispatch ---- */

static glsd301p_runtime_core_t g_runtime;
static glsd301p_uart_transport_t g_transport;
static glsd301p_uart_service_t g_uart;
static glsd301p_level_state_t g_level;
static glsd301p_control_ctx_t g_ctx;
static uint8_t g_onoff;
static uint16_t g_on_time;
static uint16_t g_off_wait_time;
static glsd301p_zcl_ctx_t g_zctx;

static status_t level_thunk(zclIncomingAddrInfo_t *addr, u8 cmd_id,
                            void *payload)
{
    g_level_cb_calls++;
    if (addr == NULL) {
        return ZCL_STA_INVALID_FIELD;
    }
    return glsd301p_zcl_level_command(&g_zctx, addr->dstEp, cmd_id, payload);
}

static status_t onoff_thunk(zclIncomingAddrInfo_t *addr, u8 cmd_id,
                            void *payload)
{
    g_onoff_cb_calls++;
    if (addr == NULL) {
        return ZCL_STA_INVALID_FIELD;
    }
    return glsd301p_zcl_onoff_command(&g_zctx, addr->dstEp, cmd_id, payload);
}

static void fixture_init(void)
{
    host_stub_reset();
    host_clock_set(0u);
    ev_timer_init();
    ev_timer_setPrevSysTick(0u);
    glsd301p_timebase_init();
    glsd301p_timer_events_init();
    glsd301p_runtime_core_init(&g_runtime);
    glsd301p_uart_transport_init(&g_transport);
    glsd301p_uart_service_init(&g_uart);
    glsd301p_control_init(&g_ctx, &g_runtime, &g_transport, &g_uart,
                          &g_level, &g_onoff,
                          DISPATCH_MIN_LEVEL, DISPATCH_MAX_LEVEL,
                          DISPATCH_MAX_LEVEL, DISPATCH_MIN_LEVEL);
    g_on_time = 0u;
    g_off_wait_time = 0u;
    g_zctx.endpoint = DISPATCH_EP;
    g_zctx.min_level = DISPATCH_MIN_LEVEL;
    g_zctx.runtime = &g_runtime;
    g_zctx.control = &g_ctx;
    g_zctx.level = &g_level;
    g_zctx.on_time = &g_on_time;
    g_zctx.off_wait_time = &g_off_wait_time;
    host_gpio_set(true, false);

    attr_store_reset();
    attr_set_u8(ZCL_CLUSTER_GEN_ON_OFF, ZCL_ATTRID_ONOFF, 0u);
    attr_set_u16(ZCL_CLUSTER_GEN_ON_OFF, ZCL_ATTRID_ON_TIME, 0u);
    attr_set_u16(ZCL_CLUSTER_GEN_ON_OFF, ZCL_ATTRID_OFF_WAIT_TIME, 0u);
    attr_set_u8(ZCL_CLUSTER_GEN_ON_OFF, ZCL_ATTRID_START_UP_ONOFF,
                ZCL_START_UP_ONOFF_SET_ONOFF_TO_OFF);
    attr_set_u8(ZCL_CLUSTER_GEN_LEVEL_CONTROL,
                ZCL_ATTRID_LEVEL_CURRENT_LEVEL, DISPATCH_MAX_LEVEL);
    attr_set_u8(ZCL_CLUSTER_GEN_LEVEL_CONTROL, ZCL_ATTRID_LEVEL_OPTIONS, 0u);
    attr_set_u8(ZCL_CLUSTER_GEN_LEVEL_CONTROL,
                ZCL_ATTRID_LEVEL_START_UP_CURRENT_LEVEL,
                ZCL_START_UP_CURRENT_LEVEL_TO_PREVIOUS);

    g_send_cmd_calls = 0u;
    g_level_cb_calls = 0u;
    g_onoff_cb_calls = 0u;
    g_level_hdlr = NULL;
    g_onoff_hdlr = NULL;
    g_level_app_cb = NULL;
    g_onoff_app_cb = NULL;
}

static void pump_ms(uint32_t ms)
{
    uint32_t i;

    for (i = 0u; i < ms; i++) {
        host_clock_advance(HOST_TICKS_PER_MS);
        ev_timer_process();
    }
}

/* Same boot observation as the target: OFF completes, output arms. */
static void boot_ready(void)
{
    assert(glsd301p_timer_io_start(glsd301p_control_io_cb, &g_ctx));
    assert(glsd301p_control_boot_off(&g_ctx, glsd301p_timebase_now_ms()));
    host_uart_set_busy(true);
    pump_ms(6u);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
    host_uart_set_busy(false);
    pump_ms(1u);
    assert(glsd301p_runtime_core_is_ready(&g_runtime));
}

static void register_clusters(void)
{
    assert(zcl_level_register(DISPATCH_EP, 0u, 0u, NULL, level_thunk) ==
           ZCL_STA_SUCCESS);
    assert(zcl_onOff_register(DISPATCH_EP, 0u, 0u, NULL, onoff_thunk) ==
           ZCL_STA_SUCCESS);
    assert(g_level_hdlr != NULL);
    assert(g_onoff_hdlr != NULL);
    assert(g_level_app_cb != NULL);
    assert(g_onoff_app_cb != NULL);
}

static apsdeDataInd_t g_msg;
static zclIncoming_t g_in;

/*
 * Drive one raw frame through the real captured cluster dispatcher. The
 * payload lives in an exact-size heap buffer so any parser overread trips
 * ASan; a zero length passes NULL so any dereference faults immediately.
 */
static status_t dispatch_frame(cluster_cmdHdlr_t hdlr,
                               cluster_forAppCb_t app_cb, u8 cmd,
                               const u8 *payload, u16 len, u8 ep, u8 dir)
{
    u8 *heap = NULL;
    status_t status;

    assert(hdlr != NULL);
    assert(app_cb != NULL);
    if (len > 0u) {
        heap = (u8 *)malloc(len);
        assert(heap != NULL);
        memcpy(heap, payload, len);
    }
    memset(&g_msg, 0, sizeof(g_msg));
    memset(&g_in, 0, sizeof(g_in));
    g_msg.indInfo.dst_ep = ep;
    g_in.msg = &g_msg;
    g_in.pData = heap;
    g_in.dataLen = len;
    g_in.hdr.cmd = cmd;
    g_in.hdr.frmCtrl.bf.dir = dir;
    g_in.addrInfo.dstEp = ep;
    g_in.addrInfo.srcEp = 1u;
    g_in.clusterAppCb = app_cb;
    status = hdlr(&g_in);
    free(heap);
    return status;
}

static status_t dispatch_level(u8 cmd, const u8 *payload, u16 len)
{
    return dispatch_frame(g_level_hdlr, g_level_app_cb, cmd, payload, len,
                          DISPATCH_EP, ZCL_FRAME_CLIENT_SERVER_DIR);
}

static status_t dispatch_onoff(u8 cmd, const u8 *payload, u16 len)
{
    return dispatch_frame(g_onoff_hdlr, g_onoff_app_cb, cmd, payload, len,
                          DISPATCH_EP, ZCL_FRAME_CLIENT_SERVER_DIR);
}

/* ---- Level cluster ---- */

static void test_level_move_with_onoff_ready(void)
{
    const u8 pld[] = {0x40u, 0x0Au, 0x00u};

    fixture_init();
    boot_ready();
    register_clusters();
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF, pld,
                          (u16)sizeof(pld)) == ZCL_STA_SUCCESS);
    assert(g_level_cb_calls == 1u);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x40u);
    assert(g_level.with_onoff == 1u);
    assert(g_send_cmd_calls == 0u);
}

static void test_level_plain_move_gated_while_off(void)
{
    const u8 pld[] = {0x40u, 0x0Au, 0x00u};

    fixture_init();
    boot_ready();
    register_clusters();
    /* OFF + default options: the SDK execute gate blocks the callback. */
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL, pld,
                          (u16)sizeof(pld)) == ZCL_STA_SUCCESS);
    assert(g_level_cb_calls == 0u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);

    /* Same frame with the light on executes. */
    attr_set_u8(ZCL_CLUSTER_GEN_ON_OFF, ZCL_ATTRID_ONOFF, 1u);
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL, pld,
                          (u16)sizeof(pld)) == ZCL_STA_SUCCESS);
    assert(g_level_cb_calls == 1u);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.with_onoff == 0u);
}

static void test_level_short_frames_rejected(void)
{
    const u8 move2[] = {0x40u, 0x0Au};
    const u8 move1[] = {0x00u};
    const u8 step3[] = {0x00u, 0x05u, 0x0Au};

    fixture_init();
    boot_ready();
    register_clusters();
    /* P2a: mandatory-length gates, no callback, no overread (ASan heap). */
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF, move2,
                          (u16)sizeof(move2)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF, move1,
                          (u16)sizeof(move1)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_level(ZCL_CMD_LEVEL_STEP_WITH_ON_OFF, step3,
                          (u16)sizeof(step3)) == ZCL_STA_MALFORMED_COMMAND);
    assert(g_level_cb_calls == 0u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
}

static void test_level_step_move_stop(void)
{
    const u8 step_up[] = {0x00u, 0x05u, 0x0Au, 0x00u};
    const u8 step_down[] = {0x01u, 0x0Au, 0x0Au, 0x00u};
    const u8 move[] = {0x00u, 0x10u};
    const u8 stop_opts[] = {0x01u, 0x01u};

    fixture_init();
    boot_ready();
    register_clusters();
    attr_set_u8(ZCL_CLUSTER_GEN_ON_OFF, ZCL_ATTRID_ONOFF, 1u);

    assert(dispatch_level(ZCL_CMD_LEVEL_STEP, step_up,
                          (u16)sizeof(step_up)) == ZCL_STA_SUCCESS);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    /* 0xFE + 5 saturates at max through the guarded clamp. */
    assert(g_level.target == DISPATCH_MAX_LEVEL);

    g_level.current_level = 0x05u;
    assert(dispatch_level(ZCL_CMD_LEVEL_STEP, step_down,
                          (u16)sizeof(step_down)) == ZCL_STA_SUCCESS);
    /* 0x05 - 0x0A floors at min, never wraps. */
    assert(g_level.target == DISPATCH_MIN_LEVEL);

    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE, move,
                          (u16)sizeof(move)) == ZCL_STA_SUCCESS);
    assert(g_level.mode == GLSD301P_LEVEL_MOVE);
    assert(g_level.direction_up == 1u);
    assert(g_level.rate == 0x10u);

    /* Stop with zero payload (NULL pData) cancels; options form also works. */
    assert(dispatch_level(ZCL_CMD_LEVEL_STOP, NULL, 0u) == ZCL_STA_SUCCESS);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE, move,
                          (u16)sizeof(move)) == ZCL_STA_SUCCESS);
    assert(dispatch_level(ZCL_CMD_LEVEL_STOP_WITH_ON_OFF, stop_opts,
                          (u16)sizeof(stop_opts)) == ZCL_STA_SUCCESS);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
    assert(g_send_cmd_calls == 0u);
}

static void test_level_unknown_and_misdirected(void)
{
    const u8 pld[] = {0x40u, 0x0Au, 0x00u};

    fixture_init();
    boot_ready();
    register_clusters();

    assert(dispatch_level(0xFFu, pld, (u16)sizeof(pld)) ==
           ZCL_STA_UNSUP_CLUSTER_COMMAND);
    assert(g_level_cb_calls == 0u);

    /* Server-to-client direction dies in the real dispatcher. */
    assert(dispatch_frame(g_level_hdlr, g_level_app_cb,
                          ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF, pld,
                          (u16)sizeof(pld), DISPATCH_EP,
                          ZCL_FRAME_SERVER_CLIENT_DIR) ==
           ZCL_STA_UNSUP_CLUSTER_COMMAND);
    assert(g_level_cb_calls == 0u);

    /* Wrong endpoint, plain variant: SDK execute gate blocks the callback. */
    assert(dispatch_frame(g_level_hdlr, g_level_app_cb,
                          ZCL_CMD_LEVEL_MOVE_TO_LEVEL, pld,
                          (u16)sizeof(pld), 0x02u,
                          ZCL_FRAME_CLIENT_SERVER_DIR) == ZCL_STA_SUCCESS);
    assert(g_level_cb_calls == 0u);

    /* Wrong endpoint, WithOnOff variant: SDK executes, policy rejects and
     * the rejection propagates (P2a) instead of reporting success. */
    assert(dispatch_frame(g_level_hdlr, g_level_app_cb,
                          ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF, pld,
                          (u16)sizeof(pld), 0x02u,
                          ZCL_FRAME_CLIENT_SERVER_DIR) ==
           ZCL_STA_INVALID_FIELD);
    assert(g_level_cb_calls == 1u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
}

static void test_level_refusal_propagates(void)
{
    const u8 pld[] = {0x40u, 0x0Au, 0x00u};
    const u8 unknown[] = {0xFFu, 0x0Au, 0x00u};

    /* Not ready: the app refusal propagates (P2a), pristine said SUCCESS. */
    fixture_init();
    register_clusters();
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF, pld,
                          (u16)sizeof(pld)) == ZCL_STA_FAILURE);
    assert(g_level_cb_calls == 1u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);

    /* Reserved level value is rejected by policy and propagated. */
    fixture_init();
    boot_ready();
    register_clusters();
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF, unknown,
                          (u16)sizeof(unknown)) == ZCL_STA_INVALID_FIELD);
    assert(g_level_cb_calls == 1u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
}

/* ---- OnOff cluster ---- */

static void test_onoff_on_off_toggle(void)
{
    uint8_t last[6];

    fixture_init();
    boot_ready();
    register_clusters();

    assert(dispatch_onoff(ZCL_CMD_ONOFF_ON, NULL, 0u) == ZCL_STA_SUCCESS);
    assert(g_onoff_cb_calls == 1u);
    assert(g_onoff == 1u);
    pump_ms(10u);
    assert(host_uart_last_frame(last));
    assert(memcmp(last, ON_FE, sizeof(ON_FE)) == 0);

    assert(dispatch_onoff(ZCL_CMD_ONOFF_TOGGLE, NULL, 0u) == ZCL_STA_SUCCESS);
    assert(g_onoff == 0u);
    assert(dispatch_onoff(ZCL_CMD_ONOFF_TOGGLE, NULL, 0u) == ZCL_STA_SUCCESS);
    assert(g_onoff == 1u);

    assert(dispatch_onoff(ZCL_CMD_ONOFF_OFF, NULL, 0u) == ZCL_STA_SUCCESS);
    assert(g_onoff == 0u);
    assert(g_send_cmd_calls == 0u);
}

static void test_onoff_not_ready_refusal(void)
{
    fixture_init();
    register_clusters();
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));

    /* ON while not ready fails and the failure propagates (P2b). */
    assert(dispatch_onoff(ZCL_CMD_ONOFF_ON, NULL, 0u) == ZCL_STA_FAILURE);
    /* OFF already holds by construction, so it still reports success. */
    assert(dispatch_onoff(ZCL_CMD_ONOFF_OFF, NULL, 0u) == ZCL_STA_SUCCESS);
    assert(g_onoff_cb_calls == 2u);
}

static void test_onoff_effect_and_timed(void)
{
    const u8 effect_short[] = {0x00u};
    const u8 effect[] = {0x00u, 0x01u};
    const u8 timed_short[] = {0x01u, 0x0Au, 0x00u, 0x05u};
    const u8 timed[] = {0x01u, 0x0Au, 0x00u, 0x05u, 0x00u};

    fixture_init();
    boot_ready();
    register_clusters();

    /* P2b: short effect/timed frames are malformed, never parsed. */
    assert(dispatch_onoff(ZCL_CMD_OFF_WITH_EFFECT, effect_short,
                          (u16)sizeof(effect_short)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_onoff(ZCL_CMD_ON_WITH_TIMED_OFF, timed_short,
                          (u16)sizeof(timed_short)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(g_onoff_cb_calls == 0u);

    /* Valid effect frame maps to OFF. */
    assert(dispatch_onoff(ZCL_CMD_ONOFF_ON, NULL, 0u) == ZCL_STA_SUCCESS);
    assert(g_onoff == 1u);
    assert(dispatch_onoff(ZCL_CMD_OFF_WITH_EFFECT, effect,
                          (u16)sizeof(effect)) == ZCL_STA_SUCCESS);
    assert(g_onoff == 0u);

    /*
     * Timed-off is deliberately unsupported: explicit UNSUP (propagated),
     * never a silent mis-map onto plain ON.
     */
    assert(dispatch_onoff(ZCL_CMD_ON_WITH_TIMED_OFF, timed,
                          (u16)sizeof(timed)) ==
           ZCL_STA_UNSUP_CLUSTER_COMMAND);
    assert(g_onoff == 0u);

    assert(dispatch_onoff(ZCL_CMD_ON_WITH_RECALL_GLOBAL_SCENE, NULL, 0u) ==
           ZCL_STA_SUCCESS);
    assert(g_onoff == 1u);

    assert(dispatch_onoff(0xFFu, NULL, 0u) == ZCL_STA_UNSUP_CLUSTER_COMMAND);
    assert(g_send_cmd_calls == 0u);
}

/* ---- defensive seams the SDK never triggers ---- */

static void test_policy_defensive_seams(void)
{
    const u8 pld[] = {0x40u, 0x0Au, 0x00u};

    fixture_init();
    boot_ready();

    assert(glsd301p_zcl_onoff_command(NULL, DISPATCH_EP, ZCL_CMD_ONOFF_ON,
                                      NULL) == ZCL_STA_FAILURE);
    assert(glsd301p_zcl_level_command(NULL, DISPATCH_EP,
                                      ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
                                      NULL) == ZCL_STA_FAILURE);
    assert(glsd301p_zcl_onoff_command(&g_zctx, 0x02u, ZCL_CMD_ONOFF_ON,
                                      NULL) == ZCL_STA_INVALID_FIELD);
    assert(glsd301p_zcl_level_command(
               &g_zctx, DISPATCH_EP, ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
               NULL) == ZCL_STA_INVALID_FIELD);
    assert(glsd301p_zcl_level_command(&g_zctx, DISPATCH_EP,
                                      ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
                                      (void *)pld) == ZCL_STA_SUCCESS);
}

int main(void)
{
    test_level_move_with_onoff_ready();
    test_level_plain_move_gated_while_off();
    test_level_short_frames_rejected();
    test_level_step_move_stop();
    test_level_unknown_and_misdirected();
    test_level_refusal_propagates();
    test_onoff_on_off_toggle();
    test_onoff_not_ready_refusal();
    test_onoff_effect_and_timed();
    test_policy_defensive_seams();
    printf("GLSD301P_ZCL_DISPATCH=PASS\n");
    return 0;
}
