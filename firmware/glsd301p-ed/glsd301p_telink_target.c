#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "bdb.h"
#include "ota.h"
#include "drv_uart.h"
#include "drv_gpio.h"

#include "glsd301p_runtime_core.h"
#include "glsd301p_uart_transport.h"

#define GLSD301P_MANUFACTURER_CODE            0x124Fu
#define GLSD301P_IMAGE_TYPE                   0x1416u
#define GLSD301P_MIN_LEVEL                    0x02u
#define GLSD301P_MAX_LEVEL                    0xFEu
#define GLSD301P_IO_POLL_MS                   1u
#define GLSD301P_LEVEL_TICK_MS                100u
#define GLSD301P_UART_RX_BUFFER_SIZE          16u
#define GLSD301P_UART_TX_DMA_SIZE              (4u + GLSD301P_CONTROL_FRAME_SIZE)
#define GLSD301P_UART_PENDING_TIMEOUT_MS       32u

static glsd301p_runtime_core_t g_runtime;
static ev_timer_event_t *g_level_timer;
static u8 g_uart_rx_buf[GLSD301P_UART_RX_BUFFER_SIZE] __attribute__((aligned(4)));
static u8 g_uart_tx_dma[GLSD301P_UART_TX_DMA_SIZE] __attribute__((aligned(4)));
static glsd301p_uart_transport_t g_uart_transport;
static u16 g_uart_pending_ms;
static u8 g_uart_transport_fault_latched;
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

static u8 g_current_level = GLSD301P_MAX_LEVEL;
static u16 g_remaining_time;
static u8 g_min_level = GLSD301P_MIN_LEVEL;
static u8 g_max_level = GLSD301P_MAX_LEVEL;
static u8 g_level_options;
static u8 g_startup_current_level = ZCL_START_UP_CURRENT_LEVEL_TO_PREVIOUS;

typedef enum {
    GLSD_LEVEL_IDLE = 0,
    GLSD_LEVEL_TARGET,
    GLSD_LEVEL_MOVE,
} glsd_level_mode_t;

typedef struct {
    glsd_level_mode_t mode;
    u8 target;
    u8 rate;
    u16 rate_accum_tenths;
    u8 direction_up;
    u8 with_onoff;
} glsd_level_transition_t;

static glsd_level_transition_t g_level_transition;

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
    {ZCL_ATTRID_LEVEL_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8 *)&g_current_level},
    {ZCL_ATTRID_LEVEL_REMAINING_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&g_remaining_time},
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

static void glsd_sync_zcl_from_runtime(void)
{
    g_onoff = g_runtime.logical_output_enabled ? ZCL_ONOFF_STATUS_ON : ZCL_ONOFF_STATUS_OFF;
    g_current_level = g_runtime.current_level;
}

static u8 glsd_uart_send_boot_off_blocking(
    const u8 frame[GLSD301P_CONTROL_FRAME_SIZE])
{
    if (!g_uart_online) {
        return 0u;
    }
    return drv_uart_tx_start((u8 *)frame, GLSD301P_CONTROL_FRAME_SIZE);
}

static u8 glsd_uart_queue(const u8 frame[GLSD301P_CONTROL_FRAME_SIZE])
{
    if (!g_uart_online) {
        return 0u;
    }
    return glsd301p_uart_transport_offer(&g_uart_transport, frame) ? 1u : 0u;
}

static void glsd_uart_service_pending(void)
{
    u8 frame[GLSD301P_CONTROL_FRAME_SIZE];
    bool is_off;

    if (!g_uart_online) {
        return;
    }
    if (!glsd301p_uart_transport_has_pending(&g_uart_transport)) {
        g_uart_pending_ms = 0u;
        return;
    }

    if (g_uart_pending_ms < 0xFFFFu) {
        g_uart_pending_ms++;
    }

    /* A wedged/busy transport must not steal the 1 ms input cadence. Latch the
     * runtime fail-OFF state once, retain priority OFF, and keep retrying only
     * through O(1) nonblocking service attempts. */
    if (g_uart_pending_ms >= GLSD301P_UART_PENDING_TIMEOUT_MS &&
        !g_uart_transport_fault_latched) {
        u8 off[GLSD301P_CONTROL_FRAME_SIZE];
        (void)glsd301p_runtime_core_latch_fault(&g_runtime, off);
        (void)glsd301p_uart_transport_offer(&g_uart_transport, off);
        g_uart_transport_fault_latched = 1u;
        glsd_sync_zcl_from_runtime();
    }

    /* The static DMA buffer must never be rewritten while hardware owns it. */
    if (uart_tx_is_busy()) {
        return;
    }
    if (!glsd301p_uart_transport_peek(&g_uart_transport, frame, &is_off)) {
        return;
    }

    g_uart_tx_dma[0] = GLSD301P_CONTROL_FRAME_SIZE;
    g_uart_tx_dma[1] = 0u;
    g_uart_tx_dma[2] = 0u;
    g_uart_tx_dma[3] = 0u;
    memcpy(g_uart_tx_dma + 4u, frame, GLSD301P_CONTROL_FRAME_SIZE);

    /* Pinned TLSR8258 uart_dma_send() is explicitly nonblocking: 0 means DMA
     * busy, 1 means the transfer was accepted. No polling loop is permitted. */
    if (uart_dma_send(g_uart_tx_dma)) {
        glsd301p_uart_transport_commit_sent(&g_uart_transport, is_off);
        g_uart_pending_ms = 0u;
    }
}

static u8 glsd_emit_runtime_result(glsd301p_runtime_result_t result,
                                   u8 frame[GLSD301P_CONTROL_FRAME_SIZE])
{
    if (result == GLSD301P_RUNTIME_NO_FRAME) {
        return 1u;
    }
    if (result == GLSD301P_RUNTIME_INVALID_ARGUMENT) {
        return 0u;
    }

    if (!glsd_uart_queue(frame)) {
        u8 off[GLSD301P_CONTROL_FRAME_SIZE];
        (void)glsd301p_runtime_core_latch_fault(&g_runtime, off);
        glsd_sync_zcl_from_runtime();
        return 0u;
    }

    if (result == GLSD301P_RUNTIME_FORCED_OFF) {
        u8 off[GLSD301P_CONTROL_FRAME_SIZE];
        (void)glsd301p_runtime_core_latch_fault(&g_runtime, off);
        (void)glsd301p_uart_transport_offer(&g_uart_transport, off);
        glsd_sync_zcl_from_runtime();
        return 0u;
    }

    glsd_sync_zcl_from_runtime();
    return 1u;
}

static void glsd_cancel_level_transition(void)
{
    g_level_transition.mode = GLSD_LEVEL_IDLE;
    g_remaining_time = 0u;
    if (g_level_timer) {
        TL_ZB_TIMER_CANCEL(&g_level_timer);
    }
}

static u8 glsd_clamp_level(u16 level)
{
    if (level < g_min_level) {
        return g_min_level;
    }
    if (level > g_max_level) {
        return g_max_level;
    }
    return (u8)level;
}

static u8 glsd_apply_level_state(u8 level, u8 with_onoff, u8 direction_up)
{
    u8 frame[GLSD301P_CONTROL_FRAME_SIZE];
    bool output = g_runtime.logical_output_enabled;

    /* 0xFF is Zigbee's reserved/unknown Level value. Never normalize it into
     * an energizing value; an impossible internal state fails closed. */
    if (level == GLSD301P_ZCL_LEVEL_UNKNOWN) {
        (void)glsd301p_runtime_core_latch_fault(&g_runtime, frame);
        (void)glsd_uart_queue(frame);
        glsd_sync_zcl_from_runtime();
        return 0u;
    }

    level = glsd_clamp_level(level);
    if (with_onoff) {
        if (direction_up || level > g_min_level) {
            output = true;
        } else if (level <= g_min_level) {
            output = false;
        }
    }

    return glsd_emit_runtime_result(
        glsd301p_runtime_core_apply_state(&g_runtime, output, level,
                                          g_min_level, false, frame), frame);
}

static s32 glsd_level_timer_cb(void *arg)
{
    u8 next = g_current_level;
    (void)arg;

    if (g_level_transition.mode == GLSD_LEVEL_IDLE ||
        !glsd301p_runtime_core_is_ready(&g_runtime)) {
        g_level_timer = NULL;
        g_remaining_time = 0u;
        return -1;
    }

    if (g_level_transition.mode == GLSD_LEVEL_TARGET) {
        if (g_current_level == g_level_transition.target) {
            g_level_timer = NULL;
            g_remaining_time = 0u;
            g_level_transition.mode = GLSD_LEVEL_IDLE;
            return -1;
        }

        if (g_remaining_time <= 1u) {
            next = g_level_transition.target;
            g_remaining_time = 0u;
        } else {
            u16 diff = (g_current_level < g_level_transition.target)
                           ? (u16)(g_level_transition.target - g_current_level)
                           : (u16)(g_current_level - g_level_transition.target);
            u16 step = (u16)((diff + g_remaining_time - 1u) / g_remaining_time);
            if (step == 0u) {
                step = 1u;
            }
            next = (g_current_level < g_level_transition.target)
                       ? glsd_clamp_level((u16)g_current_level + step)
                       : glsd_clamp_level((u16)g_current_level - step);
            g_remaining_time--;
        }

        if (!glsd_apply_level_state(next, g_level_transition.with_onoff,
                                    next >= g_current_level)) {
            g_level_timer = NULL;
            g_level_transition.mode = GLSD_LEVEL_IDLE;
            return -1;
        }

        if (next == g_level_transition.target) {
            g_level_timer = NULL;
            g_remaining_time = 0u;
            g_level_transition.mode = GLSD_LEVEL_IDLE;
            return -1;
        }
        return 0;
    }

    g_level_transition.rate_accum_tenths += g_level_transition.rate;
    {
        u8 delta = (u8)(g_level_transition.rate_accum_tenths / 10u);
        g_level_transition.rate_accum_tenths %= 10u;
        if (delta == 0u) {
            return 0;
        }

        if (g_level_transition.direction_up) {
            next = glsd_clamp_level((u16)g_current_level + delta);
        } else {
            next = (g_current_level > delta)
                       ? glsd_clamp_level((u16)g_current_level - delta)
                       : g_min_level;
        }
    }

    if (!glsd_apply_level_state(next, g_level_transition.with_onoff,
                                g_level_transition.direction_up)) {
        g_level_timer = NULL;
        g_level_transition.mode = GLSD_LEVEL_IDLE;
        return -1;
    }

    if ((g_level_transition.direction_up && next >= g_max_level) ||
        (!g_level_transition.direction_up && next <= g_min_level)) {
        g_level_timer = NULL;
        g_level_transition.mode = GLSD_LEVEL_IDLE;
        g_remaining_time = 0u;
        return -1;
    }

    return 0;
}

static void glsd_start_target_transition(u8 target, u16 transition_time, u8 with_onoff)
{
    glsd_cancel_level_transition();
    target = glsd_clamp_level(target);

    if (transition_time == 0u || transition_time == 0xFFFFu ||
        target == g_current_level) {
        (void)glsd_apply_level_state(target, with_onoff, target >= g_current_level);
        return;
    }

    g_level_transition.mode = GLSD_LEVEL_TARGET;
    g_level_transition.target = target;
    g_level_transition.with_onoff = with_onoff;
    g_remaining_time = transition_time;
    g_level_timer = TL_ZB_TIMER_SCHEDULE(glsd_level_timer_cb, NULL,
                                          GLSD301P_LEVEL_TICK_MS);
}

static void glsd_start_move(u8 direction_up, u8 rate, u8 with_onoff)
{
    glsd_cancel_level_transition();
    if (rate == 0u) {
        return;
    }

    g_level_transition.mode = GLSD_LEVEL_MOVE;
    g_level_transition.direction_up = direction_up;
    g_level_transition.rate = rate;
    g_level_transition.rate_accum_tenths = 0u;
    g_level_transition.with_onoff = with_onoff;
    g_remaining_time = 0xFFFFu;

    if (with_onoff && direction_up && !g_runtime.logical_output_enabled) {
        (void)glsd_apply_level_state(g_current_level, 1u, 1u);
    }

    g_level_timer = TL_ZB_TIMER_SCHEDULE(glsd_level_timer_cb, NULL,
                                          GLSD301P_LEVEL_TICK_MS);
}

static status_t glsd_onoff_cb(zclIncomingAddrInfo_t *addr, u8 cmd_id, void *payload)
{
    bool requested;
    u8 frame[GLSD301P_CONTROL_FRAME_SIZE];
    (void)payload;

    if (addr == NULL || addr->dstEp != GLSD301P_ENDPOINT) {
        return ZCL_STA_INVALID_FIELD;
    }

    glsd_cancel_level_transition();

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

    if (!glsd_emit_runtime_result(
            glsd301p_runtime_core_apply_state(&g_runtime, requested,
                                              g_current_level, g_min_level,
                                              false, frame), frame)) {
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
        glsd_start_target_transition(cmd->level, cmd->transitionTime,
                                     cmd_id == ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF);
        return ZCL_STA_SUCCESS;
    }
    case ZCL_CMD_LEVEL_STEP:
    case ZCL_CMD_LEVEL_STEP_WITH_ON_OFF: {
        step_t *cmd = (step_t *)payload;
        u16 target = g_current_level;
        if (cmd == NULL) {
            return ZCL_STA_INVALID_FIELD;
        }
        if (cmd->stepMode == LEVEL_STEP_UP) {
            target = (u16)(target + cmd->stepSize);
        } else {
            target = (target > cmd->stepSize) ? (u16)(target - cmd->stepSize) : g_min_level;
        }
        glsd_start_target_transition(glsd_clamp_level(target), cmd->transitionTime,
                                     cmd_id == ZCL_CMD_LEVEL_STEP_WITH_ON_OFF);
        return ZCL_STA_SUCCESS;
    }
    case ZCL_CMD_LEVEL_MOVE:
    case ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF: {
        move_t *cmd = (move_t *)payload;
        if (cmd == NULL) {
            return ZCL_STA_INVALID_FIELD;
        }
        glsd_start_move(cmd->moveMode == LEVEL_MOVE_UP, cmd->rate,
                        cmd_id == ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF);
        return ZCL_STA_SUCCESS;
    }
    case ZCL_CMD_LEVEL_STOP:
    case ZCL_CMD_LEVEL_STOP_WITH_ON_OFF:
        glsd_cancel_level_transition();
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

static s32 glsd_io_timer_cb(void *arg)
{
    u8 frame[GLSD301P_CONTROL_FRAME_SIZE];
    glsd301p_runtime_result_t result;
    bool push_took_control = false;
    (void)arg;

    /* Sample both physical inputs before touching the UART transport. */
    result = glsd301p_runtime_core_poll_push_ex(&g_runtime,
                                                drv_gpio_read(GPIO_PC2),
                                                &push_took_control, frame);
    if (push_took_control) {
        /* Physical PUSH supersedes every remote target or continuous move. */
        glsd_cancel_level_transition();
    }
    if (result != GLSD301P_RUNTIME_NO_FRAME) {
        (void)glsd_emit_runtime_result(result, frame);
    }

    result = glsd301p_runtime_core_poll_pb4(&g_runtime,
                                            drv_gpio_read(GPIO_PB4), frame);
    if (result != GLSD301P_RUNTIME_NO_FRAME) {
        (void)glsd_emit_runtime_result(result, frame);
    }

    glsd_uart_service_pending();
    return 0;
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
    u8 frame[GLSD301P_CONTROL_FRAME_SIZE];

    glsd301p_runtime_core_init(&g_runtime);
    glsd301p_uart_transport_init(&g_uart_transport);
    g_uart_pending_ms = 0u;
    g_uart_transport_fault_latched = 0u;

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
        (void)glsd301p_runtime_core_latch_fault(&g_runtime, frame);
        return 0u;
    }

    /* user_init runs before global IRQ enable. Emit exactly ONE UART frame here:
     * the confirmed electrical OFF vector. Do not perform a second TX until
     * user_init has returned and IRQs can retire the DMA transfer. */
    if (glsd301p_runtime_core_boot_off(&g_runtime, frame) !=
            GLSD301P_RUNTIME_FRAME_READY ||
        !glsd_uart_send_boot_off_blocking(frame)) {
        (void)glsd301p_runtime_core_latch_fault(&g_runtime, frame);
        return 0u;
    }

    /* Arm known logical OFF without another pre-IRQ UART transmission. The
     * already-sent boot OFF frame is the physical state represented here. */
    if (glsd301p_runtime_core_restore_state(&g_runtime, false,
                                            GLSD301P_MAX_LEVEL,
                                            GLSD301P_MIN_LEVEL,
                                            false, frame) !=
        GLSD301P_RUNTIME_FRAME_READY) {
        (void)glsd301p_runtime_core_latch_fault(&g_runtime, frame);
        return 0u;
    }

    glsd_sync_zcl_from_runtime();
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

    (void)TL_ZB_TIMER_SCHEDULE(glsd_io_timer_cb, NULL, GLSD301P_IO_POLL_MS);
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
