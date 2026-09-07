/*
 * GL-SD-301P Stage-0 production-only safety canary.
 *
 * This image intentionally contains no power-stage, UART, PUSH or PB4 code.
 * Its first application action starts a watchdog and establishes a journaled,
 * power-fail-safe return path to the untouched stock image in bank A.
 */

#ifdef GLSD_TELINK_SDK

#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "bdb.h"
#include "ota.h"

#include "glsd_stage0_recovery.h"

#define GLSD_ENDPOINT                         0x0Bu
#define GLSD_MANUFACTURER_CODE                0x124Fu
#define GLSD_RECOVERY_ONLY_IMAGE_TYPE         0x7F10u
#define GLSD_STAGE0_RETURN_TO_STOCK_MS        60000u
#define GLSD_STAGE0_EARLY_WATCHDOG_MS         3000u

static ev_timer_event_t *g_return_timer;
static glsd_stage0_recovery_status_t g_recovery_status;

static u8 g_basic_zcl_version = 0x03u;
static u8 g_basic_app_version = 0x10u;
static u8 g_basic_stack_version = 0x02u;
static u8 g_basic_hw_version = 0x02u;
static u8 g_basic_power_source = POWER_SOURCE_MAINS_1_PHASE;
static u8 g_basic_device_enabled = TRUE;
static u8 g_basic_mfr_name[] = {8,'G','L','S','D','-','L','A','B'};
static u8 g_basic_model_id[] = {16,'G','L','-','S','D','-','3','0','1','P','-','S','T','G','0','R'};
static u8 g_basic_date_code[] = {8,'2','0','2','6','0','9','0','7'};
static u8 g_basic_sw_build_id[] = {12,'S','T','A','G','E','0','-','R','B','-','0','0'};

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

static const u16 g_in_clusters[] = { ZCL_CLUSTER_GEN_BASIC };
static const u16 g_recovery_out_clusters[] = { ZCL_CLUSTER_OTA };

static const af_simple_descriptor_t g_safe_desc = {
    HA_PROFILE_ID,
    HA_DEV_DIMMABLE_LIGHT,
    GLSD_ENDPOINT,
    1,
    0,
    1,
    0,
    (u16 *)g_in_clusters,
    NULL,
};

static const af_simple_descriptor_t g_recovery_desc = {
    HA_PROFILE_ID,
    HA_DEV_DIMMABLE_LIGHT,
    GLSD_ENDPOINT,
    1,
    0,
    1,
    1,
    (u16 *)g_in_clusters,
    (u16 *)g_recovery_out_clusters,
};

static const zcl_specClusterInfo_t g_cluster_list[] = {
    {ZCL_CLUSTER_GEN_BASIC, MANUFACTURER_CODE_NONE,
     sizeof(g_basic_attrs) / sizeof(g_basic_attrs[0]), g_basic_attrs,
     zcl_basic_register, NULL},
};

/* This runtime identity is intentionally NOT the production GLEDOPTO image
 * type. It exists solely as an escape hatch if early stock-bank repair fails.
 * A normal 0x1416 GLEDOPTO OTA therefore cannot be selected while Stage-0 is
 * the only bootable bank. */
static ota_preamble_t g_recovery_ota_info = {
    .fileVer = FILE_VERSION,
    .imageType = GLSD_RECOVERY_ONLY_IMAGE_TYPE,
    .manufacturerCode = GLSD_MANUFACTURER_CODE,
};

static void glsd_recovery_ota_event(u8 evt, u8 status)
{
    if (evt == OTA_EVT_COMPLETE && status == ZCL_STA_SUCCESS) {
        ota_mcuReboot();
    }
}

static ota_callBack_t g_recovery_ota_cb = { glsd_recovery_ota_event };

static const zdo_appIndCb_t g_zdo_callbacks = {
    bdb_zdoStartDevCnf,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

static void glsd_bdb_init_cb(u8 status, u8 joined_network)
{
    (void)status;
    (void)joined_network;
    /* Stage-0 never starts network steering or factory-new commissioning. */
}

static void glsd_bdb_commission_cb(u8 status, void *arg)
{
    (void)status;
    (void)arg;
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

static s32 glsd_return_to_stock(void *arg)
{
    (void)arg;
    g_return_timer = NULL;

    if (glsd_stage0_recovery_armed()) {
        /* Stage-0's own boot flag has already been invalidated. */
        SYSTEM_RESET();
    }
    return -1;
}

static void glsd_encode_status(g lsd_stage0_recovery_status_t status);

/* Keep the Zigbee-visible build id diagnostic without introducing printf/UART. */
static void glsd_set_status_build_id(glsd_stage0_recovery_status_t status)
{
    u8 code = (status == GLSD_STAGE0_RECOVERY_OK) ? 0u : (u8)(-status);
    if (code > 9u) {
        code = 9u;
    }
    g_basic_sw_build_id[11] = (u8)('0' + code);
    g_basic_sw_build_id[12] = glsd_stage0_recovery_armed() ? 'A' : 'F';
}

void user_init(bool isRetention)
{
    const af_simple_descriptor_t *desc;
    (void)isRetention;

    /* The stock-bank repair happens before Zigbee initialization. The regular
     * Telink main starts its watchdog only after app_init(), so Stage-0 starts
     * one here first and the recovery loops feed it explicitly. */
    drv_wd_setInterval(GLSD_STAGE0_EARLY_WATCHDOG_MS);
    drv_wd_start();
    g_recovery_status = glsd_stage0_recovery_prepare();
    drv_wd_clear();

    glsd_set_status_build_id(g_recovery_status);
    desc = glsd_stage0_recovery_armed() ? &g_safe_desc : &g_recovery_desc;

    zb_init();
    zb_zdoCbRegister((zdo_appIndCb_t *)&g_zdo_callbacks);
    af_nodeDescManuCodeUpdate(GLSD_MANUFACTURER_CODE);
    zcl_init(NULL);
    af_endpointRegister(GLSD_ENDPOINT, (af_simple_descriptor_t *)desc,
                        zcl_rx_handler, NULL);
    zcl_register(GLSD_ENDPOINT,
                 sizeof(g_cluster_list) / sizeof(g_cluster_list[0]),
                 (zcl_specClusterInfo_t *)g_cluster_list);

    if (!glsd_stage0_recovery_armed()) {
        ota_init(OTA_TYPE_CLIENT, (af_simple_descriptor_t *)desc,
                 &g_recovery_ota_info, &g_recovery_ota_cb);
    }

    (void)bdb_init((af_simple_descriptor_t *)desc,
                   &g_bdb_settings, &g_bdb_callbacks, 1);

    if (glsd_stage0_recovery_armed()) {
        g_return_timer = TL_ZB_TIMER_SCHEDULE(
            glsd_return_to_stock, NULL, GLSD_STAGE0_RETURN_TO_STOCK_MS);
    }
}

#endif /* GLSD_TELINK_SDK */
