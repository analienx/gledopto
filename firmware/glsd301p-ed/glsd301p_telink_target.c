#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "bdb.h"
#include "ota.h"
#include "drv_uart.h"
#include "drv_gpio.h"

#include "glsd301p_control.h"
#include "glsd301p_hw_io.h"
#include "glsd301p_runtime_core.h"
#include "glsd301p_timebase.h"
#include "glsd301p_timer_events.h"
#include "glsd301p_uart_service.h"
#include "glsd301p_uart_transport.h"

#define GLSD301P_MANUFACTURER_CODE            0x124Fu
#define GLSD301P_IMAGE_TYPE                   0x1416u
#define GLSD301P_MIN_LEVEL                    0x02u
#define GLSD301P_MAX_LEVEL                    0xFEu
#define GLSD301P_UART_RX_BUFFER_SIZE          16u
#define GLSD301P_UART_TX_DMA_SIZE              (4u + GLSD301P_CONTROL_FRAME_SIZE)

/*
 * Thin target glue. All IO/Level/UART policy lives in the shared control
 * plane (identical code in firmware and hosted harnesses); this unit owns
 * the ZCL data model, the SDK/hardware seam implementations, boot wiring
 * and the Zigbee stack callbacks.
 */

static glsd301p_runtime_core_t g_runtime;
static glsd301p_uart_transport_t g_uart_transport;
static glsd301p_uart_service_t g_uart_service;
static glsd301p_level_state_t g_level_state;
static glsd301p_control_ctx_t g_control;
static u8 g_uart_rx_buf[GLSD301P_UART_RX_BUFFER_SIZE] __attribute__((aligned(4)));
static u8 g_uart_tx_dma[GLSD301P_UART_TX_DMA_SIZE] __attribute__((aligned(4)));
static u8 g_uart_online;
static u8 g_zcl_online;

/* Independent implementation identity; hardware/stack lineage remains explicit. */
static u8 g_basic_zcl_version = 0x03u;
static u8 g_basic_app_version = 0x01u;
static u8 g_basic_stack_version = 0x02u;
static u8 g_basic_hw_version = 0x02u;
static u8 g_basic_power_source = POWER_SOURCE_MAINS_1_PHASE;
static u8 g_basic_device_enabled = TRUE;
static u8 g_basic_mfr_name[] = {8,'a','n','a','l','i','e','n','x'};
static u8 g_basic_model_id[] = {13,'G','L','-','S','D','-','3','0','1','P','-','E','D'};
static u8 g_basic_date_code[] = {8,'2','0','2','6','0','9','0','7'};
static u8 g_basic_sw_build_id[] = {11,'G','L','S','D','-','E','D','-','0','0','1'};

static u16 g_identify_time;
static u8 g_group_name_support;

static u8 g_onoff = ZCL_ONOFF_STATUS_OFF;
static u8 g_global_scene_control = TRUE;
static u16 g_on_time;
static u16 g_off_wait_time;
static u8 g_startup_onoff = ZCL_START_UP_ONOFF_SET_ONOFF_TO_OFF;

static u8 g_min_level = GLSD301P_MIN_LEVEL;
static u8 g_max_level = GLSD301P_MAX_LEVEL;
static u8 g_level_options;
static u8 g_startup_current_level = ZCL_START_UP_CURRENT_LEVEL_TO_PREVIOUS;

static const zclAttrInfo_t g_basic_attr_table[] = {
    {ZCL_ATTRID_BASIC_ZCL_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&g_basic_zcl_version},
    {ZCL_ATTRID_BASIC_APP_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&g_basic_app_version},
    {ZCL_ATTRID_BASIC_STACK_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&g_basic_stack_version},
    {ZCL_ATTRID_BASIC_HW_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&g_basic_hw_version},
    {ZCL_ATTRID_BASIC_MFR_NAME, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, g_basic_mfr_name},
    {ZCL_ATTRID_BASIC_MODEL_ID, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, g_basic_model_id},
    {ZCL_ATTRID_BASIC_DATE_CODE, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, g_basic_date_code},
    {ZCL_ATTRID_BASIC_POWER_SOURCE, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ, (u8 *)&g_basic_power_source},
    {ZCL_ATTRID_BASIC_DEV_ENABLED, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ, (u8 *)&g_basic_device_enabled},
    {ZCL_ATTRID_BASIC_SW_BUILD_ID, ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ, g_basic_sw_build_id},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&zcl_attr_global_clusterRevision},
};

static const zclAttrInfo_t g_identify_attr_table[] = {
    {ZCL_ATTRID_IDENTIFY_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_identify_time},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&zcl_attr_global_clusterRevision},
};

static const zclAttrInfo_t g_group_attr_table[] = {
    {ZCL_ATTRID_GROUP_NAME_SUPPORT, ZCL_DATA_TYPE_BITMAP8, ACCESS_CONTROL_READ, (u8 *)&g_group_name_support},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&zcl_attr_global_clusterRevision},
};

static const zclAttrInfo_t g_onoff_attr_table[] = {
    {ZCL_ATTRID_ONOFF, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8 *)&g_onoff},
    {ZCL_ATTRID_GLOBAL_SCENE_CONTROL, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ, (u8 *)&g_global_scene_control},
    {ZCL_ATTRID_ON_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_on_time},
    {ZCL_ATTRID_OFF_WAIT_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_off_wait_time},
    {ZCL_ATTRID_START_UP_ONOFF, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_startup_onoff},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&zcl_attr_global_clusterRevision},
};

static const zclAttrInfo_t g_level_attr_table[] = {
    {ZCL_ATTRID_LEVEL_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8 *)&g_level_state.current_level},
    {ZCL_ATTRID_LEVEL_REMAINING_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&g_level_state.remaining_time},
    {ZCL_ATTRID_LEVEL_MIN_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&g_min_level},
    {ZCL_ATTRID_LEVEL_MAX_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&g_max_level},
    {ZCL_ATTRID_LEVEL_OPTIONS, ZCL_DATA_TYPE_BITMAP8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_level_options},
    {ZCL_ATTRID_LEVEL_START_UP_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_startup_current_level},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&zcl_attr_global_clusterRevision},
};

static status_t glsd_onoff_cb(zclIncomingAddrInfo_t *addr, u8 cmd_id, void *payload);
static status_t glsd_level_cb(zclIncomingAddrInfo_t *addr, u8 cmd_id, void *payload);
static status_t glsd_identify_cb(zclIncomingAddrInfo_t *addr, u8 cmd_id, void *payload);

static const u16 g_in_clusters[] = {
    ZCL_CLUSTER_GEN_BASIC,
    ZCL_CLUSTER_GEN_IDENTIFY,
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
    GLSD301P_ENDPOINT,
    1,
    0,
    sizeof(g_in_clusters) / sizeof(g_in_clusters[0]),
    sizeof(g_out_clusters) / sizeof(g_out_clusters[0]),
    (u16 *)g_in_clusters,
    (u16 *)g_out_clusters,
};

static const zcl_specClusterInfo_t g_cluster_list[] = {
    {ZCL_CLUSTER_GEN_BASIC, MANUFACTURER_CODE_NONE,
     sizeof(g_basic_attr_table) / sizeof(g_basic_attr_table[0]), g_basic_attr_table,
     zcl_basic_register, NULL},
    {ZCL_CLUSTER_GEN_IDENTIFY, MANUFACTURER_CODE_NONE,
     sizeof(g_identify_attr_table) / sizeof(g_identify_attr_table[0]), g_identify_attr_table,
     zcl_identify_register, glsd_identify_cb},
    {ZCL_CLUSTER_GEN_GROUPS, MANUFACTURER_CODE_NONE,
     sizeof(g_group_attr_table) / sizeof(g_group_attr_table[0]), g_group_attr_table,
     zcl_group_register, NULL},
    {ZCL_CLUSTER_GEN_ON_OFF, MANUFACTURER_CODE_NONE,
     sizeof(g_onoff_attr_table) / sizeof(g_onoff_attr_table[0]), g_onoff_attr_table,
     zcl_onOff_register, glsd_onoff_cb},
    {ZCL_CLUSTER_GEN_LEVEL_CONTROL, MANUFACTURER_CODE_NONE,
     sizeof(g_level_attr_table) / sizeof(g_level_attr_table[0]), g_level_attr_table,
     zcl_level_register, glsd_level_cb},
};

static ota_preamble_t g_ota_info = {
    .fileVer = FILE_VERSION,
    .imageType = GLSD301P_IMAGE_TYPE,
    .manufacturerCode = GLSD301P_MANUFACTURER_CODE,
};

bool glsd301p_hw_uart_busy(void)
{
    return uart_tx_is_busy() != 0;
}

bool glsd301p_hw_uart_send_frame(const uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE])
{
    if (frame == NULL) {
        return false;
    }

    g_uart_tx_dma[0] = GLSD301P_CONTROL_FRAME_SIZE;
    g_uart_tx_dma[1] = 0u;
    g_uart_tx_dma[2] = 0u;
    g_uart_tx_dma[3] = 0u;
    memcpy(g_uart_tx_dma + 4u, frame, GLSD301P_CONTROL_FRAME_SIZE);

    /* Pinned TLSR8258 uart_dma_send() is explicitly nonblocking: 0 means DMA
     * busy, 1 means the transfer was accepted. No polling loop is permitted. */
    return uart_dma_send(g_uart_tx_dma) != 0;
}

bool glsd301p_hw_gpio_pc2_high(void)
{
    return drv_gpio_read(GPIO_PC2) != 0;
}

bool glsd301p_hw_gpio_pb4_high(void)
{
    return drv_gpio_read(GPIO_PB4) != 0;
}

static status_t glsd_onoff_cb(zclIncomingAddrInfo_t *addr, u8 cmd_id, void *payload)
{
    bool requested;
    u8 frame[GLSD301P_CONTROL_FRAME_SIZE];
    (void)payload;

    if (addr == NULL || addr->dstEp != GLSD301P_ENDPOINT) {
        return ZCL_STA_INVALID_FIELD;
    }

    switch (cmd_id) {
    case ZCL_CMD_ONOFF_OFF:
    case ZCL_CMD_OFF_WITH_EFFECT:
        requested = false;
        break;
    case ZCL_CMD_ONOFF_ON:
    case ZCL_CMD_ON_WITH_RECALL_GLOBAL_SCENE:
        requested = true;
        break;
    case ZCL_CMD_ONOFF_TOGGLE:
        requested = !g_runtime.logical_output_enabled;
        break;
    default:
        return ZCL_STA_UNSUP_CLUSTER_COMMAND;
    }

    /*
     * Refusal leaves any running transition untouched. OFF already holds by
     * construction while not ready, so it still reports success.
     */
    if (!glsd301p_runtime_core_is_ready(&g_runtime)) {
        return requested ? ZCL_STA_FAILURE : ZCL_STA_SUCCESS;
    }

    glsd301p_control_level_cancel(&g_control);

    if (!glsd301p_control_emit(
            &g_control,
            glsd301p_runtime_core_apply_state(&g_runtime, requested,
                                              g_level_state.current_level,
                                              g_min_level, false, frame),
            frame, glsd301p_timebase_now_ms())) {
        return ZCL_STA_FAILURE;
    }

    g_on_time = 0u;
    if (!requested) {
        g_off_wait_time = 0u;
    }
    return ZCL_STA_SUCCESS;
}

static status_t glsd_level_cb(zclIncomingAddrInfo_t *addr, u8 cmd_id, void *payload)
{
    if (addr == NULL || addr->dstEp != GLSD301P_ENDPOINT) {
        return ZCL_STA_INVALID_FIELD;
    }

    switch (cmd_id) {
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL:
    case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF: {
        moveToLvl_t *cmd = (moveToLvl_t *)payload;
        if (cmd == NULL || cmd->level == GLSD301P_ZCL_LEVEL_UNKNOWN) {
            return ZCL_STA_INVALID_FIELD;
        }
        if (!glsd301p_control_level_start_target(
                &g_control, cmd->level, cmd->transitionTime,
                (uint8_t)(cmd_id == ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF ? 1u : 0u))) {
            return ZCL_STA_FAILURE;
        }
        return ZCL_STA_SUCCESS;
    }
    case ZCL_CMD_LEVEL_STEP:
    case ZCL_CMD_LEVEL_STEP_WITH_ON_OFF: {
        step_t *cmd = (step_t *)payload;
        u16 target = g_level_state.current_level;
        if (cmd == NULL) {
            return ZCL_STA_INVALID_FIELD;
        }
        if (cmd->stepMode == LEVEL_STEP_UP) {
            target = (u16)(target + cmd->stepSize);
        } else {
            target = (target > cmd->stepSize) ? (u16)(target - cmd->stepSize) : g_min_level;
        }
        if (!glsd301p_control_level_start_target(
                &g_control,
                glsd301p_control_clamp_level(&g_control, target),
                cmd->transitionTime,
                (uint8_t)(cmd_id == ZCL_CMD_LEVEL_STEP_WITH_ON_OFF ? 1u : 0u))) {
            return ZCL_STA_FAILURE;
        }
        return ZCL_STA_SUCCESS;
    }
    case ZCL_CMD_LEVEL_MOVE:
    case ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF: {
        move_t *cmd = (move_t *)payload;
        if (cmd == NULL) {
            return ZCL_STA_INVALID_FIELD;
        }
        if (!glsd301p_control_level_start_move(
                &g_control,
                (uint8_t)(cmd->moveMode == LEVEL_MOVE_UP ? 1u : 0u), cmd->rate,
                (uint8_t)(cmd_id == ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF ? 1u : 0u))) {
            return ZCL_STA_FAILURE;
        }
        return ZCL_STA_SUCCESS;
    }
    case ZCL_CMD_LEVEL_STOP:
    case ZCL_CMD_LEVEL_STOP_WITH_ON_OFF:
        glsd301p_control_level_cancel(&g_control);
        return ZCL_STA_SUCCESS;
    default:
        return ZCL_STA_UNSUP_CLUSTER_COMMAND;
    }
}

static status_t glsd_identify_cb(zclIncomingAddrInfo_t *addr, u8 cmd_id, void *payload)
{
    (void)addr;
    (void)cmd_id;
    (void)payload;
    return ZCL_STA_SUCCESS;
}

static void glsd_ota_event(u8 evt, u8 status)
{
    if (evt == OTA_EVT_COMPLETE && status == ZCL_STA_SUCCESS) {
        ota_mcuReboot();
    }
}

static ota_callBack_t g_ota_cb = {glsd_ota_event};

static void glsd_bdb_init_cb(u8 status, u8 joined_network)
{
    (void)joined_network;
    if (status != BDB_INIT_STATUS_SUCCESS && !zb_isDeviceFactoryNew()) {
        zb_rejoinReqWithBackOff(zb_apsChannelMaskGet(), g_bdbAttrs.scanDuration);
    }
}

static void glsd_bdb_commission_cb(u8 status, void *arg)
{
    (void)arg;
    if ((status == BDB_COMMISSION_STA_PARENT_LOST ||
         status == BDB_COMMISSION_STA_REJOIN_FAILURE) &&
        !zb_isDeviceFactoryNew()) {
        zb_rejoinReqWithBackOff(zb_apsChannelMaskGet(), g_bdbAttrs.scanDuration);
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

static void glsd_configure_power_descriptor(void)
{
    power_descriptor_t descriptor;
    memset((u8 *)&descriptor, 0, sizeof(descriptor));
    descriptor.current_power_mode = POWER_MODE_RECEIVER_SYNCHRONIZED_WHEN_ON_IDLE;
    descriptor.available_power_sources = POWER_SRC_MAINS_POWER;
    descriptor.current_power_source = POWER_SRC_MAINS_POWER;
    descriptor.current_power_source_level = POWER_LEVEL_PERCENT_100;
    af_powerDescriptorSet(&descriptor);
}

static u8 glsd_init_power_stage_io(void)
{
    glsd301p_runtime_core_init(&g_runtime);
    glsd301p_uart_transport_init(&g_uart_transport);
    glsd301p_uart_service_init(&g_uart_service);
    glsd301p_timer_events_init();
    glsd301p_timebase_init();
    glsd301p_control_init(&g_control, &g_runtime, &g_uart_transport,
                          &g_uart_service, &g_level_state, &g_onoff,
                          GLSD301P_MIN_LEVEL, GLSD301P_MAX_LEVEL,
                          GLSD301P_MAX_LEVEL, GLSD301P_MIN_LEVEL);

    drv_uart_pin_set(UART_TX_PB1, UART_RX_PA0);
    g_uart_online = (drv_uart_init(GLSD301P_TARGET_UART_BAUD,
                                   g_uart_rx_buf,
                                   sizeof(g_uart_rx_buf), NULL) == 0u);

    drv_gpio_func_set(GPIO_PC2);
    drv_gpio_output_en(GPIO_PC2, false);
    drv_gpio_input_en(GPIO_PC2, true);

    drv_gpio_func_set(GPIO_PB4);
    drv_gpio_output_en(GPIO_PB4, false);
    drv_gpio_input_en(GPIO_PB4, true);
    drv_gpio_up_down_resistor(GPIO_PB4, PM_PIN_PULLDOWN_100K);

    if (!g_uart_online) {
        glsd301p_control_boot_failed(&g_control, glsd301p_timebase_now_ms());
        return 0u;
    }

    /*
     * user_init runs before global IRQ enable. Emit exactly ONE UART frame
     * here through a single bounded DMA attempt on the aligned static
     * buffer: the confirmed electrical OFF vector. Output arms only after
     * the IO service observes this transfer complete.
     */
    if (!glsd301p_control_boot_off(&g_control, glsd301p_timebase_now_ms())) {
        glsd301p_control_boot_failed(&g_control, glsd301p_timebase_now_ms());
        return 0u;
    }

    return 1u;
}

void user_init(bool isRetention)
{
    (void)isRetention;

    /* Electrical safety path is initialized before Zigbee application state. */
    (void)glsd_init_power_stage_io();

    zb_init();
    zb_zdoCbRegister((zdo_appIndCb_t *)&g_zdo_callbacks);
    af_nodeDescManuCodeUpdate(GLSD301P_MANUFACTURER_CODE);
    glsd_configure_power_descriptor();

    zcl_init(NULL);
    af_endpointRegister(GLSD301P_ENDPOINT,
                        (af_simple_descriptor_t *)&g_simple_desc,
                        zcl_rx_handler, NULL);
    zcl_register(GLSD301P_ENDPOINT,
                 sizeof(g_cluster_list) / sizeof(g_cluster_list[0]),
                 (zcl_specClusterInfo_t *)g_cluster_list);
    g_zcl_online = 1u;

    ota_init(OTA_TYPE_CLIENT, (af_simple_descriptor_t *)&g_simple_desc,
             &g_ota_info, &g_ota_cb);

    /*
     * Essential lifeline: ON readiness (boot arming inside the IO service)
     * is unreachable unless this registration succeeds. Failure locks the
     * output OFF while Zigbee/OTA init still proceeds.
     */
    if (!glsd301p_timer_io_start(glsd301p_control_io_cb, &g_control)) {
        glsd301p_control_boot_failed(&g_control, glsd301p_timebase_now_ms());
    }
    (void)bdb_init((af_simple_descriptor_t *)&g_simple_desc,
                   &g_bdb_settings, &g_bdb_callbacks, 1);
}

u8 glsd301p_target_runtime_ready(void)
{
    return glsd301p_runtime_core_is_ready(&g_runtime) ? 1u : 0u;
}

u8 glsd301p_target_zcl_ready(void)
{
    return g_zcl_online;
}
