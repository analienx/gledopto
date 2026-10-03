/*
 * Pristine-body reproductions for G4 hardening (R6/R7/R8/follow-ups).
 *
 * One argv-selected case per known record-loop defect (24 cases).  Each
 * case drives the ORIGINAL SDK cluster/foundation body (no guards).
 * Cluster-direct cases use exact-size heap payloads, so overreads trip
 * AddressSanitizer deterministically.  Root-path cases pool-allocate the
 * message (as the real root requires) and use marker semantics instead:
 * pool slots carry slack past the asdu tail, so pristine overreads parse
 * slack and the defect shows as acceptance where the guard reports
 * MALFORMED.
 *
 * Exit contract (checked by the CI step that runs this binary):
 *   nonzero (sanitizer abort or REPRO marker) = defect reproduced (good);
 *   0 = the defect did NOT reproduce and fails the CI step.
 *
 * Compiled ONLY against the pristine snapshot (build/orig-sdk); never
 * against the hardened copy.  Built -m32 so pointer widths match the
 * 32-bit device pools.  Stubs mirror the dispatch harness seams exactly
 * (AF send, APS group table, report persistence, binding search, joined
 * state, random, OTA abort, IRQ lock); no app policy is linked.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zcl_include.h"
#include "ev_buffer.h"

#include "hw_stub.h"

#define REPRO_EP 0x0Bu

/* Minimal attribute storage for the registered clusters. */
static u8 r_basic_zcl_version = 3u;
static u16 r_cluster_revision = 3u;
static u16 r_identify_time;
static u8 r_group_name_support;
static u8 r_onoff;
static u8 r_ota_upgrade_status;

static const zclAttrInfo_t r_basic_attrs[] = {
    {ZCL_ATTRID_BASIC_ZCL_VER, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ,
     (u8 *)&r_basic_zcl_version},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,
     ACCESS_CONTROL_READ, (u8 *)&r_cluster_revision},
};

static const zclAttrInfo_t r_identify_attrs[] = {
    {ZCL_ATTRID_IDENTIFY_TIME, ZCL_DATA_TYPE_UINT16,
     ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&r_identify_time},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,
     ACCESS_CONTROL_READ, (u8 *)&r_cluster_revision},
};

static const zclAttrInfo_t r_group_attrs[] = {
    {ZCL_ATTRID_GROUP_NAME_SUPPORT, ZCL_DATA_TYPE_BITMAP8,
     ACCESS_CONTROL_READ, (u8 *)&r_group_name_support},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,
     ACCESS_CONTROL_READ, (u8 *)&r_cluster_revision},
};

static const zclAttrInfo_t r_onoff_attrs[] = {
    {ZCL_ATTRID_ONOFF, ZCL_DATA_TYPE_BOOLEAN,
     ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8 *)&r_onoff},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,
     ACCESS_CONTROL_READ, (u8 *)&r_cluster_revision},
};

static const zclAttrInfo_t r_ota_attrs[] = {
    {ZCL_ATTRID_OTA_IMAGE_UPGRADE_STATUS, ZCL_DATA_TYPE_ENUM8,
     ACCESS_CONTROL_READ, (u8 *)&r_ota_upgrade_status},
    {ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16,
     ACCESS_CONTROL_READ, (u8 *)&r_cluster_revision},
};

/* Counting thunks: parse must fault before any callback runs. */
static unsigned int r_identify_calls;
static unsigned int r_onoff_calls;
static unsigned int r_ota_calls;

static status_t thunk_count_identify(zclIncomingAddrInfo_t *addr, u8 cmd_id,
                                     void *payload)
{
    (void)addr;
    (void)cmd_id;
    (void)payload;
    r_identify_calls++;
    return ZCL_STA_SUCCESS;
}

static status_t thunk_count_onoff(zclIncomingAddrInfo_t *addr, u8 cmd_id,
                                  void *payload)
{
    (void)addr;
    (void)cmd_id;
    (void)payload;
    r_onoff_calls++;
    return ZCL_STA_SUCCESS;
}

static status_t thunk_count_ota(zclIncomingAddrInfo_t *addr, u8 cmd_id,
                                void *payload)
{
    (void)addr;
    (void)cmd_id;
    (void)payload;
    r_ota_calls++;
    return ZCL_STA_SUCCESS;
}

/* ---- Opaque/MCU seams (same set as the dispatch harness) ---- */

static unsigned int r_af_count;
static u8 r_af_asdu[280];
static u16 r_af_len;

u8 af_dataSend(u8 srcEp, epInfo_t *pDstEpInfo, u16 clusterId, u16 cmdPldLen,
               u8 *cmdPld, u8 *apsCnt)
{
    (void)srcEp;
    (void)pDstEpInfo;
    (void)clusterId;
    if (apsCnt != NULL) {
        *apsCnt = 0u;
    }
    if (cmdPld != NULL && cmdPldLen <= (u16)sizeof(r_af_asdu)) {
        memcpy(r_af_asdu, cmdPld, cmdPldLen);
        r_af_len = cmdPldLen;
    } else {
        r_af_len = 0u;
    }
    r_af_count++;
    return RET_OK;
}

#define REPRO_GROUP_MAX 8u
static u16 r_groups[REPRO_GROUP_MAX];
static unsigned int r_group_num;
static aps_group_tbl_ent_t r_search_ent;

aps_status_t aps_me_group_add_req(aps_add_group_req_t *req)
{
    unsigned int i;

    if (req == NULL) {
        return 1u;
    }
    for (i = 0u; i < r_group_num; i++) {
        if (r_groups[i] == req->group_addr) {
            return APS_STATUS_SUCCESS;
        }
    }
    if (r_group_num >= REPRO_GROUP_MAX) {
        return 1u;
    }
    r_groups[r_group_num++] = req->group_addr;
    return APS_STATUS_SUCCESS;
}

aps_status_t aps_me_group_delete_req(aps_delete_group_req_t *req)
{
    unsigned int i;

    if (req == NULL) {
        return 1u;
    }
    for (i = 0u; i < r_group_num; i++) {
        if (r_groups[i] == req->group_addr) {
            r_groups[i] = r_groups[--r_group_num];
            return APS_STATUS_SUCCESS;
        }
    }
    return 1u;
}

aps_status_t aps_me_group_delete_all_req(u8 ep)
{
    (void)ep;
    r_group_num = 0u;
    return APS_STATUS_SUCCESS;
}

aps_group_tbl_ent_t *aps_group_search(u16 groupAddr, u8 endpoint)
{
    unsigned int i;

    (void)endpoint;
    for (i = 0u; i < r_group_num; i++) {
        if (r_groups[i] == groupAddr) {
            memset(&r_search_ent, 0, sizeof(r_search_ent));
            r_search_ent.group_addr = groupAddr;
            return &r_search_ent;
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
    *counter = (u8)r_group_num;
    if (group_list != NULL) {
        for (i = 0u; i < r_group_num; i++) {
            group_list[i] = r_groups[i];
        }
    }
}

u8 aps_group_entry_num_get(void)
{
    return (u8)r_group_num;
}

nv_sts_t_shim zcl_reportingTab_save(void)
{
    return NV_SUCC_SHIM;
}

nv_sts_t_shim zcl_reportingTab_restore(void)
{
    return (nv_sts_t_shim)1;
}

void *zb_bindingTblSearched(u16 clusterId, u8 endpoint)
{
    (void)clusterId;
    (void)endpoint;
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

void ota_upgradeAbort(void)
{
}

u8 sys_exceptionPost(u16 line, u8 evt)
{
    (void)line;
    (void)evt;
    return 0u;
}

u8 tl_zbTaskPost(tl_zb_callback_t func, void *arg)
{
    (void)func;
    (void)arg;
    return 1u;
}

sys_diagnostics_t g_sysDiags;

/* IRQ/clock seams come from hw_stub.c (shared, already linked). */

/* ---- Fixture + drivers ---- */

static apsdeDataInd_t r_msg;
static zclIncoming_t r_in;

static void repro_init(zcl_hookFn_t hook)
{
    host_stub_reset();
    host_clock_set(0u);
    r_identify_calls = 0u;
    r_onoff_calls = 0u;
    r_ota_calls = 0u;
    r_af_count = 0u;
    r_group_num = 0u;
    r_identify_time = 0u;
    r_onoff = 0u;

    memset(&r_msg, 0, sizeof(r_msg));
    r_msg.indInfo.dst_ep = REPRO_EP;
    r_msg.indInfo.dst_addr = 0x1234u;
    r_msg.indInfo.dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
    r_msg.indInfo.src_ep = 1u;
    r_msg.indInfo.src_short_addr = 0x5678u;
    r_msg.indInfo.profile_id = 0x0104u;
    r_msg.indInfo.security_status = SECURITY_IN_APSLAYER;
    memset(&r_in, 0, sizeof(r_in));
    r_in.msg = &r_msg;

    ev_buf_init();
    zcl_init(hook);
    assert(zcl_basic_register(REPRO_EP, MANUFACTURER_CODE_NONE,
                              (u8)(sizeof(r_basic_attrs) /
                                   sizeof(r_basic_attrs[0])),
                              r_basic_attrs, NULL) == ZCL_STA_SUCCESS);
    assert(zcl_identify_register(REPRO_EP, MANUFACTURER_CODE_NONE,
                                 (u8)(sizeof(r_identify_attrs) /
                                      sizeof(r_identify_attrs[0])),
                                 r_identify_attrs,
                                 thunk_count_identify) == ZCL_STA_SUCCESS);
    assert(zcl_group_register(REPRO_EP, MANUFACTURER_CODE_NONE,
                              (u8)(sizeof(r_group_attrs) /
                                   sizeof(r_group_attrs[0])),
                              r_group_attrs, NULL) == ZCL_STA_SUCCESS);
    assert(zcl_onOff_register(REPRO_EP, MANUFACTURER_CODE_NONE,
                              (u8)(sizeof(r_onoff_attrs) /
                                   sizeof(r_onoff_attrs[0])),
                              r_onoff_attrs,
                              thunk_count_onoff) == ZCL_STA_SUCCESS);
    assert(zcl_ota_register(REPRO_EP, MANUFACTURER_CODE_NONE,
                            (u8)(sizeof(r_ota_attrs) /
                                 sizeof(r_ota_attrs[0])),
                            r_ota_attrs, thunk_count_ota) ==
           ZCL_STA_SUCCESS);
}

/*
 * Drive one cluster command straight into the registered pristine handler
 * with an exact-size heap payload (zero length passes NULL so any
 * dereference faults immediately). Returns the handler status when the
 * parse survives.
 */
static u8 cluster_frame(u16 cluster, u8 cmd, u8 direction, const u8 *payload,
                        u16 payload_len)
{
    u8 *exact = NULL;
    clusterInfo_t *info;
    u8 status;

    if (payload_len > 0u) {
        exact = (u8 *)malloc(payload_len);
        assert(exact != NULL);
        memcpy(exact, payload, payload_len);
    }
    memset(&r_msg, 0, sizeof(r_msg));
    memset(&r_in, 0, sizeof(r_in));
    r_msg.indInfo.dst_ep = REPRO_EP;
    r_msg.indInfo.dst_addr = 0x1234u;
    r_msg.indInfo.dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
    r_msg.indInfo.cluster_id = cluster;
    r_msg.indInfo.src_ep = 1u;
    r_msg.indInfo.src_short_addr = 0x5678u;
    r_msg.indInfo.profile_id = 0x0104u;
    r_msg.indInfo.security_status = SECURITY_IN_APSLAYER;
    r_in.msg = &r_msg;
    r_in.hdr.frmCtrl.bf.dir = direction;
    r_in.hdr.cmd = cmd;
    r_in.pData = (payload_len > 0u) ? exact : NULL;
    r_in.dataLen = payload_len;
    r_in.addrInfo.dstEp = REPRO_EP;
    r_in.addrInfo.srcEp = 1u;
    r_in.attrCmd = NULL;

    info = zcl_findCluster(REPRO_EP, cluster);
    assert(info != NULL);
    assert(info->cmdHandlerFunc != NULL);
    r_in.clusterAppCb = info->clusterAppCb;
    status = info->cmdHandlerFunc(&r_in);

    free(exact);
    return status;
}

/*
 * Drive one full ZCL frame through the pristine root dispatcher. The
 * message wrapper is pool-allocated (the real root frees it with
 * ev_buf_free). Pool slots carry slack past the asdu tail, so root-path
 * overreads parse slack instead of tripping ASan: root cases use marker
 * semantics (pristine accepts where the guard reports MALFORMED).
 */
static bool root_frame(u16 cluster, u8 cmd, u8 specific, u8 dir,
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
        frm |= 0x04u;
    }
    hdr[0] = frm;
    hdr[1] = seq;
    hdr[2] = cmd;

    ind = (apsdeDataInd_t *)ev_buf_allocate(
        (u16)(sizeof(apsdeDataInd_t) + total));
    if (ind == NULL) {
        return false;
    }
    memset(ind, 0, sizeof(apsdeDataInd_t) + total);
    ind->indInfo.dst_ep = REPRO_EP;
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
    return true;
}

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

static void repro_noop_hook(zclIncoming_t *msg)
{
    (void)msg;
}

/*
 * ASan-driven cases: the pristine parse must abort. A clean return means
 * the defect did not reproduce (binary exit 0 fails the CI step).
 */
static int run_asan_case(const char *name, u16 cluster, u8 cmd, u8 dir,
                         const u8 *payload, u16 len)
{
    u8 status;

    repro_init(repro_noop_hook);
    status = cluster_frame(cluster, cmd, dir, payload, len);
    printf("REPRO-NOT-ABORTED: %s returned 0x%02X\n", name, status);
    return 0;
}

/* True when the last capture is a MALFORMED default response. */
static bool last_was_default_malformed(void)
{
    const u8 *b = r_af_asdu;
    u16 off;

    if (r_af_count == 0u || r_af_len < 3u) {
        return false;
    }
    off = (u8)(((b[0] & 0x04u) != 0u) ? 5u : 3u);
    if (r_af_len < (u16)(off + 3u)) {
        return false;
    }
    if (b[off] != ZCL_CMD_DEFAULT_RSP) {
        return false;
    }
    return b[off + 2u] == ZCL_STA_MALFORMED_COMMAND;
}

/*
 * Root-path cases: pool slack absorbs overreads, so the pristine defect
 * shows as acceptance (no MALFORMED default response) rather than a
 * crash. A MALFORMED default here means the pristine body already
 * guards this shape and the case does not reproduce.
 */
static int run_root_repro_case(const char *name, u16 cluster, u8 cmd, u8 dir,
                               const u8 *payload, u16 len)
{
    repro_init(repro_noop_hook);
    if (!root_frame(cluster, cmd, 0u, dir, payload, len, 1u)) {
        printf("REPRO-INFRA: %s message alloc failed\n", name);
        return 0;
    }
    if (last_was_default_malformed()) {
        printf("REPRO-NOT-REPRODUCED: %s pristine already reports MALFORMED\n",
               name);
        return 0;
    }
    printf("REPRO: %s pristine accepted (af=%u, no MALFORMED default)\n",
           name, r_af_count);
    return 1;
}

/*
 * 40 discrete config-report records: pre-fix the u8 response length
 * wrapped, the record burst overflowed its pool buffer, and the pool
 * never returned to whole. The drain below either trips ASan on the
 * corrupted free list or counts a pool that is no longer 26.
 */
static int run_cfgwrap40(void)
{
    u8 recs[40u * 8u];
    unsigned int i;
    unsigned free_n;

    repro_init(repro_noop_hook);
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
    if (!root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_CONFIG_REPORT, 0u,
                    ZCL_FRAME_CLIENT_SERVER_DIR, recs, (u16)sizeof(recs),
                    1u)) {
        printf("REPRO-INFRA: cfgwrap40 message alloc failed\n");
        return 0;
    }
    /* Assert-free drain: a corrupted free list must surface through the
     * pool count or ASan, never through a harness assertion (the CI
     * gate rejects bare assertion aborts as infra failures). */
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
            ev_buf_free(held[i]);
        }
        free_n = n;
    }
    if (free_n != 26u) {
        printf("REPRO: cfgwrap40 pool=%u (expected 26 whole)\n", free_n);
        return 1;
    }
    printf("REPRO-NOT-REPRODUCED: cfgwrap40 pool whole af=%u\n",
           r_af_count);
    return 0;
}

/*
 * Truncated config-report tail (20 analog records minus the last byte):
 * the pristine parser reads the short tail out of pool slack and
 * accepts the frame.
 */
static int run_cfgtrunc(void)
{
    u8 recs[20u * 10u];
    unsigned int i;

    repro_init(repro_noop_hook);
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
    if (!root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_CONFIG_REPORT, 0u,
                    ZCL_FRAME_CLIENT_SERVER_DIR, recs,
                    (u16)(sizeof(recs) - 1u), 1u)) {
        printf("REPRO-INFRA: cfgtrunc message alloc failed\n");
        return 0;
    }
    if (last_was_default_malformed()) {
        printf("REPRO-NOT-REPRODUCED: cfgtrunc pristine reports MALFORMED\n");
        return 0;
    }
    printf("REPRO: cfgtrunc pristine accepted (af=%u)\n", r_af_count);
    return 1;
}

/* Ten ordinary reads with a NULL hook leak ten parsed-command buffers. */
static int run_r8leak(void)
{
    const u8 read_onoff[] = {0x00u, 0x00u};
    unsigned int i;
    unsigned free_n;

    repro_init(NULL);
    for (i = 0u; i < 10u; i++) {
        root_frame(ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_READ, 0u,
                   ZCL_FRAME_CLIENT_SERVER_DIR, read_onoff,
                   (u16)sizeof(read_onoff), (u8)(i + 1u));
    }
    free_n = pool_free_total();
    if (free_n == 16u) {
        printf("REPRO: r8leak pool drained to %u (10 leaked)\n", free_n);
        return 1;
    }
    printf("REPRO-NOT-REPRODUCED: r8leak pool=%u\n", free_n);
    return 0;
}

int main(int argc, char **argv)
{
    static const u8 one[] = {0x05u};
    static const u8 group_one[] = {0x12u};
    static const u8 membership1[] = {0x01u};
    static const u8 write_one[] = {0x00u};
    static const u8 dflt_one[] = {0x00u};
    static const u8 query_rsp_short[] = {0x00u, 0x12u};
    static const u8 block_wait_short[] = {0x97u, 0x01u, 0x02u};
    static const u8 read_rsp_short[] = {0x00u, 0x00u, 0x00u, 0x21u};
    static const u8 write_rsp_short[] = {0x00u, 0x12u};
    static const u8 cfg_rsp_short[] = {0x00u, 0x01u, 0x02u};
    static const u8 report_short[] = {0x00u, 0x00u, 0x21u, 0x05u};
    static const u8 disc1[] = {0x00u};
    static const u8 disc2[] = {0x00u, 0x00u};
    static const u8 disc4[] = {0x00u, 0x00u, 0xFFu, 0xFFu};
    static const u8 read_odd[] = {0x00u};
    const char *c;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <case>\n", argv[0]);
        return 2;
    }
    c = argv[1];

    if (strcmp(c, "identify0") == 0) {
        return run_asan_case(c, ZCL_CLUSTER_GEN_IDENTIFY, ZCL_CMD_IDENTIFY,
                             ZCL_FRAME_CLIENT_SERVER_DIR, NULL, 0u);
    }
    if (strcmp(c, "identify1") == 0) {
        return run_asan_case(c, ZCL_CLUSTER_GEN_IDENTIFY, ZCL_CMD_IDENTIFY,
                             ZCL_FRAME_CLIENT_SERVER_DIR, one,
                             (u16)sizeof(one));
    }
    if (strcmp(c, "triggereffect1") == 0) {
        return run_asan_case(c, ZCL_CLUSTER_GEN_IDENTIFY,
                             ZCL_CMD_TRIGGER_EFFECT,
                             ZCL_FRAME_CLIENT_SERVER_DIR, one,
                             (u16)sizeof(one));
    }
    if (strcmp(c, "queryrsp0") == 0) {
        return run_asan_case(c, ZCL_CLUSTER_GEN_IDENTIFY,
                             ZCL_CMD_IDENTIFY_QUERY_RSP,
                             ZCL_FRAME_SERVER_CLIENT_DIR, NULL, 0u);
    }
    if (strcmp(c, "groupadd1") == 0) {
        return run_asan_case(c, ZCL_CLUSTER_GEN_GROUPS,
                             ZCL_CMD_GROUP_ADD_GROUP,
                             ZCL_FRAME_CLIENT_SERVER_DIR, group_one,
                             (u16)sizeof(group_one));
    }
    if (strcmp(c, "groupview1") == 0) {
        return run_asan_case(c, ZCL_CLUSTER_GEN_GROUPS,
                             ZCL_CMD_GROUP_VIEW_GROUP,
                             ZCL_FRAME_CLIENT_SERVER_DIR, group_one,
                             (u16)sizeof(group_one));
    }
    if (strcmp(c, "groupmember1") == 0) {
        return run_asan_case(c, ZCL_CLUSTER_GEN_GROUPS,
                             ZCL_CMD_GROUP_GET_MEMBERSHIP,
                             ZCL_FRAME_CLIENT_SERVER_DIR, membership1,
                             (u16)sizeof(membership1));
    }
    if (strcmp(c, "write1") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_IDENTIFY,
                                   ZCL_CMD_WRITE,
                                   ZCL_FRAME_CLIENT_SERVER_DIR, write_one,
                                   (u16)sizeof(write_one));
    }
    if (strcmp(c, "cfgwrap40") == 0) {
        return run_cfgwrap40();
    }
    if (strcmp(c, "cfgtrunc") == 0) {
        return run_cfgtrunc();
    }
    if (strcmp(c, "discrsp0") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_ON_OFF,
                                   ZCL_CMD_DISCOVER_ATTR_RSP,
                                   ZCL_FRAME_SERVER_CLIENT_DIR, NULL, 0u);
    }
    if (strcmp(c, "dfltrsp1") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_ON_OFF,
                                   ZCL_CMD_DEFAULT_RSP,
                                   ZCL_FRAME_SERVER_CLIENT_DIR, dflt_one,
                                   (u16)sizeof(dflt_one));
    }
    if (strcmp(c, "dflt0") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_ON_OFF,
                                   ZCL_CMD_DEFAULT_RSP,
                                   ZCL_FRAME_SERVER_CLIENT_DIR, NULL, 0u);
    }
    if (strcmp(c, "readrsp4") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_ON_OFF,
                                   ZCL_CMD_READ_RSP,
                                   ZCL_FRAME_SERVER_CLIENT_DIR,
                                   read_rsp_short,
                                   (u16)sizeof(read_rsp_short));
    }
    if (strcmp(c, "writersp2") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_ON_OFF,
                                   ZCL_CMD_WRITE_RSP,
                                   ZCL_FRAME_SERVER_CLIENT_DIR,
                                   write_rsp_short,
                                   (u16)sizeof(write_rsp_short));
    }
    if (strcmp(c, "cfgrsp3") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_ON_OFF,
                                   ZCL_CMD_CONFIG_REPORT_RSP,
                                   ZCL_FRAME_SERVER_CLIENT_DIR,
                                   cfg_rsp_short,
                                   (u16)sizeof(cfg_rsp_short));
    }
    if (strcmp(c, "report4") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_ON_OFF,
                                   ZCL_CMD_REPORT,
                                   ZCL_FRAME_CLIENT_SERVER_DIR,
                                   report_short,
                                   (u16)sizeof(report_short));
    }
    if (strcmp(c, "disc1") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_ON_OFF,
                                   ZCL_CMD_DISCOVER_ATTR,
                                   ZCL_FRAME_CLIENT_SERVER_DIR, disc1,
                                   (u16)sizeof(disc1));
    }
    if (strcmp(c, "disc2") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_ON_OFF,
                                   ZCL_CMD_DISCOVER_ATTR,
                                   ZCL_FRAME_CLIENT_SERVER_DIR, disc2,
                                   (u16)sizeof(disc2));
    }
    if (strcmp(c, "disc4") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_ON_OFF,
                                   ZCL_CMD_DISCOVER_ATTR,
                                   ZCL_FRAME_CLIENT_SERVER_DIR, disc4,
                                   (u16)sizeof(disc4));
    }
    if (strcmp(c, "readodd") == 0) {
        return run_root_repro_case(c, ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_READ,
                                   ZCL_FRAME_CLIENT_SERVER_DIR, read_odd,
                                   (u16)sizeof(read_odd));
    }
    if (strcmp(c, "otaqueryrsp2") == 0) {
        return run_asan_case(c, ZCL_CLUSTER_OTA,
                             ZCL_CMD_OTA_QUERY_NEXT_IMAGE_RSP,
                             ZCL_FRAME_SERVER_CLIENT_DIR, query_rsp_short,
                             (u16)sizeof(query_rsp_short));
    }
    if (strcmp(c, "otablockshort") == 0) {
        return run_asan_case(c, ZCL_CLUSTER_OTA,
                             ZCL_CMD_OTA_IMAGE_BLOCK_RSP,
                             ZCL_FRAME_SERVER_CLIENT_DIR, block_wait_short,
                             (u16)sizeof(block_wait_short));
    }
    if (strcmp(c, "r8leak") == 0) {
        return run_r8leak();
    }
    fprintf(stderr, "unknown case: %s\n", c);
    return 2;
}
