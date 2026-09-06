/*
 * Minimal TLSR8258 Zigbee application shell for the temporary GL-SD firmware
 * extraction stager.
 *
 * Safety design:
 *  - restores an existing Zigbee router identity/network through the SDK BDB/NV
 *    path but never starts steering/commissioning for a factory-new node;
 *  - exposes only read-only Basic attributes plus the GL-SD 0xFC00 extraction
 *    cluster;
 *  - retains the standard Telink OTA CLIENT as the recovery channel, but does
 *    not start periodic OTA discovery/query timers; recovery is explicitly
 *    initiated by an incoming OTA Image Notify;
 *  - never calls sampleLight LED/GPIO/PWM/light/factory-reset/binding/reporting
 *    routines;
 *  - the private extraction core has only flash_read_page(), never write/erase.
 *
 * TLSR8258 standard OTA uses hardware multi-address startup. This application
 * is linked once at logical address 0 and may physically boot from 0x00000 or
 * 0x40000. The physical running bank is therefore obtained from mcuBootAddrGet()
 * at runtime and is never inferred from the link VMA.
 *
 * This source being linkable is NOT authorization to deploy it. Runtime flash
 * geometry, production MCU/revision and a validated return image remain gates.
 */

#ifdef GLSD_TELINK_SDK

#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "bdb.h"
#include "ota.h"

#include "glsd_telink_sdk_adapter.h"

#define GLSD_ENDPOINT                    0x0Bu
#define GLSD_PRIVATE_CLUSTER             0xFC00u
#define GLSD_MANUFACTURER_CODE           0x124Fu
#define GLSD_IMAGE_TYPE                  0x1416u
#define GLSD_FLASH_SIZE_512K_BYTES       0x00080000u
#define GLSD_FLASH_CAPACITY_512K         0x13u
#define GLSD_FLASH_CAPACITY_1M           0x14u

#ifndef GLSD_STAGER_BUILD_ID
#define GLSD_STAGER_BUILD_ID             0x00010000u
#endif

static glsd_stager_core_t g_glsd_core;
static u8 g_glsd_core_ready;

static u8 g_basic_zcl_version = 0x03u;
static u8 g_basic_app_version = 0x01u;
static u8 g_basic_stack_version = 0x02u;
static u8 g_basic_hw_version = 0x02u;
static u8 g_basic_power_source = POWER_SOURCE_MAINS_1_PHASE;
static u8 g_basic_device_enabled = TRUE;
static u8 g_basic_mfr_name[] = {8,'G','L','E','D','O','P','T','O'};
static u8 g_basic_model_id[] = {10,'G','L','-','S','D','-','3','0','1','P'};
static u8 g_basic_date_code[] = {8,'2','0','2','4','0','7','0','4'};
static u8 g_basic_sw_build_id[] = {11,'D','U','M','P','-','S','T','A','G','E','R'};

static const zclAttrInfo_t g_basic_attr_table[] = {
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

static const u16 g_in_clusters[] = {
    ZCL_CLUSTER_GEN_BASIC,
    GLSD_PRIVATE_CLUSTER,
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

static const zcl_specClusterInfo_t g_cluster_list[] = {
    {
        ZCL_CLUSTER_GEN_BASIC,
        MANUFACTURER_CODE_NONE,
        sizeof(g_basic_attr_table) / sizeof(g_basic_attr_table[0]),
        g_basic_attr_table,
        zcl_basic_register,
        NULL,
    },
};

static ota_preamble_t g_ota_info = {
    .fileVer = FILE_VERSION,
    .imageType = GLSD_IMAGE_TYPE,
    .manufacturerCode = GLSD_MANUFACTURER_CODE,
};

static void glsd_ota_event(u8 evt, u8 status) {
    /*
     * ota_imageNotifyHandler() in the Telink client issues Query Next Image
     * directly. We intentionally never call ota_queryStart(), so the stager
     * cannot periodically discover/query arbitrary OTA servers on its own.
     */
    if (evt == OTA_EVT_COMPLETE && status == ZCL_STA_SUCCESS) {
        ota_mcuReboot();
    }
}

static ota_callBack_t g_ota_cb = {
    glsd_ota_event,
};

static void glsd_bdb_init_cb(u8 status, u8 joined_network) {
    /* Fail closed: never start steering, commissioning, or periodic OTA query. */
    (void)status;
    (void)joined_network;
}

static bdb_appCb_t g_bdb_callbacks = {
    glsd_bdb_init_cb,
    NULL,
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

/* BDB needs the start-device confirm callback to finish normal NV restoration. */
static const zdo_appIndCb_t g_zdo_callbacks = {
    bdb_zdoStartDevCnf,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

static int glsd_flash_read_only(void *user, uint32_t address, uint8_t *dst, uint32_t length) {
    (void)user;
    flash_read_page((unsigned long)address, (unsigned long)length, (unsigned char *)dst);
    return 0;
}

static uint32_t glsd_flash_size_from_mid(uint32_t mid) {
    u8 capacity = (u8)((mid >> 16) & 0xFFu);
    if (capacity == GLSD_FLASH_CAPACITY_512K) {
        return GLSD_FLASH_SIZE_512K_BYTES;
    }
    /* 1 MiB is intentionally recognized but not accepted by extraction-core v1. */
    if (capacity == GLSD_FLASH_CAPACITY_1M) {
        return 0x00100000u;
    }
    return 0u;
}

static uint32_t glsd_new_session_id(void) {
    uint32_t id = ((uint32_t)zb_random() << 16) | (uint32_t)zb_random();
    return id ? id : 1u;
}

static void glsd_try_enable_extraction(void) {
    glsd_stager_env_t env;
    uint32_t mid = (uint32_t)flash_read_mid();
    uint32_t flash_size = glsd_flash_size_from_mid(mid);
    uint32_t boot_base = mcuBootAddrGet();

    memset(&env, 0, sizeof(env));
    env.read = glsd_flash_read_only;
    env.flash_jedec_id = mid;
    env.flash_size = flash_size;
    env.stager_build_id = GLSD_STAGER_BUILD_ID;
    env.session_id = glsd_new_session_id();

    g_glsd_core_ready = 0u;
    if (flash_size != GLSD_FLASH_SIZE_512K_BYTES) {
        return;
    }
    if (boot_base != GLSD_BANK_A_BASE && boot_base != GLSD_BANK_B_BASE) {
        return;
    }
    env.stager_base = boot_base;
    if (glsd_stager_core_init(&g_glsd_core, &env) != GLSD_STAGER_OK) {
        return;
    }
    if (glsd_telink_sdk_adapter_register(&g_glsd_core) != 0) {
        return;
    }
    g_glsd_core_ready = 1u;
}

void user_init(bool isRetention) {
    (void)isRetention;

    /* No board LED/light/GPIO/PWM/key/factory-reset initialization here. */
    zb_init();
    zb_zdoCbRegister((zdo_appIndCb_t *)&g_zdo_callbacks);

    af_nodeDescManuCodeUpdate(GLSD_MANUFACTURER_CODE);
    zcl_init(NULL);
    af_endpointRegister(GLSD_ENDPOINT, (af_simple_descriptor_t *)&g_simple_desc, zcl_rx_handler, NULL);
    zcl_register(
        GLSD_ENDPOINT,
        sizeof(g_cluster_list) / sizeof(g_cluster_list[0]),
        (zcl_specClusterInfo_t *)g_cluster_list
    );

    ota_init(OTA_TYPE_CLIENT, (af_simple_descriptor_t *)&g_simple_desc, &g_ota_info, &g_ota_cb);

    /* The extraction cluster is enabled only after runtime geometry + old-bank CRC proof. */
    glsd_try_enable_extraction();

    /* Restore existing router/NV state. Factory-new nodes stay idle: no steering call exists. */
    (void)bdb_init((af_simple_descriptor_t *)&g_simple_desc, &g_bdb_settings, &g_bdb_callbacks, 1);
}

/* Exposed only for map/nm diagnostics in the mechanics build. */
u8 glsd_telink_stager_extraction_ready(void) {
    return g_glsd_core_ready;
}

#endif /* GLSD_TELINK_SDK */
