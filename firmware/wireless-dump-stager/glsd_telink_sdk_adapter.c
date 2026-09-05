#if defined(GLSD_TELINK_SDK)
/*
 * Telink's legacy TC32 headers must establish app_cfg/platform/compiler types
 * before our portable headers include the host C library typedef surface.
 * The target-build harness also defines the legacy size_t guard macros used by
 * Telink's TC32 environment.  This ordering is irrelevant to the native
 * fail-closed stub build but is required for the real TLSR8258 translation unit.
 */
#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#endif

#include "glsd_telink_sdk_adapter.h"
#include "glsd_transport_adapter.h"

#if defined(GLSD_TELINK_SDK)

static glsd_stager_core_t *g_glsd_stager_ctx;

static int glsd_telink_send_response(
    void *user,
    uint8_t aps_secured,
    uint16_t profile_id,
    uint16_t destination_short_addr,
    uint8_t destination_endpoint,
    uint8_t source_endpoint,
    uint8_t zcl_sequence,
    uint8_t response_command_id,
    const uint8_t *payload,
    size_t payload_length
) {
    epInfo_t dst;
    status_t status;

    (void)user;
    if (payload_length > 0xFFFFu) {
        return -1;
    }

    memset(&dst, 0, sizeof(dst));
    dst.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
    dst.dstAddr.shortAddr = destination_short_addr;
    dst.dstEp = destination_endpoint;
    dst.profileId = profile_id;
    dst.txOptions |= APS_TX_OPT_ACK_TX;
    if (aps_secured) {
        dst.txOptions |= APS_TX_OPT_SECURITY_ENABLED;
    }

    status = zcl_sendCmd(
        source_endpoint,
        &dst,
        GLSD_DUMP_CLUSTER_ID,
        response_command_id,
        TRUE,
        ZCL_FRAME_SERVER_CLIENT_DIR,
        TRUE,
        0,
        zcl_sequence,
        (u16)payload_length,
        (u8 *)payload
    );
    return status == ZCL_STA_SUCCESS ? 0 : -1;
}

static status_t glsd_telink_cluster_handler(zclIncoming_t *incoming) {
    glsd_transport_request_t request;
    glsd_transport_status_t result;

    if (incoming == NULL || g_glsd_stager_ctx == NULL) {
        return ZCL_STA_SUCCESS;
    }

    memset(&request, 0, sizeof(request));
    request.is_unicast = UNICAST_MSG(incoming->msg) ? 1u : 0u;
    request.client_to_server =
        incoming->addrInfo.dirCluster == ZCL_FRAME_CLIENT_SERVER_DIR ? 1u : 0u;
    request.aps_secured = incoming->addrInfo.apsSec ? 1u : 0u;
    request.profile_id = incoming->addrInfo.profileId;
    request.source_short_addr = incoming->addrInfo.srcAddr;
    request.source_endpoint = incoming->addrInfo.srcEp;
    request.destination_endpoint = incoming->addrInfo.dstEp;
    request.zcl_sequence = incoming->addrInfo.seqNum;
    request.command_id = incoming->hdr.cmd;
    request.payload = incoming->pData;
    request.payload_length = incoming->dataLen;

    result = glsd_transport_handle(
        g_glsd_stager_ctx,
        &request,
        glsd_telink_send_response,
        NULL
    );

    /*
     * A successful transport call already emitted the cluster-specific reply.
     * Dropped/invalid requests deliberately produce no application payload.
     * The host sets disableDefaultResponse on every extraction request.
     */
    return result == GLSD_TRANSPORT_OK ? ZCL_STA_CMD_HAS_RESP : ZCL_STA_SUCCESS;
}

int glsd_telink_sdk_adapter_register(glsd_stager_core_t *ctx) {
    status_t status;
    if (ctx == NULL || !ctx->ready) {
        return -1;
    }

    g_glsd_stager_ctx = ctx;
    status = zcl_registerCluster(
        GLSD_DUMP_ENDPOINT,
        GLSD_DUMP_CLUSTER_ID,
        0,
        0,
        NULL,
        glsd_telink_cluster_handler,
        NULL
    );
    if (status != ZCL_STA_SUCCESS) {
        g_glsd_stager_ctx = NULL;
        return -1;
    }
    return 0;
}

#else

int glsd_telink_sdk_adapter_register(glsd_stager_core_t *ctx) {
    (void)ctx;
    return -1;
}

#endif
