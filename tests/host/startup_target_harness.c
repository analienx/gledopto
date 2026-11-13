#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "host_types.h"
#define _attribute_packed_ __attribute__((packed))
#define NV_BASE_ADDRESS 0x34000u
#define NV_BASE_ADDRESS2 0x7a000u
#include "drv_nv.h"
#include "glsd301p_startup.h"
/* TARGET_DEFINES */
#define MODULE_WATCHDOG_ENABLE 0
#define GLSD301P_ENDPOINT 11
#define GLSD301P_MANUFACTURER_CODE 0x124f
#define GLSD301P_STACK_NV_MODULE_COUNT 7
#define GLSD301P_NETWORK_KEY_WAIT_MS 10000u
#define OTA_TYPE_CLIENT 1
#define GLSD301P_LEVEL_IDLE 0
typedef int zdo_appIndCb_t;
typedef int af_simple_descriptor_t;
typedef int zcl_specClusterInfo_t;
static int g_stack_nv_ops,g_zdo_callbacks,g_simple_desc,g_cluster_list[5];
static int g_ota_info,g_ota_cb,g_rejoin,g_health,g_bdb_settings,g_bdb_callbacks;
static bool g_reset_to_fn,g_ota_requery_pending;
static unsigned g_reboot_countdown_s,g_reboot_uart_waits,TRANSPORT_NETWORK_KEY_WAIT_TIME;
static unsigned initialized, reads, writes, observed_module, observed_item;
static bool dma_busy, pending, ready=true, write_fail, write_noop, read_fail, read_corrupt, timer_ok=true;
static unsigned voltage=3300, flash_writes, flash_erases;
static uint8_t stored[GLSD301P_STARTUP_RECORD_SIZE];
static struct {bool boot_on;uint8_t boot_level;} g_control;
static struct {bool logical_output_enabled;uint8_t current_level;} g_runtime;
static struct {bool fault_latched,inflight_active;} g_uart_service;
static struct {int mode;} g_level_state;
static int g_uart_transport,g_zcl_online,zcl_rx_handler,glsd301p_control_io_cb;
static glsd301p_startup_t g_startup;
static bool glsd301p_runtime_core_is_ready(void *p){return ready;}
static bool glsd301p_hw_uart_busy(void){return dma_busy;}
static bool glsd301p_uart_transport_has_pending(void *p){return pending;}
static uint32_t now;
static uint32_t glsd301p_timebase_now_ms(void){return now;}
static u16 drv_get_adc_data(void){return voltage;}
static void flash_write_page(u32 a,u32 n,u8 *p){++flash_writes;}
static void flash_erase_sector(u32 a){++flash_erases;}
nv_sts_t nv_flashWriteNew(u8 single,u16 module,u8 item,u16 n,u8 *p)
{
    ++writes; assert(module==NV_MODULE_ZCL); assert(item==0xe6); assert(n==sizeof(stored));
    observed_module=module; observed_item=item;
    if(write_fail)return NV_CHECK_SUM_ERROR;
    if(!write_noop)memcpy(stored,p,n);
    return NV_SUCC;
}
nv_sts_t nv_flashReadNew(u8 single,u8 module,u8 item,u16 n,u8 *p)
{
    ++reads; assert(initialized==1); assert(module==NV_MODULE_ZCL); assert(item==0xe6);
    if(read_fail)return NV_ITEM_NOT_FOUND;
    memcpy(p,stored,n); if(read_corrupt)p[0]^=1;return NV_SUCC;
}
static int glsd_init_power_stage_io(void){++initialized;g_control.boot_on=false;g_control.boot_level=254;return 1;}
static int glsd301p_stack_nv_ensure_clean(void *ops,int count,bool force){assert(initialized==1);assert(!force);return 0;}
static bool glsd301p_timer_io_start(int cb,void *p){assert(initialized==1);assert(g_control.boot_on);assert(g_control.boot_level==73);return timer_ok;}
static void glsd301p_control_boot_failed(void *p,uint32_t t){ready=false;}
#define zb_init() ((void)0)
#define zb_zdoCbRegister(...) ((void)0)
#define af_nodeDescManuCodeUpdate(...) ((void)0)
#define glsd_configure_power_descriptor() ((void)0)
#define zcl_init(...) ((void)0)
#define af_endpointRegister(...) ((void)0)
#define zcl_register(...) ((void)0)
#define ota_init(...) ((void)0)
#define glsd301p_rejoin_init(...) ((void)0)
#define glsd301p_health_init(...) ((void)0)
#define glsd_health_snapshot_now(...) ((void)0)
#define glsd_ensure_maintenance_timer() ((void)0)
#define bdb_init(...) 0
/* TARGET_BODIES */
static bool seed(const uint8_t *r,void *ctx){memcpy(stored,r,sizeof(stored));return true;}
int main(void)
{
    uint8_t good[sizeof(stored)], bad[sizeof(stored)];
    glsd301p_startup_init(&g_startup,NULL);g_startup.policy=1;
    glsd301p_startup_poll(&g_startup,true,73,0,true,seed,NULL);
    assert(glsd301p_startup_poll(&g_startup,true,73,1000,true,seed,NULL));
    memcpy(good,stored,sizeof(good));
    user_init(false);assert(g_control.boot_on && g_control.boot_level==73);
    assert(reads==1);timer_ok=false;initialized=0;user_init(false);assert(!ready);
    ready=true;initialized=1;
    assert(glsd_startup_write(good,NULL));
    unsigned count=writes;
    voltage=2199;assert(!glsd_startup_write(good,NULL));assert(writes==count);
    voltage=2200;assert(glsd_startup_write(good,NULL));
    write_fail=true;assert(!glsd_startup_write(good,NULL));write_fail=false;
    read_fail=true;assert(!glsd_startup_write(good,NULL));read_fail=false;
    read_corrupt=true;assert(!glsd_startup_write(good,NULL));read_corrupt=false;
    /* Valid-but-different readback must also fail. */
    memcpy(bad,good,sizeof(bad));
    glsd301p_startup_init(&g_startup,NULL);g_startup.policy=2;
    glsd301p_startup_poll(&g_startup,true,73,0,true,seed,NULL);
    assert(glsd301p_startup_poll(&g_startup,true,73,1000,true,seed,NULL));
    /* Model a writer acknowledging without storing anything. */
    memcpy(bad,stored,sizeof(bad));
    write_noop=true;assert(!glsd_startup_write(good,NULL));write_noop=false;
    /* safe seam: independently flip every reason that must defer a save. */
    for(unsigned i=0;i<6;i++){
        glsd301p_startup_init(&g_startup,good);g_startup.policy=255;
        g_runtime.logical_output_enabled=true;g_runtime.current_level=73;
        ready=i!=0;g_uart_service.fault_latched=i==1;g_uart_service.inflight_active=i==2;
        dma_busy=i==3;pending=i==4;g_level_state.mode=i==5?1:0;
        count=writes;now=1000;glsd_startup_maintenance();now=3000;glsd_startup_maintenance();assert(writes==count);
    }
    ready=true;g_uart_service.fault_latched=false;g_uart_service.inflight_active=false;
    dma_busy=false;pending=false;g_level_state.mode=0;
    now=4000;glsd_startup_maintenance();now=5000;glsd_startup_maintenance();assert(writes==count+1);
    voltage=2199;flash_write(0,1,good);flash_erase(0);assert(flash_writes==0 && flash_erases==0);
    voltage=2200;flash_write(0,1,good);flash_erase(0);assert(flash_writes==1 && flash_erases==1);
    printf("{\"nv_module\":%u,\"nv_item\":%u,\"nv_start\":%u,\"voltage_guard\":%u,\"target_wiring\":\"PASS\"}\n",
        observed_module,observed_item,(unsigned)MODULES_START_ADDR(observed_module),VOLTAGE_DETECT_ENABLE);
    return 0;
}
