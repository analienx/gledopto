/*
 * GL-SD-301P product Zigbee application.
 *
 * This is a normal mains-powered Zigbee End Device (leaf) which keeps its
 * receiver on while idle. It provides the standard dimmable-light application
 * surface on endpoint 11 and delegates electrical output to glsd_power_stage.*.
 *
 * The Telink sample-light GPIO/PWM implementation is intentionally NOT used:
 * the installed GL-SD power-stage wiring/protocol is a separate adapter.
 */

#ifdef GLSD_TELINK_SDK

#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "bdb.h"
#include "ota.h"

#include "glsd_ed_core.h"
#include "glsd_power_stage.h"

#define GLSD_ENDPOINT                    0x0Bu
#define GLSD_MANUFACTURER_CODE           0x124Fu
#define GLSD_IMAGE_TYPE                  0x1416u
#define GLSD_LEVEL_MIN                   1u
#define GLSD_LEVEL_MAX                   254u
#define GLSD_MOVE_TICK_MS                100u

static glsd_ed_core_t g_core;
static u8 g_power_ready;
static ev_timer_event_t *g_move_timer;
static s16 g_move_delta;
static u8 g_move_with_onoff;
static ev_timer_event_t *g_steer_timer;
static ev_timer_event_t *g_rejoin_timer;

/* Basic cluster: development identity is intentionally not stock-identical. */
static u8 g_basic_zcl_version = 0x03u;
static u8 g_basic_app_version = 0x01u;
static u8 g_basic_stack_version = 0x02u;
static u8 g_basic_hw_version = 0x02u;
static u8 g_basic_power_source = POWER_SOURCE_MAINS_1_PHASE;
static u8 g_basic_device_enabled = TRUE;
static u8 g_basic_mfr_name[] = {8,'G','L','E','D','O','P','T','O'};
static u8 g_basic_model_id[] = {13,'G','L','-','S','D','-','3','0','1','P','-','E','D'};
static u8 g_basic_date_code[] = {8,'2','0','2','6','0','9','0','6'};
static u8 g_basic_sw_build_id[] = {11,'G','L','S','D','-','E','D','-','D','E','V'};

static u8 g_group_name_support = 0u;

static zcl_onOffAttr_t g_onoff = {
    .onOff = ZCL_ONOFF_STATUS_OFF,
    .globalSceneControl = TRUE,
    .onTime = 0,
    .offWaitTime = 0,
    .startUpOnOff = ZCL_START_UP_ONOFF_SET_ONOFF_TO_PREVIOUS,
};

static zcl_levelAttr_t g_level = {
    .curLevel = GLSD_LEVEL_MAX,
    .remainingTime = 0,
    .options = 0,
    .minLevel = GLSD_LEVEL_MIN,
    .maxLevel = GLSD_LEVEL_MAX,
    .startUpCurrentLevel = ZCL_START_UP_CURRENT_LEVEL_TO_PREVIOUS,
};

static const zclAttrInfo_t g_basic_attrs[] = {
    {ZCL_ATTRID_BASIC_ZCL_VER,      ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ, (u8 *)&g_basic_zcl_version},
    {ZCL_ATTRID_BASIC_APP_VER,      ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ, (u8 *)&g_basic_app_version},
    {ZCL_ATTRID_BASIC_STACK_VER,    ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ, (u8 *)&g_basic_stack_version},
    {ZCL_ATTRID_BASIC_HW_VER,       ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ, (u8 *)&g_basic_hw_version},
    {ZCL_ATTRID_BASIC_MFR_NAME,     ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, g_basic_mfr_name},
    {ZCL_ATTRID_BASIC_MODEL_ID,     ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, g_basic_model_id},
    {ZCL_ATTRID_BASIC_DATE_CODE,    ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, g_basic_date_code},
    {ZCL_ATTRID_BASIC_POWER_SOURCE, ZCL_DATA_TYPE_ENUM8,    ACCESS_CONTROL_READ, (u8 *)&g_basic_power_source},
    {ZCL_ATTRID_BASIC_DEV_ENABLED,  ZCL_DATA_TYPE_BOOLEAN,  ACCESS_CONTROL_READ, (u8 *)&g_basic_device_enabled},
    {ZCL_ATTRID_BASIC_SW_BUILD_ID,  ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, g_basic_sw_build_id},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&zcl_attr_global_clusterRevision},
};

static const zclAttrInfo_t g_group_attrs[] = {
    {ZCL_ATTRID_GROUP_NAME_SUPPORT, ZCL_DATA_TYPE_BITMAP8, ACCESS_CONTROL_READ, (u8 *)&g_group_name_support},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&zcl_attr_global_clusterRevision},
};

static const zclAttrInfo_t g_onoff_attrs[] = {
    {ZCL_ATTRID_ONOFF, ZCL_DATA_TYPE_BOOLEAN,
     ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8 *)&g_onoff.onOff},
    {ZCL_ATTRID_GLOBAL_SCENE_CONTROL, ZCL_DATA_TYPE_BOOLEAN,
     ACCESS_CONTROL_READ, (u8 *)&g_onoff.globalSceneControl},
    {ZCL_ATTRID_ON_TIME, ZCL_DATA_TYPE_UINT16,
     ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_onoff.onTime},
    {ZCL_ATTRID_OFF_WAIT_TIME, ZCL_DATA_TYPE_UINT16,
     ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_onoff.offWaitTime},
    {ZCL_ATTRID_START_UP_ONOFF, ZCL_DATA_TYPE_ENUM8,
     ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_onoff.startUpOnOff},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,
     ACCESS_CONTROL_READ, (u8 *)&zcl_attr_global_clusterRevision},
};

static const zclAttrInfo_t g_level_attrs[] = {
    {ZCL_ATTRID_LEVEL_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8,
     ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8 *)&g_level.curLevel},
    {ZCL_ATTRID_LEVEL_REMAINING_TIME, ZCL_DATA_TYPE_UINT16,
     ACCESS_CONTROL_READ, (u8 *)&g_level.remainingTime},
    {ZCL_ATTRID_LEVEL_MIN_LEVEL, ZCL_DATA_TYPE_UINT8,
     ACCESS_CONTROL_READ, (u8 *)&g_level.minLevel},
    {ZCL_ATTRID_LEVEL_MAX_LEVEL, ZCL_DATA_TYPE_UINT8,
     ACCESS_CONTROL_READ, (u8 *)&g_level.maxLevel},
    {ZCL_ATTRID_LEVEL_OPTIONS, ZCL_DATA_TYPE_BITMAP8,
     ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_level.options},
    {ZCL_ATTRID_LEVEL_START_UP_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8,
     ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_level.startUpCurrentLevel},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,
     ACCESS_CONTROL_READ, (u8 *)&zcl_attr_global_clusterRevision},
};

static const u16 g_in_clusters[] = {
    ZCL_CLUSTER_GEN_BASIC,
    ZCL_CLUSTER_GEN_GROUPS,
    ZCL_CLUSTER_GEN_ON_OFF,
    ZCL_CLUSTER_GEN_LEVEL_CONTROL,
};

static const u16 g_out_clusters[] = {
    ZCL_CLUSTER_OTA,
};

static const af_simple_descriptor_t g_simple_desc = {
    HA_PROFILE_ID,
    HA_DEV_DIMMABLE_LIGHT,
    GLSD_ENDPOINT,
    1,
    0,
    sizeof(g_in_clusters) / sizeof(g_in_clusters[0]),
    sizeof(g_out_clusters) / sizeof(g_out_clusters[0]),
    (u16 *)g_in_clusters,
    (u16 *)g_out_clusters,
};

static int glsd_hw_apply(void *user, uint8_t on, uint8_t level)
{
    (void)user;
    if (!g_power_ready) {
        return -1;
    }
    return glsd_power_stage_apply(on, level);
}

static void glsd_sync_attrs(void)
{
    g_onoff.onOff = g_core.on ? ZCL_ONOFF_STATUS_ON : ZCL_ONOFF_STATUS_OFF;
    g_level.curLevel = g_core.level;
}

static status_t glsd_onoff_cb(zclIncomingAddrInfo_t *addr, u8 cmd, void *payload)
{
    int rc = GLSD_ED_ERR_ARG;
    (void)payload;

    if (!addr || addr->dstEp != GLSD_ENDPOINT) {
        return ZCL_STA_INVALID_FIELD;
    }

    switch (cmd) {
    case ZCL_CMD_ONOFF_ON:
        rc = glsd_ed_set_on(&g_core, 1u);
        break;
    case ZCL_CMD_ONOFF_OFF:
        rc = glsd_ed_set_on(&g_core, 0u);
        break;
    case ZCL_CMD_ONOFF_TOGGLE:
        rc = glsd_ed_toggle(&g_core);
        break;
    default:
        return ZCL_STA_UNSUP_CLUSTER_COMMAND;
    }

    if (rc != GLSD_ED_OK) {
        return ZCL_STA_FAILURE;
    }
    glsd_sync_attrs();
    return ZCL_STA_SUCCESS;
}

static s32 glsd_move_tick(void *arg)
{
    (void)arg;

    if (!g_move_delta || glsd_ed_step_level(&g_core, g_move_delta, g_move_with_onoff) != GLSD_ED_OK) {
        g_move_timer = NULL;
        g_level.remainingTime = 0;
        return -1;
    }

    glsd_sync_attrs();
    g_level.remainingTime = 0xFFFFu;

    if ((g_move_delta > 0 && g_core.level >= g_core.max_level) ||
        (g_move_delta < 0 && g_core.level <= g_core.min_level)) {
        g_move_timer = NULL;
        g_level.remainingTime = 0;
        return -1;
    }
    return 0;
}

static void glsd_stop_move(void)
{
    if (g_move_timer) {
        TL_ZB_TIMER_CANCEL(&g_move_timer);
    }
    g_move_timer = NULL;
    g_move_delta = 0;
    g_level.remainingTime = 0;
}

static status_t glsd_level_cb(zclIncomingAddrInfo_t *addr, u8 cmd_id, void *payload)
{
    int rc = GLSD_ED_OK;
    u8 with_onoff;

    if (!addr || addr->dstEp != GLSD_ENDPOINT || !payload) {
        return ZCL_STA_INVALID_FIELD;
    }

    switch (cmd_id) {
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL:
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF: {
        moveToLvl_t *cmd = (moveToLvl_t *)payload;
        with_onoff = (cmd_id == ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF);
        glsd_stop_move();
        rc = glsd_ed_set_level(&g_core, cmd->level, with_onoff);
        break;
    }
    case ZCL_CMD_LEVEL_STEP:
    case ZCL_CMD_LEVEL_STEP_WITH_ON_OFF: {
        step_t *cmd = (step_t *)payload;
        s16 delta = (cmd->stepMode == LEVEL_STEP_UP) ? (s16)cmd->stepSize : -(s16)cmd->stepSize;
        with_onoff = (cmd_id == ZCL_CMD_LEVEL_STEP_WITH_ON_OFF);
        glsd_stop_move();
        rc = glsd_ed_step_level(&g_core, delta, with_onoff);
        break;
    }
    case ZCL_CMD_LEVEL_MOVE:
    case ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF: {
        move_t *cmd = (move_t *)payload;
        s16 per_tick = (s16)((cmd->rate + 9u) / 10u);
        if (per_tick < 1) {
            per_tick = 1;
        }
        g_move_delta = (cmd->moveMode == LEVEL_MOVE_UP) ? per_tick : -per_tick;
        g_move_with_onoff = (cmd_id == ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF);
        glsd_stop_move();
        g_move_delta = (cmd->moveMode == LEVEL_MOVE_UP) ? per_tick : -per_tick;
        g_move_with_onoff = (cmd_id == ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF);
        g_level.remainingTime = 0xFFFFu;
        if (glsd_move_tick(NULL) == 0) {
            g_move_timer = TL_ZB_TIMER_SCHEDULE(glsd_move_tick, NULL, GLSD_MOVE_TICK_MS);
        }
        break;
    }
    case ZCL_CMD_LEVEL_STOP:
    case ZCL_CMD_LEVEL_STOP_WITH_ON_OFF:
        glsd_stop_move();
        break;
    default:
        return ZCL_STA_UNSUP_CLUSTER_COMMAND;
    }

    if (rc != GLSD_ED_OK) {
        return ZCL_STA_FAILURE;
    }
    glsd_sync_attrs();
    return ZCL_STA_SUCCESS;
}

static const zcl_specClusterInfo_t g_cluster_list[] = {
    {ZCL_CLUSTER_GEN_BASIC, MANUFACTURER_CODE_NONE,
     sizeof(g_basic_attrs) / sizeof(g_basic_attrs[0]), g_basic_attrs,
     zcl_basic_register, NULL},
    {ZCL_CLUSTER_GEN_GROUPS, MANUFACTURER_CODE_NONE,
     sizeof(g_group_attrs) / sizeof(g_group_attrs[0]), g_group_attrs,
     zcl_group_register, NULL},
    {ZCL_CLUSTER_GEN_ON_OFF, MANUFACTURER_CODE_NONE,
     sizeof(g_onoff_attrs) / sizeof(g_onoff_attrs[0]), g_onoff_attrs,
     zcl_onOff_register, glsd_onoff_cb},
    {ZCL_CLUSTER_GEN_LEVEL_CONTROL, MANUFACTURER_CODE_NONE,
     sizeof(g_level_attrs) / sizeof(g_level_attrs[0]), g_level_attrs,
     zcl_level_register, glsd_level_cb},
};

static ota_preamble_t g_ota_info = {
    .fileVer = FILE_VERSION,
    .imageType = GLSD_IMAGE_TYPE,
    .manufacturerCode = GLSD_MANUFACTURER_CODE,
};

static void glsd_ota_event(u8 evt, u8 status)
{
    if (evt == OTA_EVT_COMPLETE && status == ZCL_STA_SUCCESS) {
        ota_mcuReboot();
    }
}

static ota_callBack_t g_ota_cb = { glsd_ota_event };

static s32 glsd_steer_start(void *arg)
{
    (void)arg;
    g_steer_timer = NULL;
    if (zb_isDeviceFactoryNew()) {
        bdb_networkSteerStart();
    }
    return -1;
}

static s32 glsd_rejoin(void *arg)
{
    (void)arg;
    if (zb_isDeviceFactoryNew() || zb_isDeviceJoinedNwk()) {
        g_rejoin_timer = NULL;
        return -1;
    }
    zb_rejoinReq(zb_apsChannelMaskGet(), g_bdbAttrs.scanDuration);
    return 0;
}

static void glsd_bdb_init_cb(u8 status, u8 joined_network)
{
    if (status != BDB_INIT_STATUS_SUCCESS) {
        return;
    }
    if (!joined_network && !g_steer_timer) {
        u16 jitter = (u16)((zb_random() % 0x0FFFu) + 1u);
        g_steer_timer = TL_ZB_TIMER_SCHEDULE(glsd_steer_start, NULL, jitter);
    }
}

static void glsd_bdb_commission_cb(u8 status, void *arg)
{
    (void)arg;
    switch (status) {
    case BDB_COMMISSION_STA_NO_NETWORK:
    case BDB_COMMISSION_STA_TCLK_EX_FAILURE:
    case BDB_COMMISSION_STA_TARGET_FAILURE:
        if (!g_steer_timer) {
            g_steer_timer = TL_ZB_TIMER_SCHEDULE(glsd_steer_start, NULL, 1000);
        }
        break;
    case BDB_COMMISSION_STA_REJOIN_FAILURE:
    case BDB_LOSS_CONNECTIVITY:
        if (!g_rejoin_timer) {
            g_rejoin_timer = TL_ZB_TIMER_SCHEDULE(glsd_rejoin, NULL, 60000);
        }
        break;
    default:
        break;
    }
}

static bdb_appCb_t g_bdb_callbacks = {
    glsd_bdb_init_cb,
    glsd_bdb_commission_cb,
    NULL,
    NULL,
};

static bdb_commissionSetting_t g_bdb_settings = {
    .linkKey.tcLinkKey.keyType = SS_GLOBAL_LINK_KEY,
    .linkKey.tcLinkKey.key = (u8 *)tcLinkKeyCentralDefault,
    .linkKey.distributeLinkKey.keyType = MASTER_KEY,
    .linkKey.distributeLinkKey.key = (u8 *)linkKeyDistributedMaster,
    .linkKey.touchLinkKey.keyType = MASTER_KEY,
    .linkKey.touchLinkKey.key = (u8 *)touchLinkKeyMaster,
    .touchlinkEnable = 0,
    .touchlinkChannel = DEFAULT_CHANNEL,
    .touchlinkLqiThreshold = 0xA0,
};

static const zdo_appIndCb_t g_zdo_callbacks = {
    bdb_zdoStartDevCnf,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

static void glsd_app_task(void)
{
    /* Standard report-table processing; no coordinator/network manipulation. */
    report_handler();
}

void user_init(bool isRetention)
{
    glsd_ed_hw_t hw;
    u8 reportable_change[2] = {0, 0};
    (void)isRetention;

    g_power_ready = (glsd_power_stage_init() == 0) ? 1u : 0u;
    hw.user = NULL;
    hw.apply_output = glsd_hw_apply;
    (void)glsd_ed_core_init(&g_core, &hw, 0u, GLSD_LEVEL_MAX, GLSD_LEVEL_MIN, GLSD_LEVEL_MAX);

    zb_init();
    zb_zdoCbRegister((zdo_appIndCb_t *)&g_zdo_callbacks);
    af_nodeDescManuCodeUpdate(GLSD_MANUFACTURER_CODE);

    zcl_init(NULL);
    af_endpointRegister(GLSD_ENDPOINT, (af_simple_descriptor_t *)&g_simple_desc, zcl_rx_handler, NULL);
    zcl_reportingTabInit();
    zcl_register(GLSD_ENDPOINT,
                 sizeof(g_cluster_list) / sizeof(g_cluster_list[0]),
                 (zcl_specClusterInfo_t *)g_cluster_list);

    ota_init(OTA_TYPE_CLIENT, (af_simple_descriptor_t *)&g_simple_desc, &g_ota_info, &g_ota_cb);

    bdb_defaultReportingCfg(GLSD_ENDPOINT, HA_PROFILE_ID,
                            ZCL_CLUSTER_GEN_ON_OFF, ZCL_ATTRID_ONOFF,
                            1, 120, reportable_change);
    bdb_defaultReportingCfg(GLSD_ENDPOINT, HA_PROFILE_ID,
                            ZCL_CLUSTER_GEN_LEVEL_CONTROL, ZCL_ATTRID_LEVEL_CURRENT_LEVEL,
                            1, 120, reportable_change);

    ev_on_poll(EV_POLL_IDLE, glsd_app_task);
    (void)bdb_init((af_simple_descriptor_t *)&g_simple_desc,
                   &g_bdb_settings, &g_bdb_callbacks, 1);
}

#endif /* GLSD_TELINK_SDK */
