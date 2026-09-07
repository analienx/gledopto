#pragma once

#define DEFAULT_CHANNEL                 20
#define NV_ENABLE                       1
#define SECURITY_ENABLE                 1

#define ZCL_CLUSTER_NUM_MAX             3
#define ZCL_REPORTING_TABLE_NUM         1
#define ZCL_SCENE_TABLE_NUM             1
#define ZCL_MAX_SCENE_EXT_FIELD_SIZE    1
#define APS_GROUP_TABLE_NUM             1
#define APS_BINDING_TABLE_NUM           4

#if (COORDINATOR)
#define ZB_COORDINATOR_ROLE             1
#elif (ROUTER)
#define ZB_ROUTER_ROLE                  1
#elif (END_DEVICE)
#define ZB_ED_ROLE                      1
#endif

#if ZB_ED_ROLE
#if PM_ENABLE
#error "Stage-0 must remain awake so the rollback watchdog and radio stay live"
#endif
#ifndef ZB_MAC_RX_ON_WHEN_IDLE
#define ZB_MAC_RX_ON_WHEN_IDLE          1
#endif
#endif
