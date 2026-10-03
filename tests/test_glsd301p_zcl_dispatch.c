/*
 * Hosted ZCL dispatch coverage with REAL pinned Telink SDK bodies.
 *
 * Staged (byte-identical pre-patch, hash-pinned post-patch): zcl.c (root +
 * foundation dispatch), ev_buffer.c + mempool.c (real event pool), ev.c and
 * ev_timer.c (timers/exceptions), zcl_reporting.c (report table),
 * zcl_basic/identify/group/onoff/level.c and ota_upgrading/zcl_ota.c
 * (cluster parsers). The shared app policy (OnOff/Level) is the real
 * src/glsd301p_zcl_commands.c; Identify/Groups/OTA use the target's exact
 * callback wiring (Identify no-op SUCCESS, Groups NULL, OTA counting).
 *
 * Only opaque-archive/MCU/OTA-core seams are stubbed: af_dataSend (send
 * capture), aps group-table calls (fake table + counters), report-table
 * persistence (call counters), binding search, joined state, random, OTA
 * abort, IRQ lock. The pool is drained per test for allocation accounting.
 *
 * Built -m32: ev_buffer.c carries 32-bit layout assumptions (like the
 * TC32 target), so the host pool matches the target pool exactly.
 */

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

/* Early so the Level table mirrors live policy state like the target. */
static glsd301p_level_state_t g_level;

/* ------------------------------------------------------------------ */
/* Attribute storage: exact mirror of the target tables (ids, types, */
/* access flags). Values reset per test.                              */
/* ------------------------------------------------------------------ */

static u8 t_basic_zcl_version = 3u;
static u8 t_basic_app_version = 3u;
static u8 t_basic_stack_version = 2u;
static u8 t_basic_hw_version = 1u;
static u8 t_basic_mfr_name[] = {4, 'G', 'L', 'S', 'D'};
static u8 t_basic_model_id[] = {10, 'G', 'L', '-', 'S', 'D', '-', '3', '0', '1', 'P'};
static u8 t_basic_date_code[] = {8, '2', '0', '2', '6', '1', '0', '0', '3'};
static u8 t_basic_power_source = 1u;
static u8 t_basic_device_enabled = 1u;
static u8 t_basic_sw_build_id[] = {2, '0', '3'};
static u8 t_basic_health[49] = {48};
static u16 t_identify_time;
static u8 t_group_name_support;
static u8 t_onoff;
static u8 t_global_scene_control = 1u;
static u16 t_on_time;
static u16 t_off_wait_time;
static u8 t_startup_onoff = 0u;
static u8 t_min_level = DISPATCH_MIN_LEVEL;
static u8 t_max_level = DISPATCH_MAX_LEVEL;
static u8 t_level_options;
static u8 t_startup_current_level = 0xFFu;
static u8 t_ota_upgrade_status;
static u16 t_cluster_revision = 3u;

static const zclAttrInfo_t t_basic_attrs[] = {
    {ZCL_ATTRID_BASIC_ZCL_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&t_basic_zcl_version},
    {ZCL_ATTRID_BASIC_APP_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&t_basic_app_version},
    {ZCL_ATTRID_BASIC_STACK_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&t_basic_stack_version},
    {ZCL_ATTRID_BASIC_HW_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&t_basic_hw_version},
    {ZCL_ATTRID_BASIC_MFR_NAME, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, t_basic_mfr_name},
    {ZCL_ATTRID_BASIC_MODEL_ID, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, t_basic_model_id},
    {ZCL_ATTRID_BASIC_DATE_CODE, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, t_basic_date_code},
    {ZCL_ATTRID_BASIC_POWER_SOURCE, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ, (u8 *)&t_basic_power_source},
    {ZCL_ATTRID_BASIC_DEV_ENABLED, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ, (u8 *)&t_basic_device_enabled},
    {ZCL_ATTRID_BASIC_SW_BUILD_ID, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, t_basic_sw_build_id},
    {0xFF10u, ZCL_DATA_TYPE_OCTET_STR, ACCESS_CONTROL_READ, t_basic_health},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&t_cluster_revision},
};

static const zclAttrInfo_t t_identify_attrs[] = {
    {ZCL_ATTRID_IDENTIFY_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&t_identify_time},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&t_cluster_revision},
};

static const zclAttrInfo_t t_group_attrs[] = {
    {ZCL_ATTRID_GROUP_NAME_SUPPORT, ZCL_DATA_TYPE_BITMAP8, ACCESS_CONTROL_READ, (u8 *)&t_group_name_support},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&t_cluster_revision},
};

static const zclAttrInfo_t t_onoff_attrs[] = {
    {ZCL_ATTRID_ONOFF, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8 *)&t_onoff},
    {ZCL_ATTRID_GLOBAL_SCENE_CONTROL, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ, (u8 *)&t_global_scene_control},
    {ZCL_ATTRID_ON_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&t_on_time},
    {ZCL_ATTRID_OFF_WAIT_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&t_off_wait_time},
    {ZCL_ATTRID_START_UP_ONOFF, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&t_startup_onoff},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&t_cluster_revision},
};

static const zclAttrInfo_t t_level_attrs[] = {
    {ZCL_ATTRID_LEVEL_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8 *)&g_level.current_level},
    {ZCL_ATTRID_LEVEL_REMAINING_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&g_level.remaining_time},
    {ZCL_ATTRID_LEVEL_MIN_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&t_min_level},
    {ZCL_ATTRID_LEVEL_MAX_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&t_max_level},
    {ZCL_ATTRID_LEVEL_OPTIONS, ZCL_DATA_TYPE_BITMAP8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&t_level_options},
    {ZCL_ATTRID_LEVEL_START_UP_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&t_startup_current_level},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&t_cluster_revision},
};

static const zclAttrInfo_t t_ota_attrs[] = {
    {ZCL_ATTRID_OTA_IMAGE_UPGRADE_STATUS, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ, (u8 *)&t_ota_upgrade_status},
};

/* ------------------------------------------------------------------ */
/* Control-plane fixture (real policy path).                           */
/* ------------------------------------------------------------------ */

static glsd301p_runtime_core_t g_runtime;
static glsd301p_uart_transport_t g_transport;
static glsd301p_uart_service_t g_uart;
static glsd301p_control_ctx_t g_ctx;
static uint8_t g_onoff;
static uint16_t g_on_time;
static uint16_t g_off_wait_time;
static glsd301p_zcl_ctx_t g_zctx;

static unsigned g_level_cb_calls;
static unsigned g_onoff_cb_calls;

static status_t thunk_level(zclIncomingAddrInfo_t *addr, u8 cmd_id,
                            void *payload)
{
    g_level_cb_calls++;
    if (addr == NULL) {
        return ZCL_STA_INVALID_FIELD;
    }
    return glsd301p_zcl_level_command(&g_zctx, addr->dstEp, cmd_id, payload);
}

static status_t thunk_onoff(zclIncomingAddrInfo_t *addr, u8 cmd_id,
                            void *payload)
{
    g_onoff_cb_calls++;
    if (addr == NULL) {
        return ZCL_STA_INVALID_FIELD;
    }
    return glsd301p_zcl_onoff_command(&g_zctx, addr->dstEp, cmd_id, payload);
}

/* Mirrors glsd_identify_cb: accepted, no effect (locked by tests). */
static unsigned int identify_calls;
static status_t thunk_identify(zclIncomingAddrInfo_t *addr, u8 cmd_id,
                               void *payload)
{
    (void)addr;
    (void)cmd_id;
    (void)payload;
    identify_calls++;
    return ZCL_STA_SUCCESS;
}

static unsigned int ota_calls;
static u8 ota_last_cmd;
static status_t thunk_ota(zclIncomingAddrInfo_t *addr, u8 cmd_id,
                          void *payload)
{
    (void)addr;
    (void)payload;
    ota_calls++;
    ota_last_cmd = cmd_id;
    return ZCL_STA_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* AF/APS/NV/misc stubs (opaque or MCU-owned seams only).              */
/* ------------------------------------------------------------------ */

#define AF_CAP_MAX 8u
#define AF_ASDU_MAX 280u

typedef struct {
    u8 src_ep;
    u16 cluster;
    u16 len;
    u8 asdu[AF_ASDU_MAX];
} af_capture_t;

static af_capture_t af_caps[AF_CAP_MAX];
static unsigned int af_count;

u8 af_dataSend(u8 srcEp, epInfo_t *pDstEpInfo, u16 clusterId, u16 cmdPldLen,
               u8 *cmdPld, u8 *apsCnt)
{
    (void)pDstEpInfo;
    if (apsCnt != NULL) {
        *apsCnt = 0u;
    }
    if (af_count < AF_CAP_MAX && cmdPld != NULL &&
        cmdPldLen <= AF_ASDU_MAX) {
        af_caps[af_count].src_ep = srcEp;
        af_caps[af_count].cluster = clusterId;
        af_caps[af_count].len = cmdPldLen;
        memcpy(af_caps[af_count].asdu, cmdPld, cmdPldLen);
    }
    af_count++;
    return RET_OK;
}

/* Parse a captured ZCL ASDU (manuf bit honored). */
static bool af_parse(unsigned int idx, u16 *cluster, u8 *cmd,
                     const u8 **payload, u16 *payload_len)
{
    const u8 *b;
    u16 off;
    if (idx >= af_count || idx >= AF_CAP_MAX || cluster == NULL ||
        cmd == NULL || payload == NULL || payload_len == NULL) {
        return false;
    }
    *cluster = af_caps[idx].cluster;
    b = af_caps[idx].asdu;
    if (af_caps[idx].len < 3u) {
        return false;
    }
    off = (u8)((b[0] & 0x04u) ? 5u : 3u);
    if (af_caps[idx].len < (u16)(off + 1u)) {
        return false;
    }
    *cmd = b[off];
    *payload = b + off + 1u;
    *payload_len = (u16)(af_caps[idx].len - off - 1u);
    return true;
}

/* Fake APS group table with call counters. */
#define FAKE_GROUP_MAX 8u
static u16 fake_groups[FAKE_GROUP_MAX];
static unsigned int fake_group_num;
static unsigned int aps_add_calls;
static unsigned int aps_del_calls;
static unsigned int aps_del_all_calls;
static aps_group_tbl_ent_t fake_search_ent;

aps_status_t aps_me_group_add_req(aps_add_group_req_t *req)
{
    unsigned int i;
    aps_add_calls++;
    if (req == NULL) {
        return 1u;
    }
    for (i = 0u; i < fake_group_num; i++) {
        if (fake_groups[i] == req->group_addr) {
            return APS_STATUS_SUCCESS;
        }
    }
    if (fake_group_num >= FAKE_GROUP_MAX) {
        return 1u;
    }
    fake_groups[fake_group_num++] = req->group_addr;
    return APS_STATUS_SUCCESS;
}

aps_status_t aps_me_group_delete_req(aps_delete_group_req_t *req)
{
    unsigned int i;
    aps_del_calls++;
    if (req == NULL) {
        return 1u;
    }
    for (i = 0u; i < fake_group_num; i++) {
        if (fake_groups[i] == req->group_addr) {
            fake_groups[i] = fake_groups[--fake_group_num];
            return APS_STATUS_SUCCESS;
        }
    }
    return 1u;
}

aps_status_t aps_me_group_delete_all_req(u8 ep)
{
    (void)ep;
    aps_del_all_calls++;
    fake_group_num = 0u;
    return APS_STATUS_SUCCESS;
}

aps_group_tbl_ent_t *aps_group_search(u16 groupAddr, u8 endpoint)
{
    unsigned int i;
    (void)endpoint;
    for (i = 0u; i < fake_group_num; i++) {
        if (fake_groups[i] == groupAddr) {
            memset(&fake_search_ent, 0, sizeof(fake_search_ent));
            fake_search_ent.group_addr = groupAddr;
            return &fake_search_ent;
        }
    }
    return NULL;
}

void aps_group_list_get(u8 *counter, u16 *group_list)
{
    unsigned int i;
    if (counter == NULL) {
        return;
    }
    *counter = (u8)fake_group_num;
    if (group_list != NULL) {
        for (i = 0u; i < fake_group_num; i++) {
            group_list[i] = fake_groups[i];
        }
    }
}

u8 aps_group_entry_num_get(void)
{
    return (u8)fake_group_num;
}

static unsigned int nv_save_calls;
static unsigned int nv_restore_calls;

nv_sts_t_shim zcl_reportingTab_save(void)
{
    nv_save_calls++;
    return NV_SUCC_SHIM;
}

nv_sts_t_shim zcl_reportingTab_restore(void)
{
    nv_restore_calls++;
    return (nv_sts_t_shim)1;
}

static unsigned int binding_search_calls;

void *zb_bindingTblSearched(u16 clusterId, u8 endpoint)
{
    (void)clusterId;
    (void)endpoint;
    binding_search_calls++;
    return NULL;
}

bool zb_isDeviceJoinedNwk(void)
{
    return true;
}

u32 zb_random(void)
{
    return 0x42u;
}

static unsigned int ota_abort_calls;

void ota_upgradeAbort(void)
{
    ota_abort_calls++;
}

/* IRQ/clock seams come from hw_stub.c (shared, already linked). */

/* ------------------------------------------------------------------ */
/* Fixture + dispatch helpers.                                         */
/* ------------------------------------------------------------------ */

static apsdeDataInd_t g_msg;
static zclIncoming_t g_in;

static void fixture_init(zcl_hookFn_t hook)
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
    g_level_cb_calls = 0u;
    g_onoff_cb_calls = 0u;

    t_identify_time = 0u;
    t_group_name_support = 0u;
    t_onoff = 0u;
    t_global_scene_control = 1u;
    t_on_time = 0u;
    t_off_wait_time = 0u;
    t_startup_onoff = 0u;
    t_min_level = DISPATCH_MIN_LEVEL;
    t_max_level = DISPATCH_MAX_LEVEL;
    t_level_options = 0u;
    t_startup_current_level = 0xFFu;
    t_ota_upgrade_status = 0u;
    memset(t_basic_health, 0, sizeof(t_basic_health));
    t_basic_health[0] = 48u;

    af_count = 0u;
    memset(af_caps, 0, sizeof(af_caps));
    fake_group_num = 0u;
    aps_add_calls = 0u;
    aps_del_calls = 0u;
    aps_del_all_calls = 0u;
    nv_save_calls = 0u;
    nv_restore_calls = 0u;
    binding_search_calls = 0u;
    ota_abort_calls = 0u;
    identify_calls = 0u;
    ota_calls = 0u;
    ota_last_cmd = 0xFFu;

    memset(&g_msg, 0, sizeof(g_msg));
    g_msg.indInfo.dst_ep = DISPATCH_EP;
    g_msg.indInfo.dst_addr = 0x1234u;
    g_msg.indInfo.dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
    g_msg.indInfo.cluster_id = 0u;
    g_msg.indInfo.src_ep = 1u;
    g_msg.indInfo.src_short_addr = 0x5678u;
    g_msg.indInfo.profile_id = 0x0104u;
    g_msg.indInfo.security_status = SECURITY_IN_APSLAYER;
    memset(&g_in, 0, sizeof(g_in));
    g_in.msg = &g_msg;

    ev_buf_init();
    zcl_init(hook);
    assert(zcl_basic_register(DISPATCH_EP, MANUFACTURER_CODE_NONE,
                              (u8)(sizeof(t_basic_attrs) /
                                   sizeof(t_basic_attrs[0])),
                              t_basic_attrs, NULL) == ZCL_STA_SUCCESS);
    assert(zcl_identify_register(DISPATCH_EP, MANUFACTURER_CODE_NONE,
                                 (u8)(sizeof(t_identify_attrs) /
                                      sizeof(t_identify_attrs[0])),
                                 t_identify_attrs,
                                 thunk_identify) == ZCL_STA_SUCCESS);
    assert(zcl_group_register(DISPATCH_EP, MANUFACTURER_CODE_NONE,
                              (u8)(sizeof(t_group_attrs) /
                                   sizeof(t_group_attrs[0])),
                              t_group_attrs, NULL) == ZCL_STA_SUCCESS);
    assert(zcl_onOff_register(DISPATCH_EP, MANUFACTURER_CODE_NONE,
                              (u8)(sizeof(t_onoff_attrs) /
                                   sizeof(t_onoff_attrs[0])),
                              t_onoff_attrs,
                              thunk_onoff) == ZCL_STA_SUCCESS);
    assert(zcl_level_register(DISPATCH_EP, MANUFACTURER_CODE_NONE,
                              (u8)(sizeof(t_level_attrs) /
                                   sizeof(t_level_attrs[0])),
                              t_level_attrs,
                              thunk_level) == ZCL_STA_SUCCESS);
    assert(zcl_ota_register(DISPATCH_EP, MANUFACTURER_CODE_NONE,
                            (u8)(sizeof(t_ota_attrs) /
                                 sizeof(t_ota_attrs[0])),
                            t_ota_attrs, thunk_ota) == ZCL_STA_SUCCESS);
}

/*
 * Drive one cluster command straight into the real registered handler
 * with an exact-size ASan payload (zero length passes NULL so any
 * dereference faults immediately).
 */
static u8 dispatch_frame(u16 cluster, u8 cmd, u8 direction,
                         const u8 *payload, u16 payload_len, u8 ep)
{
    u8 *exact = NULL;
    clusterInfo_t *info;
    u8 status;

    if (payload_len > 0u) {
        exact = (u8 *)malloc(payload_len);
        assert(exact != NULL);
        memcpy(exact, payload, payload_len);
    }
    memset(&g_msg, 0, sizeof(g_msg));
    memset(&g_in, 0, sizeof(g_in));
    g_msg.indInfo.dst_ep = ep;
    g_msg.indInfo.dst_addr = 0x1234u;
    g_msg.indInfo.dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
    g_msg.indInfo.cluster_id = cluster;
    g_msg.indInfo.src_ep = 1u;
    g_msg.indInfo.src_short_addr = 0x5678u;
    g_msg.indInfo.profile_id = 0x0104u;
    g_msg.indInfo.security_status = SECURITY_IN_APSLAYER;
    g_in.msg = &g_msg;
    g_in.hdr.frmCtrl.bf.dir = direction;
    g_in.hdr.cmd = cmd;
    g_in.pData = (payload_len > 0u) ? exact : NULL;
    g_in.dataLen = payload_len;
    g_in.addrInfo.dstEp = ep;
    g_in.addrInfo.srcEp = 1u;
    g_in.attrCmd = NULL;

    info = zcl_findCluster(DISPATCH_EP, cluster);
    assert(info != NULL);
    assert(info->cmdHandlerFunc != NULL);
    g_in.clusterAppCb = info->clusterAppCb;
    status = info->cmdHandlerFunc(&g_in);

    free(exact);
    return status;
}

static u8 dispatch_level(u8 cmd, const u8 *payload, u16 len)
{
    return dispatch_frame(ZCL_CLUSTER_GEN_LEVEL_CONTROL, cmd,
                          ZCL_FRAME_CLIENT_SERVER_DIR, payload, len,
                          DISPATCH_EP);
}

static u8 dispatch_onoff(u8 cmd, const u8 *payload, u16 len)
{
    return dispatch_frame(ZCL_CLUSTER_GEN_ON_OFF, cmd,
                          ZCL_FRAME_CLIENT_SERVER_DIR, payload, len,
                          DISPATCH_EP);
}

static u8 dispatch_ep(u16 cluster, u8 cmd, u8 dir, const u8 *payload,
                      u16 len, u8 ep)
{
    return dispatch_frame(cluster, cmd, dir, payload, len, ep);
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

/*
 * Drive one full ZCL frame through the REAL root dispatcher. The ASDU
 * lives in an exact-size heap buffer (never reused), so parser overreads
 * trip ASan deterministically.
 */
static void root_frame(u16 cluster, u8 cmd, u8 specific, u8 dir,
                       const u8 *payload, u16 payload_len, u8 seq)
{
    u8 hdr[3];
    u16 total = (u16)(3u + payload_len);
    apsdeDataInd_t *ind;
    u8 frm = 0u;

    if (specific) {
        frm |= 0x01u;
    }
    if (dir) {
        frm |= 0x08u;
    }
    hdr[0] = frm;
    hdr[1] = seq;
    hdr[2] = cmd;

    ind = (apsdeDataInd_t *)malloc(sizeof(apsdeDataInd_t) + total);
    assert(ind != NULL);
    memset(ind, 0, sizeof(apsdeDataInd_t) + total);
    ind->indInfo.dst_ep = DISPATCH_EP;
    ind->indInfo.dst_addr = 0x1234u;
    ind->indInfo.dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
    ind->indInfo.cluster_id = cluster;
    ind->indInfo.src_ep = 1u;
    ind->indInfo.src_short_addr = 0x5678u;
    ind->indInfo.profile_id = 0x0104u;
    ind->indInfo.security_status = SECURITY_IN_APSLAYER;
    ind->asduLen = total;
    memcpy(ind->asdu, hdr, 3u);
    if (payload_len > 0u) {
        memcpy(ind->asdu + 3u, payload, payload_len);
    }
    zcl_cmdHandler(ind);
    free(ind);
}

/* Number of free pool buffers: drains size-1 allocs, then frees all. */
static unsigned pool_free_total(void)
{
    u8 *held[64];
    unsigned n = 0u;
    unsigned i;

    for (i = 0u; i < 64u; i++) {
        u8 *b = ev_buf_allocate(1u);
        if (b == NULL) {
            break;
        }
        held[n++] = b;
    }
    for (i = 0u; i < n; i++) {
        assert(ev_buf_free(held[i]) == BUFFER_SUCC);
    }
    return n;
}

/* Parse capture idx as a default response; returns its (rspCmd, status). */
static bool last_default_rsp(unsigned int idx, u8 *rsp_cmd, u8 *status)
{
    u16 cluster;
    u8 cmd;
    const u8 *pld;
    u16 len;

    if (!af_parse(idx, &cluster, &cmd, &pld, &len)) {
        return false;
    }
    if (cmd != ZCL_CMD_DEFAULT_RSP || len < 2u) {
        return false;
    }
    *rsp_cmd = pld[0];
    *status = pld[1];
    return true;
}

static void noop_hook(zclIncoming_t *msg)
{
    (void)msg;
}

/* ---- Level cluster (ported, same expectations) ---- */

static void test_level_move_with_onoff_ready(void)
{
    const u8 pld[] = {0x40u, 0x0Au, 0x00u};
    const u8 pld_opts[] = {0x50u, 0x05u, 0x00u, 0x01u, 0x01u};

    fixture_init(NULL);
    boot_ready();
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF, pld,
                          (u16)sizeof(pld)) == ZCL_STA_SUCCESS);
    assert(g_level_cb_calls == 1u);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x40u);
    assert(g_level.with_onoff == 1u);
    assert(af_count == 0u);

    /* Options-present form parses the trailing bytes and executes. */
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF, pld_opts,
                          (u16)sizeof(pld_opts)) == ZCL_STA_SUCCESS);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x50u);
}

static void test_level_plain_move_gated_while_off(void)
{
    const u8 pld[] = {0x40u, 0x0Au, 0x00u};

    fixture_init(NULL);
    boot_ready();
    /* OFF + default options: the SDK execute gate blocks the callback. */
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL, pld,
                          (u16)sizeof(pld)) == ZCL_STA_SUCCESS);
    assert(g_level_cb_calls == 0u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);

    /* Same frame with the light on executes. */
    t_onoff = 1u;
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

    fixture_init(NULL);
    boot_ready();
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

    fixture_init(NULL);
    boot_ready();
    t_onoff = 1u;

    g_level.current_level = 0x10u;
    assert(dispatch_level(ZCL_CMD_LEVEL_STEP, step_up,
                          (u16)sizeof(step_up)) == ZCL_STA_SUCCESS);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x15u);

    g_level.current_level = 0x05u;
    assert(dispatch_level(ZCL_CMD_LEVEL_STEP, step_down,
                          (u16)sizeof(step_down)) == ZCL_STA_SUCCESS);
    /* 0x05 - 0x0A floors at min, never wraps. */
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == DISPATCH_MIN_LEVEL);

    /* A saturating step short-circuits: already at target, stays IDLE. */
    g_level.current_level = DISPATCH_MAX_LEVEL;
    assert(dispatch_level(ZCL_CMD_LEVEL_STEP, step_up,
                          (u16)sizeof(step_up)) == ZCL_STA_SUCCESS);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);

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
    assert(af_count == 0u);
}

static void test_level_unknown_and_misdirected(void)
{
    const u8 pld[] = {0x40u, 0x0Au, 0x00u};

    fixture_init(NULL);
    boot_ready();

    assert(dispatch_level(0xFFu, pld, (u16)sizeof(pld)) ==
           ZCL_STA_UNSUP_CLUSTER_COMMAND);
    assert(g_level_cb_calls == 0u);

    /* Server-to-client direction dies in the real dispatcher. */
    assert(dispatch_ep(ZCL_CLUSTER_GEN_LEVEL_CONTROL,
                       ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
                       ZCL_FRAME_SERVER_CLIENT_DIR, pld,
                       (u16)sizeof(pld),
                       DISPATCH_EP) == ZCL_STA_UNSUP_CLUSTER_COMMAND);
    assert(g_level_cb_calls == 0u);

    /* Wrong endpoint, plain variant: SDK execute gate blocks the callback. */
    assert(dispatch_ep(ZCL_CLUSTER_GEN_LEVEL_CONTROL,
                       ZCL_CMD_LEVEL_MOVE_TO_LEVEL,
                       ZCL_FRAME_CLIENT_SERVER_DIR, pld,
                       (u16)sizeof(pld),
                       0x02u) == ZCL_STA_SUCCESS);
    assert(g_level_cb_calls == 0u);

    /* Wrong endpoint, WithOnOff variant: SDK executes, policy rejects and
     * the rejection propagates (P2a) instead of reporting success. */
    assert(dispatch_ep(ZCL_CLUSTER_GEN_LEVEL_CONTROL,
                       ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
                       ZCL_FRAME_CLIENT_SERVER_DIR, pld,
                       (u16)sizeof(pld),
                       0x02u) == ZCL_STA_INVALID_FIELD);
    assert(g_level_cb_calls == 1u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
}

static void test_level_malformed_preserves_transition(void)
{
    const u8 start[] = {0x40u, 0x0Au, 0x00u};
    const u8 rate_zero[] = {0x00u, 0x00u};
    const u8 bad_step_mode[] = {0x02u, 0x05u, 0x0Au, 0x00u};
    const u8 bad_move_mode[] = {0xFFu, 0x10u};
    const u8 short_step[] = {0x00u, 0x05u, 0x0Au};
    const u8 short_move2[] = {0x40u, 0x0Au};
    const u8 unknown_level[] = {0xFFu, 0x0Au, 0x00u};
    const u8 move[] = {0x00u, 0x10u};
    const u8 short_move[] = {0x00u};

    fixture_init(NULL);
    boot_ready();
    t_onoff = 1u;

    /* F8: every malformed input below leaves the running TARGET intact. */
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF, start,
                          (u16)sizeof(start)) == ZCL_STA_SUCCESS);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x40u);

    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF, rate_zero,
                          (u16)sizeof(rate_zero)) == ZCL_STA_INVALID_FIELD);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x40u);

    assert(dispatch_level(ZCL_CMD_LEVEL_STEP_WITH_ON_OFF, bad_step_mode,
                          (u16)sizeof(bad_step_mode)) ==
           ZCL_STA_INVALID_FIELD);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x40u);

    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF, bad_move_mode,
                          (u16)sizeof(bad_move_mode)) ==
           ZCL_STA_INVALID_FIELD);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x40u);

    assert(dispatch_level(ZCL_CMD_LEVEL_STEP_WITH_ON_OFF, short_step,
                          (u16)sizeof(short_step)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x40u);

    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
                          short_move2,
                          (u16)sizeof(short_move2)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x40u);

    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
                          unknown_level,
                          (u16)sizeof(unknown_level)) ==
           ZCL_STA_INVALID_FIELD);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x40u);

    assert(dispatch_ep(ZCL_CLUSTER_GEN_LEVEL_CONTROL,
                       ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
                       ZCL_FRAME_CLIENT_SERVER_DIR, start,
                       (u16)sizeof(start), 0x02u) == ZCL_STA_INVALID_FIELD);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x40u);

    /* Unknown OnOff commands likewise never touch the transition. */
    assert(dispatch_onoff(0xFFu, NULL, 0u) == ZCL_STA_UNSUP_CLUSTER_COMMAND);
    assert(g_level.mode == GLSD301P_LEVEL_TARGET);
    assert(g_level.target == 0x40u);

    /* Same guarantee for a running MOVE. */
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF, move,
                          (u16)sizeof(move)) == ZCL_STA_SUCCESS);
    assert(g_level.mode == GLSD301P_LEVEL_MOVE);
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF, short_move,
                          (u16)sizeof(short_move)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(g_level.mode == GLSD301P_LEVEL_MOVE);
    assert(g_level.rate == 0x10u);
    assert(g_level.direction_up == 1u);
}

static void test_level_refusal_propagates(void)
{
    const u8 pld[] = {0x40u, 0x0Au, 0x00u};
    const u8 unknown[] = {0xFFu, 0x0Au, 0x00u};

    /* Not ready: the app refusal propagates (P2a), pristine said SUCCESS. */
    fixture_init(NULL);
    assert(!glsd301p_runtime_core_is_ready(&g_runtime));
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF, pld,
                          (u16)sizeof(pld)) == ZCL_STA_FAILURE);
    assert(g_level_cb_calls == 1u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);

    /* Reserved level value is rejected by policy and propagated. */
    fixture_init(NULL);
    boot_ready();
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF, unknown,
                          (u16)sizeof(unknown)) == ZCL_STA_INVALID_FIELD);
    assert(g_level_cb_calls == 1u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
}

/* ---- OnOff cluster (ported, same expectations) ---- */

static void test_onoff_on_off_toggle(void)
{
    uint8_t last[6];

    fixture_init(NULL);
    boot_ready();

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
    assert(af_count == 0u);
}

static void test_onoff_not_ready_refusal(void)
{
    fixture_init(NULL);
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

    fixture_init(NULL);
    boot_ready();

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
    assert(af_count == 0u);
}

static void test_onoff_effect_id_validation(void)
{
    const u8 delayed[] = {0x00u, 0x00u};
    const u8 dying[] = {0x01u, 0x02u};
    const u8 reserved[] = {0x02u, 0x00u};
    const u8 reserved_ff[] = {0xFFu, 0x00u};

    fixture_init(NULL);
    boot_ready();

    /* F9: the two defined effect ids map to OFF. */
    assert(dispatch_onoff(ZCL_CMD_ONOFF_ON, NULL, 0u) == ZCL_STA_SUCCESS);
    assert(dispatch_onoff(ZCL_CMD_OFF_WITH_EFFECT, delayed,
                          (u16)sizeof(delayed)) == ZCL_STA_SUCCESS);
    assert(g_onoff == 0u);
    assert(dispatch_onoff(ZCL_CMD_ONOFF_ON, NULL, 0u) == ZCL_STA_SUCCESS);
    assert(dispatch_onoff(ZCL_CMD_OFF_WITH_EFFECT, dying,
                          (u16)sizeof(dying)) == ZCL_STA_SUCCESS);
    assert(g_onoff == 0u);

    /* Reserved ids are rejected with output state untouched. */
    assert(dispatch_onoff(ZCL_CMD_ONOFF_ON, NULL, 0u) == ZCL_STA_SUCCESS);
    assert(dispatch_onoff(ZCL_CMD_OFF_WITH_EFFECT, reserved,
                          (u16)sizeof(reserved)) == ZCL_STA_INVALID_FIELD);
    assert(g_onoff == 1u);
    assert(dispatch_onoff(ZCL_CMD_OFF_WITH_EFFECT, reserved_ff,
                          (u16)sizeof(reserved_ff)) ==
           ZCL_STA_INVALID_FIELD);
    assert(g_onoff == 1u);
}

/* ---- defensive seams the SDK never triggers ---- */

static void test_policy_defensive_seams(void)
{
    moveToLvl_t direct;

    fixture_init(NULL);
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
    /*
     * Direct calls receive parsed SDK structs, never raw bytes (the real
     * header pads transitionTime; only the SDK parser reads wire bytes).
     */
    memset(&direct, 0, sizeof(direct));
    direct.level = 0x40u;
    direct.transitionTime = 0x000Au;
    assert(glsd301p_zcl_level_command(&g_zctx, DISPATCH_EP,
                                      ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
                                      &direct) == ZCL_STA_SUCCESS);
}

/* ---- R6: Identify + Groups ingress ---- */

static u8 dispatch_identify(u8 cmd, u8 dir, const u8 *payload, u16 len)
{
    return dispatch_ep(ZCL_CLUSTER_GEN_IDENTIFY, cmd, dir, payload, len,
                       DISPATCH_EP);
}

static u8 dispatch_group(u8 cmd, u8 dir, const u8 *payload, u16 len)
{
    return dispatch_ep(ZCL_CLUSTER_GEN_GROUPS, cmd, dir, payload, len,
                       DISPATCH_EP);
}

static u8 dispatch_ota(u8 cmd, u8 dir, const u8 *payload, u16 len)
{
    return dispatch_ep(ZCL_CLUSTER_OTA, cmd, dir, payload, len, DISPATCH_EP);
}

static void test_r6_identify_truncated(void)
{
    const u8 one[] = {0x05u};

    fixture_init(NULL);
    boot_ready();

    assert(dispatch_identify(ZCL_CMD_IDENTIFY,
                             ZCL_FRAME_CLIENT_SERVER_DIR, NULL,
                             0u) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_identify(ZCL_CMD_IDENTIFY,
                             ZCL_FRAME_CLIENT_SERVER_DIR, one,
                             (u16)sizeof(one)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_identify(ZCL_CMD_TRIGGER_EFFECT,
                             ZCL_FRAME_CLIENT_SERVER_DIR, NULL,
                             0u) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_identify(ZCL_CMD_TRIGGER_EFFECT,
                             ZCL_FRAME_CLIENT_SERVER_DIR, one,
                             (u16)sizeof(one)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_identify(ZCL_CMD_IDENTIFY_QUERY_RSP,
                             ZCL_FRAME_SERVER_CLIENT_DIR, NULL,
                             0u) == ZCL_STA_MALFORMED_COMMAND);
    assert(identify_calls == 0u);
    /* Reserved effect ids never reach the callback either. */
    assert(t_identify_time == 0u);
}

static void test_r6_identify_valid(void)
{
    const u8 identify[] = {0x05u, 0x00u};
    const u8 effect[] = {0x00u, 0x00u};
    const u8 query_rsp[] = {0x07u};

    fixture_init(NULL);
    boot_ready();

    assert(dispatch_identify(ZCL_CMD_IDENTIFY,
                             ZCL_FRAME_CLIENT_SERVER_DIR, identify,
                             (u16)sizeof(identify)) == ZCL_STA_SUCCESS);
    assert(identify_calls == 1u);
    assert(dispatch_identify(ZCL_CMD_TRIGGER_EFFECT,
                             ZCL_FRAME_CLIENT_SERVER_DIR, effect,
                             (u16)sizeof(effect)) == ZCL_STA_SUCCESS);
    assert(identify_calls == 2u);
    /* Query solicits a response through the real send path. */
    assert(dispatch_identify(ZCL_CMD_IDENTIFY_QUERY,
                             ZCL_FRAME_CLIENT_SERVER_DIR, NULL,
                             0u) == ZCL_STA_SUCCESS);
    assert(af_count == 1u);
    assert(dispatch_identify(ZCL_CMD_IDENTIFY_QUERY_RSP,
                             ZCL_FRAME_SERVER_CLIENT_DIR, query_rsp,
                             (u16)sizeof(query_rsp)) == ZCL_STA_SUCCESS);
    assert(identify_calls == 3u);
}

static void test_r6_groups_truncated(void)
{
    const u8 one[] = {0x12u};
    const u8 three[] = {0x12u, 0x00u, 0x01u};
    const u8 membership1[] = {0x01u};

    fixture_init(NULL);
    boot_ready();

    assert(dispatch_group(ZCL_CMD_GROUP_ADD_GROUP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, NULL,
                          0u) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_group(ZCL_CMD_GROUP_ADD_GROUP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, one,
                          (u16)sizeof(one)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_group(ZCL_CMD_GROUP_VIEW_GROUP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, one,
                          (u16)sizeof(one)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_group(ZCL_CMD_GROUP_REMOVE_GROUP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, one,
                          (u16)sizeof(one)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_group(ZCL_CMD_GROUP_ADD_GROUP_IF_IDF,
                          ZCL_FRAME_CLIENT_SERVER_DIR, three,
                          (u16)sizeof(three)) == ZCL_STA_MALFORMED_COMMAND);
    /* {count=1} alone over-reads 2 list bytes pre-fix. */
    assert(dispatch_group(ZCL_CMD_GROUP_GET_MEMBERSHIP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, membership1,
                          (u16)sizeof(membership1)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(aps_add_calls == 0u);
    assert(aps_del_calls == 0u);
    assert(aps_del_all_calls == 0u);
    assert(fake_group_num == 0u);
    assert(af_count == 0u);
}

static void test_r6_groups_valid(void)
{
    const u8 add[] = {0x12u, 0x00u};
    const u8 add_named[] = {0x34u, 0x00u, 0x03u, 'a', 'b', 'c'};
    const u8 view[] = {0x12u, 0x00u};
    const u8 membership0[] = {0x00u};
    const u8 membership1[] = {0x01u, 0x12u, 0x00u};
    const u8 remove[] = {0x12u, 0x00u};
    const u8 add_if[] = {0x56u, 0x00u, 0x01u, 0x02u};

    fixture_init(NULL);
    boot_ready();

    assert(dispatch_group(ZCL_CMD_GROUP_ADD_GROUP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, add,
                          (u16)sizeof(add)) == ZCL_STA_SUCCESS);
    assert(aps_add_calls == 1u);
    assert(fake_group_num == 1u);
    assert(fake_groups[0] == 0x0012u);
    assert(af_count == 1u);

    /* Trailing group-name bytes are defined content: accepted, ignored. */
    assert(dispatch_group(ZCL_CMD_GROUP_ADD_GROUP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, add_named,
                          (u16)sizeof(add_named)) == ZCL_STA_SUCCESS);
    assert(fake_group_num == 2u);

    assert(dispatch_group(ZCL_CMD_GROUP_VIEW_GROUP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, view,
                          (u16)sizeof(view)) == ZCL_STA_SUCCESS);
    assert(dispatch_group(ZCL_CMD_GROUP_GET_MEMBERSHIP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, membership0,
                          (u16)sizeof(membership0)) == ZCL_STA_SUCCESS);
    assert(dispatch_group(ZCL_CMD_GROUP_GET_MEMBERSHIP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, membership1,
                          (u16)sizeof(membership1)) == ZCL_STA_SUCCESS);
    assert(dispatch_group(ZCL_CMD_GROUP_ADD_GROUP_IF_IDF,
                          ZCL_FRAME_CLIENT_SERVER_DIR, add_if,
                          (u16)sizeof(add_if)) == ZCL_STA_SUCCESS);
    assert(dispatch_group(ZCL_CMD_GROUP_REMOVE_GROUP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, remove,
                          (u16)sizeof(remove)) == ZCL_STA_SUCCESS);
    assert(fake_group_num == 1u);
    assert(dispatch_group(ZCL_CMD_GROUP_REMOVE_ALL_GROUP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, NULL,
                          0u) == ZCL_STA_SUCCESS);
    assert(aps_del_all_calls == 1u);
    assert(fake_group_num == 0u);
}

static void test_r6_groups_exact_lengths(void)
{
    const u8 view3[] = {0x12u, 0x00u, 0xFFu};
    const u8 remove_all1[] = {0x00u};
    const u8 add_if5[] = {0x56u, 0x00u, 0x01u, 0x02u, 0x03u};
    const u8 membership_bad[] = {0x01u, 0x12u};

    fixture_init(NULL);
    boot_ready();

    /* Fixed-shape group commands require exact lengths. */
    assert(dispatch_group(ZCL_CMD_GROUP_VIEW_GROUP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, view3,
                          (u16)sizeof(view3)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_group(ZCL_CMD_GROUP_REMOVE_ALL_GROUP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, remove_all1,
                          (u16)sizeof(remove_all1)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_group(ZCL_CMD_GROUP_ADD_GROUP_IF_IDF,
                          ZCL_FRAME_CLIENT_SERVER_DIR, add_if5,
                          (u16)sizeof(add_if5)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_group(ZCL_CMD_GROUP_GET_MEMBERSHIP,
                          ZCL_FRAME_CLIENT_SERVER_DIR, membership_bad,
                          (u16)sizeof(membership_bad)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(aps_add_calls == 0u);
    assert(aps_del_calls == 0u);
    assert(aps_del_all_calls == 0u);
    assert(af_count == 0u);
}

static void test_r6_group_responses_unreachable(void)
{
    const u8 rsp[] = {0x00u, 0x12u, 0x00u};

    fixture_init(NULL);
    boot_ready();

    /*
     * The target registers Groups with a NULL callback, so response-side
     * parsers never read on target. Any length is safe; the SDK reports
     * FAILURE without touching tables or the wire.
     */
    assert(dispatch_group(ZCL_CMD_GROUP_ADD_GROUP_RSP,
                          ZCL_FRAME_SERVER_CLIENT_DIR, NULL,
                          0u) == ZCL_STA_FAILURE);
    assert(dispatch_group(ZCL_CMD_GROUP_VIEW_GROUP_RSP,
                          ZCL_FRAME_SERVER_CLIENT_DIR, rsp,
                          (u16)sizeof(rsp)) == ZCL_STA_FAILURE);
    assert(aps_add_calls == 0u);
    assert(af_count == 0u);
}

/* ---- R7: foundation bounds through real root dispatch ---- */

static void test_r7_write_truncated(void)
{
    const u8 one[] = {0x00u};
    const u8 no_type[] = {0x00u, 0x00u};
    const u8 no_value[] = {0x00u, 0x00u, 0x21u};
    const u8 short_str[] = {0x04u, 0x00u, 0x42u, 0x05u, 'a', 'b'};
    u8 rsp_cmd;
    u8 status;

    fixture_init(noop_hook);

    root_frame(ZCL_CLUSTER_GEN_IDENTIFY, ZCL_CMD_WRITE, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, one, (u16)sizeof(one), 1u);
    assert(af_count == 1u);
    assert(last_default_rsp(0u, &rsp_cmd, &status));
    assert(rsp_cmd == ZCL_CMD_WRITE);
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    root_frame(ZCL_CLUSTER_GEN_IDENTIFY, ZCL_CMD_WRITE, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, no_type, (u16)sizeof(no_type),
               2u);
    assert(af_count == 2u);
    assert(last_default_rsp(1u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    root_frame(ZCL_CLUSTER_GEN_IDENTIFY, ZCL_CMD_WRITE, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, no_value,
               (u16)sizeof(no_value), 3u);
    assert(af_count == 3u);
    assert(last_default_rsp(2u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    /* Declared string length overruns the payload. */
    root_frame(ZCL_CLUSTER_GEN_BASIC, ZCL_CMD_WRITE, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, short_str,
               (u16)sizeof(short_str), 4u);
    assert(af_count == 4u);
    assert(last_default_rsp(3u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    assert(t_identify_time == 0u);
    assert(nv_save_calls == 0u);
    assert(pool_free_total() == 26u);
}

static void test_r7_write_valid_matrix(void)
{
    const u8 ok[] = {0x00u, 0x00u, 0x21u, 0x1Eu, 0x00u};
    const u8 read_only[] = {0x00u, 0x00u, 0x10u, 0x01u};
    const u8 unknown_attr[] = {0xFFu, 0xFFu, 0x21u, 0x01u, 0x00u};
    const u8 wrong_type[] = {0x00u, 0x00u, 0x20u, 0x05u};
    u16 cluster;
    u8 cmd;
    const u8 *pld;
    u16 len;

    fixture_init(noop_hook);

    /* IdentifyTime is writable: value lands, write response succeeds. */
    root_frame(ZCL_CLUSTER_GEN_IDENTIFY, ZCL_CMD_WRITE, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, ok, (u16)sizeof(ok), 1u);
    assert(t_identify_time == 30u);
    assert(af_count == 1u);
    assert(af_parse(0u, &cluster, &cmd, &pld, &len));
    assert(cluster == ZCL_CLUSTER_GEN_IDENTIFY);
    assert(cmd == ZCL_CMD_WRITE_RSP);
    assert(len == 1u);
    assert(pld[0] == ZCL_STA_SUCCESS);

    /* Read-only, unknown and mistyped records fail per-record. */
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_WRITE, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, read_only,
               (u16)sizeof(read_only), 2u);
    assert(t_onoff == 0u);
    assert(af_count == 2u);
    assert(af_parse(1u, &cluster, &cmd, &pld, &len));
    assert(cmd == ZCL_CMD_WRITE_RSP);
    assert(len == 3u);
    assert(pld[0] == ZCL_STA_READ_ONLY);
    assert(pld[1] == 0x00u);
    assert(pld[2] == 0x00u);

    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_WRITE, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, unknown_attr,
               (u16)sizeof(unknown_attr), 3u);
    assert(af_count == 3u);
    assert(af_parse(2u, &cluster, &cmd, &pld, &len));
    assert(pld[0] == ZCL_STA_UNSUPPORTED_ATTRIBUTE);

    root_frame(ZCL_CLUSTER_GEN_IDENTIFY, ZCL_CMD_WRITE, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, wrong_type,
               (u16)sizeof(wrong_type), 4u);
    assert(t_identify_time == 30u);
    assert(af_count == 4u);
    assert(af_parse(3u, &cluster, &cmd, &pld, &len));
    assert(pld[0] == ZCL_STA_INVALID_DATA_TYPE);

    assert(pool_free_total() == 26u);
}

static void test_r7_write_no_rsp_and_undivided(void)
{
    const u8 ok[] = {0x00u, 0x00u, 0x21u, 0x07u, 0x00u};
    const u8 bad[] = {0x00u, 0x00u, 0x21u, 0x07u, 0x00u, 0xFFu};
    const u8 silent[] = {0x00u, 0x00u, 0x21u, 0x09u, 0x00u};

    fixture_init(noop_hook);

    /* Undivided applies atomically; trailing garbage voids the frame. */
    root_frame(ZCL_CLUSTER_GEN_IDENTIFY, ZCL_CMD_WRITE_UNDIVIDED, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, ok, (u16)sizeof(ok), 1u);
    assert(t_identify_time == 7u);
    assert(af_count == 1u);
    root_frame(ZCL_CLUSTER_GEN_IDENTIFY, ZCL_CMD_WRITE_UNDIVIDED, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, bad, (u16)sizeof(bad), 2u);
    assert(t_identify_time == 7u);
    assert(af_count == 2u);

    /* Write-no-response applies silently on success. */
    root_frame(ZCL_CLUSTER_GEN_IDENTIFY, ZCL_CMD_WRITE_NO_RSP, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, silent, (u16)sizeof(silent),
               3u);
    assert(t_identify_time == 9u);
    assert(af_count == 2u);

    assert(pool_free_total() == 26u);
}

static void test_r7_response_parsers(void)
{
    const u8 read_rsp_short[] = {0x00u, 0x00u, 0x00u, 0x21u};
    const u8 write_rsp2[] = {0x00u, 0x12u};
    const u8 cfg_rsp3[] = {0x00u, 0x01u, 0x02u};
    const u8 report_short[] = {0x00u, 0x00u, 0x21u, 0x05u};
    u8 rsp_cmd;
    u8 status;

    fixture_init(noop_hook);

    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_READ_RSP, 0u,
               ZCL_FRAME_SERVER_CLIENT_DIR, read_rsp_short,
               (u16)sizeof(read_rsp_short), 1u);
    assert(af_count == 1u);
    assert(last_default_rsp(0u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_WRITE_RSP, 0u,
               ZCL_FRAME_SERVER_CLIENT_DIR, write_rsp2,
               (u16)sizeof(write_rsp2), 2u);
    assert(af_count == 2u);
    assert(last_default_rsp(1u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_CONFIG_REPORT_RSP, 0u,
               ZCL_FRAME_SERVER_CLIENT_DIR, cfg_rsp3,
               (u16)sizeof(cfg_rsp3), 3u);
    assert(af_count == 3u);
    assert(last_default_rsp(2u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_REPORT, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, report_short,
               (u16)sizeof(report_short), 4u);
    assert(af_count == 4u);
    assert(last_default_rsp(3u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    assert(pool_free_total() == 26u);
}

static void test_r7_dflt_and_discover_requests(void)
{
    const u8 dflt0_len = 0u;
    const u8 dflt1[] = {0x00u};
    const u8 disc1[] = {0x00u};
    const u8 disc2[] = {0x00u, 0x00u};
    const u8 disc4[] = {0x00u, 0x00u, 0xFFu, 0xFFu};
    const u8 disc_ok[] = {0x00u, 0x00u, 0xFFu};
    const u8 disc_rsp0_len = 0u;
    u8 rsp_cmd;
    u8 status;
    (void)dflt0_len;
    (void)disc_rsp0_len;

    fixture_init(noop_hook);

    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_DEFAULT_RSP, 0u,
               ZCL_FRAME_SERVER_CLIENT_DIR, NULL, 0u, 1u);
    assert(af_count == 1u);
    assert(last_default_rsp(0u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_DEFAULT_RSP, 0u,
               ZCL_FRAME_SERVER_CLIENT_DIR, dflt1, (u16)sizeof(dflt1),
               2u);
    assert(af_count == 2u);
    assert(last_default_rsp(1u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_DISCOVER_ATTR, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, disc1, (u16)sizeof(disc1),
               3u);
    assert(af_count == 3u);
    assert(last_default_rsp(2u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_DISCOVER_ATTR, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, disc2, (u16)sizeof(disc2),
               4u);
    assert(af_count == 4u);
    assert(last_default_rsp(3u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_DISCOVER_ATTR, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, disc4, (u16)sizeof(disc4),
               5u);
    assert(af_count == 5u);
    assert(last_default_rsp(4u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    /* A valid discover round-trips the real attribute table. */
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_DISCOVER_ATTR, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, disc_ok,
               (u16)sizeof(disc_ok), 6u);
    assert(af_count == 6u);

    /* Empty discover responses underflowed the (len-1) arithmetic pre-fix. */
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_DISCOVER_ATTR_RSP, 0u,
               ZCL_FRAME_SERVER_CLIENT_DIR, NULL, 0u, 7u);
    assert(af_count == 7u);
    assert(last_default_rsp(6u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_DISCOVER_ATTR_EXTD_RSP, 0u,
               ZCL_FRAME_SERVER_CLIENT_DIR, NULL, 0u, 8u);
    assert(af_count == 8u);
    assert(last_default_rsp(7u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    assert(pool_free_total() == 26u);
}

static void test_r7_read_matrix(void)
{
    const u8 read_onoff[] = {0x00u, 0x00u};
    const u8 read_odd[] = {0x00u};
    const u8 read_health[] = {0x10u, 0xFFu};
    u16 cluster;
    u8 cmd;
    const u8 *pld;
    u16 len;
    u8 rsp_cmd;
    u8 status;

    fixture_init(noop_hook);
    t_onoff = 1u;

    /* A valid read returns the framed value. */
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_READ, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, read_onoff,
               (u16)sizeof(read_onoff), 1u);
    assert(af_count == 1u);
    assert(af_parse(0u, &cluster, &cmd, &pld, &len));
    assert(cmd == ZCL_CMD_READ_RSP);
    assert(len == 5u);
    assert(pld[0] == 0x00u);
    assert(pld[1] == 0x00u);
    assert(pld[2] == ZCL_STA_SUCCESS);
    assert(pld[3] == ZCL_DATA_TYPE_BOOLEAN);
    assert(pld[4] == 1u);

    /* Odd lengths cannot frame attribute ids. */
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_READ, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, read_odd,
               (u16)sizeof(read_odd), 2u);
    assert(af_count == 2u);
    assert(last_default_rsp(1u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);

    /* The 49-byte health string reads through the same path. */
    root_frame(ZCL_CLUSTER_GEN_BASIC, ZCL_CMD_READ, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, read_health,
               (u16)sizeof(read_health), 3u);
    assert(af_count == 3u);
    assert(af_parse(2u, &cluster, &cmd, &pld, &len));
    assert(cmd == ZCL_CMD_READ_RSP);
    assert(len == 53u);

    assert(pool_free_total() == 26u);
}

static void test_r7_config_report_wrap_battery(void)
{
    u8 recs[64 * 10];
    unsigned int i;
    u8 *canary;
    u8 rsp_cmd;
    u8 status;

    fixture_init(noop_hook);

    /* 40 discrete records: pre-fix u8 length wrapped, pool overflowed. */
    for (i = 0u; i < 40u; i++) {
        recs[i * 8u + 0u] = 0x00u;
        recs[i * 8u + 1u] = (u8)(i & 0xFFu);
        recs[i * 8u + 2u] = 0x00u;
        recs[i * 8u + 3u] = ZCL_DATA_TYPE_BOOLEAN;
        recs[i * 8u + 4u] = 0x01u;
        recs[i * 8u + 5u] = 0x00u;
        recs[i * 8u + 6u] = 0xFFu;
        recs[i * 8u + 7u] = 0x00u;
    }
    canary = ev_buf_allocate(16u);
    assert(canary != NULL);
    memset(canary, 0xA5u, 16u);
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_CONFIG_REPORT, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, recs, 40u * 8u, 1u);
    assert(af_count == 1u);
    for (i = 0u; i < 16u; i++) {
        assert(canary[i] == 0xA5u);
    }
    assert(ev_buf_free(canary) == BUFFER_SUCC);
    assert(pool_free_total() == 26u);

    /* 20 analog records with reportable change bytes. */
    for (i = 0u; i < 20u; i++) {
        recs[i * 10u + 0u] = 0x00u;
        recs[i * 10u + 1u] = (u8)(i & 0xFFu);
        recs[i * 10u + 2u] = 0x00u;
        recs[i * 10u + 3u] = ZCL_DATA_TYPE_UINT16;
        recs[i * 10u + 4u] = 0x01u;
        recs[i * 10u + 5u] = 0x00u;
        recs[i * 10u + 6u] = 0xFFu;
        recs[i * 10u + 7u] = 0x00u;
        recs[i * 10u + 8u] = 0x01u;
        recs[i * 10u + 9u] = 0x00u;
    }
    canary = ev_buf_allocate(16u);
    assert(canary != NULL);
    memset(canary, 0xA5u, 16u);
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_CONFIG_REPORT, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, recs, 20u * 10u, 2u);
    assert(af_count == 2u);
    for (i = 0u; i < 16u; i++) {
        assert(canary[i] == 0xA5u);
    }
    assert(ev_buf_free(canary) == BUFFER_SUCC);
    assert(pool_free_total() == 26u);

    /* Truncated tail: rejected before any report-table change. */
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_CONFIG_REPORT, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, recs, 20u * 10u - 1u, 3u);
    assert(af_count == 3u);
    assert(last_default_rsp(2u, &rsp_cmd, &status));
    assert(status == ZCL_STA_MALFORMED_COMMAND);
    assert(pool_free_total() == 26u);
}

static void test_r7_config_report_state_guarded(void)
{
    const u8 valid[] = {0x00u, 0x00u, 0x00u, 0x10u, 0x01u, 0x00u, 0xFFu,
                        0x00u};
    const u8 truncated[] = {0x00u, 0x00u, 0x00u, 0x10u, 0x01u};
    const u8 read_cfg[] = {0x00u, 0x00u, 0x00u};
    u16 cluster;
    u8 cmd;
    const u8 *pld;
    u16 len;

    fixture_init(noop_hook);

    /* One valid entry first. */
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_CONFIG_REPORT, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, valid, (u16)sizeof(valid),
               1u);
    assert(af_count == 1u);
    assert(nv_save_calls == 1u);

    /* Malformed input changes nothing and persists nothing. */
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_CONFIG_REPORT, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, truncated,
               (u16)sizeof(truncated), 2u);
    assert(af_count == 2u);
    assert(nv_save_calls == 1u);

    /* The table still holds exactly the original entry. */
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_READ_REPORT_CFG, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, read_cfg,
               (u16)sizeof(read_cfg), 3u);
    assert(af_count == 3u);
    assert(af_parse(2u, &cluster, &cmd, &pld, &len));
    assert(cmd == ZCL_CMD_READ_REPORT_CFG_RSP);
    assert(len == 9u);
    assert(pld[0] == ZCL_STA_SUCCESS);
    assert(pld[1] == 0x00u);
    assert(pld[2] == 0x00u);
    assert(pld[3] == 0x00u);
    assert(pld[4] == ZCL_DATA_TYPE_BOOLEAN);

    assert(pool_free_total() == 26u);
}

static void test_r7_pool_exhaustion_exits(void)
{
    const u8 read_onoff[] = {0x00u, 0x00u};
    u8 *held[64];
    unsigned n = 0u;
    unsigned i;
    u8 rsp_cmd;
    u8 status;

    fixture_init(noop_hook);

    while (n < 64u) {
        u8 *b = ev_buf_allocate(1u);
        if (b == NULL) {
            break;
        }
        held[n++] = b;
    }
    assert(n == 26u);

    /* With the pool drained, a valid read fails closed, never crashes. */
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_READ, 0u,
               ZCL_FRAME_CLIENT_SERVER_DIR, read_onoff,
               (u16)sizeof(read_onoff), 1u);
    assert(af_count <= 1u);
    if (af_count == 1u) {
        assert(last_default_rsp(0u, &rsp_cmd, &status));
        assert(status == ZCL_STA_INSUFFICIENT_SPACE);
    }

    for (i = 0u; i < n; i++) {
        assert(ev_buf_free(held[i]) == BUFFER_SUCC);
    }
    assert(pool_free_total() == 26u);
}

/* ---- R8: parsed-command ownership ---- */

static void test_r8_null_hook_leaks_documented(void)
{
    const u8 read_onoff[] = {0x00u, 0x00u};
    unsigned int i;

    fixture_init(NULL);

    /* Ten ordinary reads leak exactly ten parsed-command buffers. */
    for (i = 0u; i < 10u; i++) {
        root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_READ, 0u,
                   ZCL_FRAME_CLIENT_SERVER_DIR, read_onoff,
                   (u16)sizeof(read_onoff), (u8)(i + 1u));
    }
    assert(af_count == 10u);
    assert(pool_free_total() == 16u);
}

static void test_r8_noop_hook_returns_to_baseline(void)
{
    const u8 read_onoff[] = {0x00u, 0x00u};
    const u8 write_ok[] = {0x00u, 0x00u, 0x21u, 0x09u, 0x00u};
    const u8 report[] = {0x00u, 0x00u, 0x10u, 0x01u};
    unsigned int i;

    fixture_init(noop_hook);

    for (i = 0u; i < 10u; i++) {
        root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_READ, 0u,
                   ZCL_FRAME_CLIENT_SERVER_DIR, read_onoff,
                   (u16)sizeof(read_onoff), (u8)(i + 1u));
        root_frame(ZCL_CLUSTER_GEN_IDENTIFY, ZCL_CMD_WRITE, 0u,
                   ZCL_FRAME_CLIENT_SERVER_DIR, write_ok,
                   (u16)sizeof(write_ok), (u8)(i + 11u));
        root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_REPORT, 0u,
                   ZCL_FRAME_CLIENT_SERVER_DIR, report,
                   (u16)sizeof(report), (u8)(i + 21u));
    }
    assert(af_count == 30u);
    assert(t_identify_time == 9u);
    assert(pool_free_total() == 26u);
}

/* ---- Follow-ups ---- */

static void test_followup_identify_accepted_no_effect(void)
{
    const u8 identify[] = {0x05u, 0x00u};
    const u8 effect[] = {0x01u, 0x02u};
    unsigned int uart_base;

    fixture_init(NULL);
    boot_ready();
    uart_base = host_uart_send_attempts();

    assert(dispatch_identify(ZCL_CMD_IDENTIFY,
                             ZCL_FRAME_CLIENT_SERVER_DIR, identify,
                             (u16)sizeof(identify)) == ZCL_STA_SUCCESS);
    assert(dispatch_identify(ZCL_CMD_TRIGGER_EFFECT,
                             ZCL_FRAME_CLIENT_SERVER_DIR, effect,
                             (u16)sizeof(effect)) == ZCL_STA_SUCCESS);
    /* Accepted for commissioning-tool compatibility; no state, no wire. */
    assert(identify_calls == 2u);
    assert(t_identify_time == 0u);
    assert(g_onoff == 0u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
    assert(host_uart_send_attempts() == uart_base);
    assert(af_count == 0u);
}

static void test_followup_level_exact_lengths(void)
{
    const u8 move2level4[] = {0x40u, 0x0Au, 0x00u, 0x01u};
    const u8 move3[] = {0x00u, 0x10u, 0x01u};
    const u8 step5[] = {0x00u, 0x05u, 0x0Au, 0x00u, 0x01u};
    const u8 stop1[] = {0x01u};
    const u8 stop3[] = {0x01u, 0x01u, 0x01u};

    fixture_init(NULL);
    boot_ready();
    t_onoff = 1u;

    /* Partial options are framings no valid sender produces: malformed. */
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF,
                          move2level4,
                          (u16)sizeof(move2level4)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_level(ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF, move3,
                          (u16)sizeof(move3)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_level(ZCL_CMD_LEVEL_STEP_WITH_ON_OFF, step5,
                          (u16)sizeof(step5)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_level(ZCL_CMD_LEVEL_STOP, stop1,
                          (u16)sizeof(stop1)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_level(ZCL_CMD_LEVEL_STOP, stop3,
                          (u16)sizeof(stop3)) == ZCL_STA_MALFORMED_COMMAND);
    assert(g_level_cb_calls == 0u);
    assert(g_level.mode == GLSD301P_LEVEL_IDLE);
}

static void test_followup_onoff_exact_lengths(void)
{
    const u8 on1[] = {0x00u};
    const u8 effect3[] = {0x00u, 0x01u, 0x02u};
    const u8 timed4[] = {0x01u, 0x0Au, 0x00u, 0x05u};
    const u8 timed6[] = {0x01u, 0x0Au, 0x00u, 0x05u, 0x00u, 0x00u};

    fixture_init(NULL);
    boot_ready();

    assert(dispatch_onoff(ZCL_CMD_ONOFF_ON, on1,
                          (u16)sizeof(on1)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_onoff(ZCL_CMD_OFF_WITH_EFFECT, effect3,
                          (u16)sizeof(effect3)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_onoff(ZCL_CMD_ON_WITH_TIMED_OFF, timed4,
                          (u16)sizeof(timed4)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_onoff(ZCL_CMD_ON_WITH_TIMED_OFF, timed6,
                          (u16)sizeof(timed6)) == ZCL_STA_MALFORMED_COMMAND);
    assert(g_onoff_cb_calls == 0u);
    assert(g_onoff == 0u);
}

static void test_followup_ota_bounds(void)
{
    const u8 notify_short[] = {0x03u};
    const u8 notify[] = {0x00u, 0x05u};
    const u8 query_rsp_short[] = {0x00u, 0x12u};
    const u8 block_wait_short[] = {0x97u, 0x01u, 0x02u};
    const u8 block_data_short[] = {0x00u, 0x12u, 0x00u, 0x34u, 0x00u,
                                   0x01u, 0x00u, 0x00u, 0x00u, 0x02u,
                                   0x00u, 0x00u, 0x00u, 0x05u};
    const u8 upgrade_end_short[] = {0x12u, 0x00u};

    fixture_init(NULL);
    boot_ready();

    /* Every truncated OTA shape is malformed before the OTA callback. */
    assert(dispatch_ota(ZCL_CMD_OTA_IMAGE_NOTIFY,
                        ZCL_FRAME_SERVER_CLIENT_DIR, notify_short,
                        (u16)sizeof(notify_short)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_ota(ZCL_CMD_OTA_QUERY_NEXT_IMAGE_RSP,
                        ZCL_FRAME_SERVER_CLIENT_DIR, query_rsp_short,
                        (u16)sizeof(query_rsp_short)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_ota(ZCL_CMD_OTA_IMAGE_BLOCK_RSP,
                        ZCL_FRAME_SERVER_CLIENT_DIR, block_wait_short,
                        (u16)sizeof(block_wait_short)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_ota(ZCL_CMD_OTA_IMAGE_BLOCK_RSP,
                        ZCL_FRAME_SERVER_CLIENT_DIR, block_data_short,
                        (u16)sizeof(block_data_short)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_ota(ZCL_CMD_OTA_UPGRADE_END_RSP,
                        ZCL_FRAME_SERVER_CLIENT_DIR, upgrade_end_short,
                        (u16)sizeof(upgrade_end_short)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(ota_calls == 0u);

    /* A minimal valid notify parses and reaches the callback. */
    assert(dispatch_ota(ZCL_CMD_OTA_IMAGE_NOTIFY,
                        ZCL_FRAME_SERVER_CLIENT_DIR, notify,
                        (u16)sizeof(notify)) == ZCL_STA_SUCCESS);
    assert(ota_calls == 1u);
    assert(ota_last_cmd == ZCL_CMD_OTA_IMAGE_NOTIFY);
}

static void test_followup_ota_requests(void)
{
    const u8 query_short[] = {0x00u, 0x12u};
    const u8 block_short[] = {0x00u, 0x12u, 0x00u};
    const u8 end_short[] = {0x00u};
    const u8 dev_short[] = {0x12u, 0x00u, 0x34u, 0x00u};
    const u8 page_short[] = {0x00u, 0x12u, 0x00u, 0x34u, 0x00u};
    const u8 query_ok[] = {0x00u, 0x12u, 0x00u, 0x34u, 0x00u, 0x01u,
                           0x00u, 0x00u, 0x00u};

    fixture_init(NULL);
    boot_ready();

    /*
     * Client-to-server OTA requests also parse on receipt (direction is
     * wire-controlled, callback non-NULL): truncated forms are malformed.
     */
    assert(dispatch_ota(ZCL_CMD_OTA_QUERY_NEXT_IMAGE_REQ,
                        ZCL_FRAME_CLIENT_SERVER_DIR, query_short,
                        (u16)sizeof(query_short)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_ota(ZCL_CMD_OTA_IMAGE_BLOCK_REQ,
                        ZCL_FRAME_CLIENT_SERVER_DIR, block_short,
                        (u16)sizeof(block_short)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_ota(ZCL_CMD_OTA_UPGRADE_END_REQ,
                        ZCL_FRAME_CLIENT_SERVER_DIR, end_short,
                        (u16)sizeof(end_short)) == ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_ota(ZCL_CMD_OTA_QUERY_DEVICE_SPECIFIC_FILE_REQ,
                        ZCL_FRAME_CLIENT_SERVER_DIR, dev_short,
                        (u16)sizeof(dev_short)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(dispatch_ota(ZCL_CMD_OTA_IMAGE_PAGE_REQ,
                        ZCL_FRAME_CLIENT_SERVER_DIR, page_short,
                        (u16)sizeof(page_short)) ==
           ZCL_STA_MALFORMED_COMMAND);
    assert(ota_calls == 0u);

    /* A well-formed request parses and reaches the callback. */
    assert(dispatch_ota(ZCL_CMD_OTA_QUERY_NEXT_IMAGE_REQ,
                        ZCL_FRAME_CLIENT_SERVER_DIR, query_ok,
                        (u16)sizeof(query_ok)) == ZCL_STA_SUCCESS);
    assert(ota_calls == 1u);
}

static void test_cluster_via_root_dispatch(void)
{
    fixture_init(NULL);
    boot_ready();

    /* A cluster command through the root reaches policy with no default
     * response on success. */
    root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_ONOFF_ON, 1u,
               ZCL_FRAME_CLIENT_SERVER_DIR, NULL, 0u, 9u);
    assert(g_onoff == 1u);
    assert(g_onoff_cb_calls == 1u);
    assert(af_count == 0u);
    assert(pool_free_total() == 26u);
}

static void test_followup_ota_abort_path(void)
{
    const u8 dflt_abort[] = {ZCL_CMD_OTA_UPGRADE_END_REQ, ZCL_STA_ABORT};

    fixture_init(noop_hook);
    t_ota_upgrade_status = IMAGE_UPGRADE_STATUS_DOWNLOAD_COMPLETE;

    /* OTA-TC-14C: ABORT default response on upgrade-end aborts download. */
    root_frame(ZCL_CLUSTER_OTA, ZCL_CMD_DEFAULT_RSP, 0u,
               ZCL_FRAME_SERVER_CLIENT_DIR, dflt_abort,
               (u16)sizeof(dflt_abort), 1u);
    assert(ota_abort_calls == 1u);
    assert(pool_free_total() == 26u);
}

int main(void)
{
    test_level_move_with_onoff_ready();
    test_level_plain_move_gated_while_off();
    test_level_short_frames_rejected();
    test_level_step_move_stop();
    test_level_unknown_and_misdirected();
    test_level_malformed_preserves_transition();
    test_level_refusal_propagates();
    test_onoff_on_off_toggle();
    test_onoff_not_ready_refusal();
    test_onoff_effect_and_timed();
    test_onoff_effect_id_validation();
    test_policy_defensive_seams();
    test_r6_identify_truncated();
    test_r6_identify_valid();
    test_r6_groups_truncated();
    test_r6_groups_valid();
    test_r6_groups_exact_lengths();
    test_r6_group_responses_unreachable();
    test_r7_write_truncated();
    test_r7_write_valid_matrix();
    test_r7_write_no_rsp_and_undivided();
    test_r7_response_parsers();
    test_r7_dflt_and_discover_requests();
    test_r7_read_matrix();
    test_r7_config_report_wrap_battery();
    test_r7_config_report_state_guarded();
    test_r7_pool_exhaustion_exits();
    test_r8_null_hook_leaks_documented();
    test_r8_noop_hook_returns_to_baseline();
    test_followup_identify_accepted_no_effect();
    test_followup_level_exact_lengths();
    test_followup_onoff_exact_lengths();
    test_followup_ota_bounds();
    test_followup_ota_abort_path();
    test_followup_ota_requests();
    test_cluster_via_root_dispatch();
    printf("GLSD301P_ZCL_DISPATCH=PASS\n");
    return 0;
}
