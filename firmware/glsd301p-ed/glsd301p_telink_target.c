#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "zdo_api.h"
#include "tl_zb_mac.h"
#include "bdb.h"
#include "ota.h"
#include "drv_uart.h"
#include "drv_gpio.h"

#include "glsd301p_bdb_adapter.h"
#include "glsd301p_control.h"
#include "glsd301p_health.h"
#include "glsd301p_hw_io.h"
#include "glsd301p_rejoin.h"
#include "glsd301p_runtime_core.h"
#include "glsd301p_stack_nv.h"
#include "glsd301p_startup.h"
#include "glsd301p_timebase.h"
#include "glsd301p_timer_events.h"
#include "glsd301p_uart_service.h"
#include "glsd301p_uart_transport.h"
#include "glsd301p_zcl_commands.h"

#define GLSD301P_MANUFACTURER_CODE            0x124Fu
#define GLSD301P_IMAGE_TYPE                   0x1416u
#define GLSD301P_MIN_LEVEL                    0x02u
#define GLSD301P_MAX_LEVEL                    0xFEu
#define GLSD301P_UART_RX_BUFFER_SIZE          16u
#define GLSD301P_UART_TX_DMA_SIZE              (4u + GLSD301P_CONTROL_FRAME_SIZE)
/* Periodic OTA server query once joined (u16 seconds; 12 h). Updates are
 * still only delivered when the server chooses to answer with an image. */
#define GLSD301P_OTA_QUERY_INTERVAL_S          43200u
/* Delay between a gesture-initiated own-Leave confirm and the reboot. */
#define GLSD301P_LEAVE_REBOOT_DELAY_S          2u
/* Fallback reboot after a gesture-initiated factory reset while joined
 * (the Leave confirm normally reboots sooner). */
#define GLSD301P_GESTURE_REBOOT_DELAY_S        10u
/* Reboot after a gesture when no Leave can be sent (not joined). */
#define GLSD301P_GESTURE_LOCAL_REBOOT_DELAY_S  1u
/* Upper bound on extra 1 s ticks a pending reboot waits for UART idle. */
#define GLSD301P_REBOOT_UART_WAIT_MAX_S        3u
/* Private item in NV_MODULE_APP naming the firmware line that owns the
 * stack's NV modules (see glsd301p_stack_nv.h). */
#define GLSD301P_NV_ITEM_STACK_EPOCH           0xE5u
#define GLSD301P_NV_ITEM_STARTUP               0xE6u
/* Routine saves must NEVER rotate the epoch marker's APP module. */
#define GLSD301P_STARTUP_NV_MODULE              NV_MODULE_ZCL
/* How long an associated device waits for its network key. The SDK default
 * of 2 s leaves no margin for a multi-hop trust-centre round trip. */
#define GLSD301P_NETWORK_KEY_WAIT_MS           10000u

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
static glsd301p_startup_t g_startup;
typedef char glsd_startup_separate_from_epoch[
    (GLSD301P_STARTUP_NV_MODULE != NV_MODULE_APP) ? 1 : -1];
typedef char glsd_startup_zcl_region[
    (MODULES_START_ADDR(GLSD301P_STARTUP_NV_MODULE) == 0x3A000u) ? 1 : -1];
typedef char glsd_startup_voltage_guard_enabled[(VOLTAGE_DETECT_ENABLE == 1) ? 1 : -1];
typedef char glsd_startup_item_is_private[
    (GLSD301P_NV_ITEM_STARTUP > NV_ITEM_APS_BINDING_TABLE_V2 &&
     GLSD301P_NV_ITEM_STARTUP < NV_ITEM_ID_MAX) ? 1 : -1];
static u8 g_uart_rx_buf[GLSD301P_UART_RX_BUFFER_SIZE] __attribute__((aligned(4)));
static u8 g_uart_tx_dma[GLSD301P_UART_TX_DMA_SIZE] __attribute__((aligned(4)));
static u8 g_uart_online;
static u8 g_zcl_online;

/* Independent implementation identity; hardware/stack lineage remains explicit. */
static u8 g_basic_zcl_version = 0x03u;
static u8 g_basic_app_version = 0x0Au;
static u8 g_basic_stack_version = 0x02u;
static u8 g_basic_hw_version = 0x02u;
static u8 g_basic_power_source = POWER_SOURCE_MAINS_1_PHASE;
static u8 g_basic_device_enabled = TRUE;
static u8 g_basic_mfr_name[] = {8,'a','n','a','l','i','e','n','x'};
static u8 g_basic_model_id[] = {13,'G','L','-','S','D','-','3','0','1','P','-','E','D'};
static u8 g_basic_date_code[] = {8,'2','0','2','6','1','1','1','3'};
static u8 g_basic_sw_build_id[] = {11,'G','L','S','D','-','E','D','-','0','0','9'};

/*
 * RAM-only v2 health snapshot body for Basic:0xFF10. The length prefix is
 * constant; the 1 s owned event refreshes the 48 payload bytes in place.
 * Read-only, never reported, never persisted.
 */
static u8 g_basic_health[1u + GLSD301P_HEALTH_SIZE] = {GLSD301P_HEALTH_SIZE};
static glsd301p_health_t g_health;

/* SDK ev.c exception record: [0] line, [1] code. Fail-closed to zero. */
extern volatile u16 T_evtExcept[4];

static u16 g_identify_time;
static u8 g_group_name_support;

static u8 g_onoff = ZCL_ONOFF_STATUS_OFF;
static u8 g_global_scene_control = TRUE;
static u16 g_on_time;
static u16 g_off_wait_time;
/*
 * Boot remains locked OFF until UART completion and timer readiness.
 * StartUpOnOff selects the subsequent guarded state; level restores the
 * last committed brightness (StartUpCurrentLevel=255, read-only).
 */

static u8 g_min_level = GLSD301P_MIN_LEVEL;
static u8 g_max_level = GLSD301P_MAX_LEVEL;
static u8 g_level_options;
static u8 g_startup_current_level = 255u;

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
    {GLSD301P_HEALTH_ATTR_ID, ZCL_DATA_TYPE_OCTET_STR, ACCESS_CONTROL_READ, g_basic_health},
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
    {ZCL_ATTRID_START_UP_ONOFF, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, &g_startup.policy},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&zcl_attr_global_clusterRevision},
};

static const zclAttrInfo_t g_level_attr_table[] = {
    {ZCL_ATTRID_LEVEL_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8 *)&g_level_state.current_level},
    {ZCL_ATTRID_LEVEL_REMAINING_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ, (u8 *)&g_level_state.remaining_time},
    {ZCL_ATTRID_LEVEL_MIN_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&g_min_level},
    {ZCL_ATTRID_LEVEL_MAX_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&g_max_level},
    {ZCL_ATTRID_LEVEL_OPTIONS, ZCL_DATA_TYPE_BITMAP8, ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&g_level_options},
    {ZCL_ATTRID_LEVEL_START_UP_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, (u8 *)&g_startup_current_level},
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

/*
 * Thin ZCL adapters: NULL guard plus endpoint forward into the shared
 * command policy (glsd301p_zcl_commands.c), which is also driven by the
 * hosted harness through the real pinned SDK parsers.
 */
static glsd301p_zcl_ctx_t g_zcl_ctx = {
    GLSD301P_ENDPOINT,
    GLSD301P_MIN_LEVEL,
    &g_runtime,
    &g_control,
    &g_level_state,
    &g_on_time,
    &g_off_wait_time,
};

static status_t glsd_onoff_cb(zclIncomingAddrInfo_t *addr, u8 cmd_id, void *payload)
{
    if (addr == NULL) {
        return ZCL_STA_INVALID_FIELD;
    }
    return glsd301p_zcl_onoff_command(&g_zcl_ctx, addr->dstEp, cmd_id, payload);
}

static status_t glsd_level_cb(zclIncomingAddrInfo_t *addr, u8 cmd_id, void *payload)
{
    if (addr == NULL) {
        return ZCL_STA_INVALID_FIELD;
    }
    return glsd301p_zcl_level_command(&g_zcl_ctx, addr->dstEp, cmd_id, payload);
}

/*
 * R17/R18 Identify adapter. All decisions and state changes run in the
 * shared glsd301p_identify_cluster_command() (the same code the hosted
 * fixture drives): Identify lands on the shared IdentifyTime store and
 * arms the countdown (0 stops); every Trigger Effect is rejected before
 * any state change. Query is answered by the SDK from the same store,
 * so no frame code is needed here.
 */
static status_t glsd_identify_cb(zclIncomingAddrInfo_t *addr, u8 cmd_id, void *payload)
{
    zcl_identify_cmdPayload_t *cmd;

    if (addr == NULL || payload == NULL) {
        return ZCL_STA_INVALID_FIELD;
    }
    cmd = (zcl_identify_cmdPayload_t *)payload;
    if (glsd301p_identify_cluster_command(
            &g_control.identify, &g_identify_time, addr->dstEp,
            GLSD301P_ENDPOINT, cmd_id, cmd->identify.identifyTime,
            cmd->triggerEffect.effectId, cmd->triggerEffect.effectVariant,
            glsd301p_timebase_now_ms()) == GLSD301P_IDENTIFY_CMD_OK) {
        return ZCL_STA_SUCCESS;
    }
    return ZCL_STA_INVALID_FIELD;
}

/* Set by the OTA callback, consumed by the 1 s maintenance tick. */
static bool g_ota_requery_pending;

static void glsd_arm_reboot(u8 delay_s);

static void glsd_ota_event(u8 evt, u8 status)
{
    if (evt != OTA_EVT_COMPLETE) {
        return;
    }
    if (status == ZCL_STA_SUCCESS) {
        ota_mcuReboot();
        /*
         * Still here: the SDK refused to switch images (new image invalid
         * or boot-flag write failed). Reboot onto the current valid image
         * so no RAM resume state survives; the OTA NV module was already
         * reset, so the next server-initiated transfer starts clean.
         */
        glsd_arm_reboot(1u);
        return;
    }
    /*
     * Failed/aborted transfer: the shared otaTimer was taken over by the
     * transfer and is gone (or is the very timer whose callback is running
     * now, e.g. the IEEE-address wait, where ota_queryStart would be a
     * no-op). Re-arm the periodic query from the next maintenance tick.
     * This keeps the client's own periodic query alive; with Zigbee2MQTT
     * an aborted recovery is retried by re-sending the same request.
     */
    g_ota_requery_pending = true;
}

static ota_callBack_t g_ota_cb = {glsd_ota_event};

/*
 * Application-owned rejoin attempt ownership. The stack owns backoff for an
 * accepted attempt; this layer owns single-outstanding-attempt gating, the
 * 5 s paced retry after a rejected start, and joined-state reconciliation.
 * Factory-new devices never rejoin; they commission through the steering
 * path on the 1 s maintenance tick (see glsd301p_rejoin.h), which also
 * kicks a rejoin for stranded network state. Polling policy is never
 * touched here.
 */
static glsd301p_rejoin_t g_rejoin;

/*
 * Main-loop maintenance owned by the 1 s event: factory-new steering,
 * reset-gesture consumption and deferred reboot. Pending reboot countdown
 * in seconds; 0 means none.
 */
static u8 g_reboot_countdown_s;
static u8 g_reboot_uart_waits;
/* Reset gesture accepted: the pending reboot must come up factory new. */
static bool g_reset_to_fn;

static void glsd_arm_reboot(u8 delay_s)
{
    if (g_reboot_countdown_s == 0u || delay_s < g_reboot_countdown_s) {
        g_reboot_countdown_s = delay_s;
    }
}

/*
 * Stack NV seam for the shared clean-start policy. Index 0 is the module
 * holding the marker item; NV_MODULE_NWK_FRAME_COUNT is deliberately not
 * listed, so the outgoing NWK frame counter survives every wipe.
 */
static const u8 g_stack_nv_modules[] = {
    NV_MODULE_APP,
    NV_MODULE_ZB_INFO,
    NV_MODULE_ADDRESS_TABLE,
    NV_MODULE_APS,
    NV_MODULE_ZCL,
    NV_MODULE_OTA,
    NV_MODULE_KEYPAIR,
};
#define GLSD301P_STACK_NV_MODULE_COUNT \
    ((u8)(sizeof(g_stack_nv_modules) / sizeof(g_stack_nv_modules[0])))

/*
 * The wipe must hit the addresses where the 2026-11-12 SWire dump of the
 * first migrated unit found the vendor's records (512 KiB map, module id in
 * every sector header). Compile-time only: any SDK/flash-map drift fails the
 * build instead of silently wiping the wrong flash. No code is emitted.
 */
typedef char glsd_nv_zb_info_at_0x34000[
    (MODULES_START_ADDR(NV_MODULE_ZB_INFO) == 0x34000u) ? 1 : -1];
typedef char glsd_nv_address_table_at_0x36000[
    (MODULES_START_ADDR(NV_MODULE_ADDRESS_TABLE) == 0x36000u) ? 1 : -1];
typedef char glsd_nv_aps_at_0x38000[
    (MODULES_START_ADDR(NV_MODULE_APS) == 0x38000u) ? 1 : -1];
typedef char glsd_nv_zcl_at_0x3a000[
    (MODULES_START_ADDR(NV_MODULE_ZCL) == 0x3A000u) ? 1 : -1];
typedef char glsd_nv_frame_count_at_0x3c000_kept[
    (MODULES_START_ADDR(NV_MODULE_NWK_FRAME_COUNT) == 0x3C000u) ? 1 : -1];
typedef char glsd_nv_ota_at_0x3e000[
    (MODULES_START_ADDR(NV_MODULE_OTA) == 0x3E000u) ? 1 : -1];
typedef char glsd_nv_app_at_0x7a000[
    (MODULES_START_ADDR(NV_MODULE_APP) == 0x7A000u) ? 1 : -1];
typedef char glsd_nv_keypair_at_0x7c000[
    (MODULES_START_ADDR(NV_MODULE_KEYPAIR) == 0x7C000u) ? 1 : -1];
typedef char glsd_nv_keypair_ends_at_0x80000[
    (MODULES_START_ADDR(NV_MODULE_KEYPAIR) +
     MODULE_SECTOR_NUM * NV_SECTOR_SIZE(NV_MODULE_KEYPAIR) == 0x80000u) ? 1 : -1];
/* Nothing the wipe touches may reach the MAC/calibration sectors. */
typedef char glsd_nv_stack_below_identity[
    (MODULES_START_ADDR(NV_MODULE_OTA) +
     MODULE_SECTOR_NUM * NV_SECTOR_SIZE(NV_MODULE_OTA) <= 0x40000u) ? 1 : -1];

static bool glsd_stack_nv_marker_read(
    uint8_t out[GLSD301P_STACK_NV_MARKER_SIZE])
{
    return nv_flashReadNew(1, NV_MODULE_APP, GLSD301P_NV_ITEM_STACK_EPOCH,
                           GLSD301P_STACK_NV_MARKER_SIZE, out) == NV_SUCC;
}

static bool glsd_stack_nv_marker_write(
    const uint8_t marker[GLSD301P_STACK_NV_MARKER_SIZE])
{
    u8 copy[GLSD301P_STACK_NV_MARKER_SIZE];

    memcpy(copy, marker, sizeof(copy));
    return nv_flashWriteNew(1, NV_MODULE_APP, GLSD301P_NV_ITEM_STACK_EPOCH,
                            sizeof(copy), copy) == NV_SUCC;
}

static bool glsd_stack_nv_module_erase(uint8_t index)
{
    if (index >= GLSD301P_STACK_NV_MODULE_COUNT) {
        return false;
    }
    return nv_resetModule(g_stack_nv_modules[index]) == NV_SUCC;
}

/* Reads the whole module back: a sector erase that a write-protected or
 * failing flash silently ignored must not count as clean. */
static bool glsd_stack_nv_module_blank(uint8_t index)
{
    u8 chunk[32];
    u32 addr;
    u32 end;
    u8 id;
    u8 i;

    if (index >= GLSD301P_STACK_NV_MODULE_COUNT) {
        return false;
    }
    id = g_stack_nv_modules[index];
    addr = MODULES_START_ADDR(id);
    end = addr + (u32)MODULE_SECTOR_NUM * NV_SECTOR_SIZE(id);
#if (MODULE_WATCHDOG_ENABLE)
    drv_wd_clear();
#endif
    for (; addr < end; addr += sizeof(chunk)) {
        flash_read(addr, sizeof(chunk), chunk);
        for (i = 0u; i < sizeof(chunk); i++) {
            if (chunk[i] != 0xFFu) {
                return false;
            }
        }
    }
    return true;
}

static const glsd301p_stack_nv_ops_t g_stack_nv_ops = {
    glsd_stack_nv_marker_read,
    glsd_stack_nv_marker_write,
    glsd_stack_nv_module_erase,
    glsd_stack_nv_module_blank,
};

static void glsd_note_joined(void)
{
    /* Idempotent: the SDK only arms its query timer when none exists. */
    ota_queryStart(GLSD301P_OTA_QUERY_INTERVAL_S);
}

/* Wrap-safe oldest-stamp tracking for the pending/in-flight UART age. */
static void glsd_health_track_oldest(bool *has_oldest, uint32_t *oldest,
                                     uint32_t stamp, uint32_t now_ms)
{
    if (!*has_oldest ||
        glsd301p_timebase_age_ms(stamp, now_ms) >
            glsd301p_timebase_age_ms(*oldest, now_ms)) {
        *oldest = stamp;
        *has_oldest = true;
    }
}

static void glsd_health_snapshot_now(uint32_t now_ms)
{
    glsd301p_health_inputs_t in;
    bool has_oldest = false;
    uint32_t oldest = 0u;
    uint32_t stamp;

    in.runtime_ready = glsd301p_runtime_core_is_ready(&g_runtime);
    in.fault_latched = g_runtime.output_guard.fault_latched;
    in.mac_rx_on_idle = (ZB_PIB_RX_ON_WHEN_IDLE() != 0u);
    in.off_pending = g_uart_transport.off_pending;
    in.normal_pending = g_uart_transport.normal_pending;
    in.uart_busy = glsd301p_hw_uart_busy();
    in.io_registered = glsd301p_timer_io_registered();
    in.boot_off_complete = g_uart_service.boot_complete;
    in.uptime_ms = now_ms;
    in.last_io_age_ms = g_control.io_serviced_once
        ? glsd301p_timebase_age_ms(g_control.io_last_ms, now_ms)
        : 0xFFFFFFFFu;
    if (glsd301p_uart_transport_oldest_ms(&g_uart_transport, &stamp)) {
        glsd_health_track_oldest(&has_oldest, &oldest, stamp, now_ms);
    }
    if (g_uart_service.inflight_active) {
        glsd_health_track_oldest(&has_oldest, &oldest,
                                 g_uart_service.inflight_since_ms, now_ms);
    }
    if (g_uart_service.boot_pending && !g_uart_service.boot_complete) {
        glsd_health_track_oldest(&has_oldest, &oldest,
                                 g_uart_service.boot_since_ms, now_ms);
    }
    in.uart_age_ms =
        has_oldest ? glsd301p_timebase_age_ms(oldest, now_ms) : 0u;
    in.io_max_gap_ms = g_control.io_max_gap_ms;
    in.uart_deadline_faults =
        glsd301p_uart_service_deadline_faults(&g_uart_service);
    in.timer_reg_faults = glsd301p_timer_reg_faults();
    in.parent_losses = g_rejoin.parent_losses;
    in.rejoin_starts = g_rejoin.starts;
    in.rejoin_failures = g_rejoin.failures;
    in.rejoin_successes = g_rejoin.successes;
    in.connectivity = glsd301p_rejoin_health_byte(&g_rejoin);
    in.exc_line = T_evtExcept[0];
    in.exc_code = (uint8_t)(T_evtExcept[1] & 0xFFu);

    glsd301p_health_snapshot(&g_health, &in, g_basic_health + 1u);
}

static void glsd_rejoin_start_attempt(bool from_retry_fire);

static bool glsd_startup_write(const uint8_t *record, void *context)
{
    uint8_t check[GLSD301P_STARTUP_RECORD_SIZE];
    (void)context;
    /* SDK owns journal/sector rotation. Acknowledged application durability
     * requires a readback; a failed write never advances our committed state. */
    if (drv_get_adc_data() < BATTERY_SAFETY_THRESHOLD) return false;
    if (nv_flashWriteNew(1, GLSD301P_STARTUP_NV_MODULE, GLSD301P_NV_ITEM_STARTUP,
                         GLSD301P_STARTUP_RECORD_SIZE, (u8 *)record) != NV_SUCC)
        return false;
    return nv_flashReadNew(1, GLSD301P_STARTUP_NV_MODULE, GLSD301P_NV_ITEM_STARTUP,
                           sizeof(check), check) == NV_SUCC &&
           glsd301p_startup_record_valid(check) &&
           memcmp(record, check, sizeof(check)) == 0;
}

static void glsd_startup_maintenance(void)
{
    const bool safe = glsd301p_runtime_core_is_ready(&g_runtime) &&
        !g_uart_service.fault_latched && !g_uart_service.inflight_active &&
        !glsd301p_hw_uart_busy() &&
        !glsd301p_uart_transport_has_pending(&g_uart_transport) &&
        g_level_state.mode == GLSD301P_LEVEL_IDLE;
    (void)glsd301p_startup_poll(&g_startup, g_runtime.logical_output_enabled,
        g_runtime.current_level, glsd301p_timebase_now_ms(), safe,
        glsd_startup_write, NULL);
}

static void glsd_maintenance_tick(void)
{
    bool factory_new = zb_isDeviceFactoryNew() ? true : false;
    bool joined = zb_isDeviceJoinedNwk() ? true : false;
    glsd301p_bdb_action_t action;

    glsd_startup_maintenance();

    if (g_reboot_countdown_s != 0u) {
        if (g_reboot_countdown_s > 1u) {
            g_reboot_countdown_s--;
            return;
        }
        /*
         * Never cut a power-stage frame mid-DMA; wait a bounded number of
         * extra ticks for the UART to go idle. Clean restart: boot re-runs
         * the locked-OFF sequence first.
         */
        if (glsd301p_hw_uart_busy() &&
            g_reboot_uart_waits < GLSD301P_REBOOT_UART_WAIT_MAX_S) {
            g_reboot_uart_waits++;
            return;
        }
        if (g_reset_to_fn) {
            /*
             * The gesture promised a factory-new boot with empty stack
             * storage. Wipe every stack NV module unconditionally (the
             * outgoing NWK frame counter is kept): "already factory-new"
             * says nothing about leftover records or full sectors, which
             * is exactly the state a unit that cannot join may be in. A
             * failed wipe leaves no marker, so the next boot repeats it.
             */
            (void)glsd301p_stack_nv_ensure_clean(
                &g_stack_nv_ops, GLSD301P_STACK_NV_MODULE_COUNT, true);
        }
        zb_resetDevice();
        return;
    }

    if (glsd301p_control_take_network_reset_request(&g_control)) {
        /*
         * Always end in a clean factory-new boot, also on a device that
         * already counts as factory-new (the boot steers again at once).
         * Joined: broadcast the Leave first (its confirm reboots sooner).
         * Not joined: no Leave can be delivered, reboot right away. The
         * reboot path wipes the stack NV either way.
         */
        g_reset_to_fn = true;
        glsd301p_timer_retry_stop();
        if (joined) {
            zb_factoryReset();
            glsd_arm_reboot(GLSD301P_GESTURE_REBOOT_DELAY_S);
        } else {
            glsd_arm_reboot(GLSD301P_GESTURE_LOCAL_REBOOT_DELAY_S);
        }
        return;
    }

    if (g_ota_requery_pending) {
        g_ota_requery_pending = false;
        ota_queryStart(GLSD301P_OTA_QUERY_INTERVAL_S);
    }

    action = glsd301p_bdb_tick(&g_rejoin, joined, factory_new);
    if (action.stop_retry) {
        glsd301p_timer_retry_stop();
    }
    if (action.joined) {
        glsd301p_health_note_network(&g_health, GLSD301P_HEALTH_NET_JOINED);
        glsd_note_joined();
    }
    if (action.start_attempt) {
        glsd_rejoin_start_attempt(false);
    }
    if (action.start_steer) {
        glsd301p_rejoin_note_steer_start_result(
            &g_rejoin, bdb_networkSteerStart() == BDB_STATE_IDLE);
    }
}

static int glsd_health_cb(void *data)
{
    (void)data;
    glsd_health_snapshot_now(glsd301p_timebase_now_ms());
    glsd_maintenance_tick();
    return 0;
}

/*
 * The 1 s event carries essential recovery behaviour (steering, gesture,
 * reboot), not just diagnostics. If its registration ever failed, retry it
 * from every BDB callback; init always reports at least once.
 */
static void glsd_ensure_maintenance_timer(void)
{
    if (!glsd301p_timer_health_registered()) {
        (void)glsd301p_timer_health_start(glsd_health_cb, NULL);
    }
}

static int glsd_retry_cb(void *data);

static void glsd_rejoin_start_attempt(bool from_retry_fire)
{
    u8 rc;
    bool accepted;

    if (g_reset_to_fn) {
        /* Reset gesture accepted: never rejoin during the reboot
         * countdown (no NV write, no brief reappearance). */
        return;
    }
    /*
     * Documented contract (zb_api.h): RET_ILLEGAL_REQUEST or zdo_status_t.
     * Anything but ZDO_SUCCESS is a rejected start and takes the owned 5 s
     * paced retry; accepted starts run under stack-owned backoff.
     */
    rc = zb_rejoinReqWithBackOff(zb_apsChannelMaskGet(), g_bdbAttrs.scanDuration);
    accepted = (rc == ZDO_SUCCESS);
    glsd301p_rejoin_note_start_result(&g_rejoin, accepted);
    if (from_retry_fire) {
        /*
         * Inside the one-shot fire: the return value drives re-arm vs
         * unregister, so no timer call is made here at all.
         */
        return;
    }
    if (accepted) {
        glsd301p_timer_retry_stop();
    } else {
        (void)glsd301p_timer_retry_start_oneshot(glsd_retry_cb, NULL);
    }
}

static int glsd_retry_cb(void *data)
{
    (void)data;
    if (glsd301p_rejoin_note_retry_fire(&g_rejoin)) {
        glsd_rejoin_start_attempt(true);
    }
    /* A stale fire (not pending) self-heals by unregistering. */
    return (glsd301p_rejoin_state(&g_rejoin) == GLSD301P_REJOIN_RETRY_PENDING)
        ? (int)GLSD301P_TIMER_RETRY_MS
        : -1;
}

/*
 * Health network mapping (advisory diagnostics): JOINED only on observed
 * joined evidence, JOINING on any other BDB activity, DISCONNECTED only
 * from boot until the first BDB event.
 */
static void glsd_bdb_init_cb(u8 status, u8 joined_network)
{
    bool factory_new = zb_isDeviceFactoryNew() ? true : false;
    glsd301p_bdb_action_t action;

    glsd_ensure_maintenance_timer();
    glsd301p_health_note_bdb_status(&g_health, status);
    glsd301p_health_note_network(
        &g_health,
        (uint8_t)((status == BDB_INIT_STATUS_SUCCESS && joined_network != 0u)
                      ? GLSD301P_HEALTH_NET_JOINED
                      : GLSD301P_HEALTH_NET_JOINING));
    if (status == BDB_INIT_STATUS_SUCCESS) {
        action = glsd301p_bdb_handle_event(
            &g_rejoin,
            joined_network != 0u ? GLSD301P_BDB_INIT_JOINED
                                 : GLSD301P_BDB_INIT_NOT_JOINED,
            factory_new);
        if (action.stop_retry) {
            glsd301p_timer_retry_stop();
        }
        if (action.joined) {
            glsd_note_joined();
        }
        return;
    }
    action = glsd301p_bdb_handle_event(&g_rejoin,
                                       GLSD301P_BDB_INIT_FAILURE,
                                       factory_new);
    if (action.start_attempt) {
        glsd_rejoin_start_attempt(false);
    }
}

static void glsd_bdb_commission_cb(u8 status, void *arg)
{
    bool factory_new = zb_isDeviceFactoryNew() ? true : false;
    bool joined = zb_isDeviceJoinedNwk() ? true : false;
    glsd301p_bdb_action_t action;

    (void)arg;
    glsd_ensure_maintenance_timer();
    glsd301p_health_note_bdb_status(&g_health, status);
    glsd301p_health_note_network(
        &g_health,
        (uint8_t)((status == BDB_COMMISSION_STA_SUCCESS && joined)
                      ? GLSD301P_HEALTH_NET_JOINED
                      : GLSD301P_HEALTH_NET_JOINING));
    if (status == BDB_COMMISSION_STA_SUCCESS) {
        action = glsd301p_bdb_handle_event(
            &g_rejoin,
            joined ? GLSD301P_BDB_COMMISSION_JOINED
                   : GLSD301P_BDB_COMMISSION_NOT_JOINED,
            factory_new);
        if (action.stop_retry) {
            glsd301p_timer_retry_stop();
        }
        if (action.joined) {
            glsd_note_joined();
        }
        return;
    }
    if (status == BDB_COMMISSION_STA_IN_PROGRESS ||
        status == BDB_COMMISSION_STA_FORMATION_DONE) {
        /* Non-terminal / coordinator-only statuses. */
        return;
    }
    if (status == BDB_COMMISSION_STA_PARENT_LOST) {
        action = glsd301p_bdb_handle_event(&g_rejoin,
                                           GLSD301P_BDB_PARENT_LOST,
                                           factory_new);
        if (action.start_attempt) {
            glsd_rejoin_start_attempt(false);
        }
        return;
    }
    if (status == BDB_COMMISSION_STA_REJOIN_FAILURE) {
        action = glsd301p_bdb_handle_event(&g_rejoin,
                                           GLSD301P_BDB_REJOIN_FAILURE,
                                           factory_new);
        if (action.start_attempt) {
            glsd_rejoin_start_attempt(false);
        }
        return;
    }
    /*
     * Every other status is a terminal commissioning failure (NO_NETWORK,
     * TCLK_EX_FAILURE, NOT_PERMITTED, ...). It ends an in-progress
     * steering attempt, which then backs off on the 1 s tick.
     */
    (void)glsd301p_bdb_handle_event(&g_rejoin, GLSD301P_BDB_STEER_FAILURE,
                                    factory_new);
}

/*
 * Own Leave completed. Only a gesture-initiated Leave reboots (cleanly into
 * a factory-new boot; the reboot path clears any NV the stack kept). Every
 * other own Leave -- BDB's own after a failed TC link-key exchange
 * (bdb.c), a coordinator removal, a Leave with rejoin=1 -- needs no action
 * here: the 1 s tick steers a factory-new device (with the RAM backoff
 * intact, which a reboot would lose) and kicks a rejoin for one that kept
 * its network state. Nothing here can leave the device off-network.
 */
static void glsd_leave_cnf_cb(nlme_leave_cnf_t *cnf)
{
    if (cnf == NULL || cnf->status != SUCCESS) {
        return;
    }
    if (g_reset_to_fn) {
        glsd_arm_reboot(GLSD301P_LEAVE_REBOOT_DELAY_S);
        return;
    }
    /* No longer joined: the next join is a new edge (health success count). */
    (void)glsd301p_rejoin_note_joined(&g_rejoin, false);
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
    glsd_leave_cnf_cb,
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
    /* R13: the household tick polls the Identify countdown here. */
    glsd301p_control_identify_bind_store(&g_control, &g_identify_time,
                                         GLSD301P_ENDPOINT);

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

    /*
     * Before the stack reads anything: stack NV left by another firmware
     * line (the vendor Router image this build replaces in place) is erased
     * once and the modules start empty. A failed wipe writes no marker and
     * is repeated on the next boot; the stack still starts either way.
     */
    (void)glsd301p_stack_nv_ensure_clean(
        &g_stack_nv_ops, GLSD301P_STACK_NV_MODULE_COUNT, false);

    {
        uint8_t record[GLSD301P_STARTUP_RECORD_SIZE];
        const bool loaded = nv_flashReadNew(1, GLSD301P_STARTUP_NV_MODULE,
            GLSD301P_NV_ITEM_STARTUP, sizeof(record), record) == NV_SUCC;
        glsd301p_startup_init(&g_startup, loaded ? record : NULL);
        g_control.boot_on = glsd301p_startup_boot_on(&g_startup);
        g_control.boot_level = g_startup.previous_level;
    }

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
    /*
     * One coherent v2 snapshot immediately so Basic:0xFF10 is valid even
     * if the 1 s event never runs, then periodic refresh. The same owned
     * 1 s event carries network maintenance (factory-new steering, reset
     * gesture, deferred reboot). Neither is ever in the output path; a
     * registration failure is counted and retried from every BDB callback
     * (glsd_ensure_maintenance_timer).
     */
    glsd301p_rejoin_init(&g_rejoin);
    g_reboot_countdown_s = 0u;
    g_reboot_uart_waits = 0u;
    g_reset_to_fn = false;
    g_ota_requery_pending = false;
    glsd301p_health_init(&g_health);
    glsd_health_snapshot_now(glsd301p_timebase_now_ms());
    glsd_ensure_maintenance_timer();
    TRANSPORT_NETWORK_KEY_WAIT_TIME = GLSD301P_NETWORK_KEY_WAIT_MS;
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
