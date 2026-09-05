#include "glsd_transport_adapter.h"

#include "glsd_stager_dispatch.h"

#include <string.h>

glsd_transport_status_t glsd_transport_handle(
    const glsd_stager_core_t *ctx,
    const glsd_transport_request_t *request,
    glsd_transport_send_fn send_response,
    void *send_user
) {
    uint8_t response[GLSD_DISPATCH_MAX_RESPONSE];
    uint8_t response_command = 0;
    size_t response_length = 0;
    glsd_dispatch_status_t dispatch_status;
    int send_status;

    if (ctx == NULL || request == NULL || send_response == NULL) {
        return GLSD_TRANSPORT_ERR_ARGUMENT;
    }
    if (!request->is_unicast) {
        return GLSD_TRANSPORT_DROP_NON_UNICAST;
    }
    if (request->destination_endpoint != GLSD_DUMP_ENDPOINT) {
        return GLSD_TRANSPORT_DROP_WRONG_ENDPOINT;
    }
    if (!request->client_to_server) {
        return GLSD_TRANSPORT_DROP_WRONG_DIRECTION;
    }
    if (request->payload_length > 0u && request->payload == NULL) {
        return GLSD_TRANSPORT_ERR_ARGUMENT;
    }

    memset(response, 0, sizeof(response));
    dispatch_status = glsd_stager_dispatch(
        ctx,
        request->command_id,
        request->payload,
        request->payload_length,
        &response_command,
        response,
        sizeof(response),
        &response_length
    );
    if (dispatch_status != GLSD_DISPATCH_OK) {
        return GLSD_TRANSPORT_ERR_DISPATCH;
    }

    send_status = send_response(
        send_user,
        request->profile_id,
        request->source_short_addr,
        request->source_endpoint,
        request->destination_endpoint,
        request->zcl_sequence,
        response_command,
        response,
        response_length
    );
    if (send_status != 0) {
        return GLSD_TRANSPORT_ERR_SEND;
    }
    return GLSD_TRANSPORT_OK;
}
