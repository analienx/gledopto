#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "glsd_stager_dispatch.h"
#include "glsd_transport_adapter.h"

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

typedef struct {
    int calls;
    uint8_t aps_secured;
    uint16_t profile_id;
    uint16_t destination_short_addr;
    uint8_t destination_endpoint;
    uint8_t source_endpoint;
    uint8_t zcl_sequence;
    uint8_t response_command_id;
    uint8_t payload[GLSD_DISPATCH_MAX_RESPONSE];
    size_t payload_length;
} send_capture_t;

static int capture_send(
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
    send_capture_t *capture = (send_capture_t *)user;
    assert(capture != NULL);
    assert(payload_length <= sizeof(capture->payload));
    ++capture->calls;
    capture->aps_secured = aps_secured;
    capture->profile_id = profile_id;
    capture->destination_short_addr = destination_short_addr;
    capture->destination_endpoint = destination_endpoint;
    capture->source_endpoint = source_endpoint;
    capture->zcl_sequence = zcl_sequence;
    capture->response_command_id = response_command_id;
    capture->payload_length = payload_length;
    if (payload_length != 0u) {
        assert(payload != NULL);
        memcpy(capture->payload, payload, payload_length);
    }
    return 0;
}

static glsd_transport_request_t base_request(const uint8_t *payload, size_t payload_length) {
    glsd_transport_request_t request;
    memset(&request, 0, sizeof(request));
    request.is_unicast = 1u;
    request.client_to_server = 1u;
    request.aps_secured = 1u;
    request.profile_id = 0x0104u;
    request.source_short_addr = 0x1234u;
    request.source_endpoint = 1u;
    request.destination_endpoint = GLSD_DUMP_ENDPOINT;
    request.zcl_sequence = 0x5Au;
    request.command_id = GLSD_CMD_PING;
    request.payload = payload;
    request.payload_length = payload_length;
    return request;
}

int main(void) {
    glsd_stager_core_t ctx;
    send_capture_t capture;
    glsd_transport_request_t request;
    glsd_transport_status_t rc;
    const uint8_t ping[] = {
        GLSD_DUMP_PROTOCOL_VERSION,
        0x44u, 0x33u, 0x22u, 0x11u,
    };

    memset(&ctx, 0, sizeof(ctx));
    ctx.ready = 1u;
    ctx.env.stager_build_id = 0xA1B2C3D4u;
    ctx.env.session_id = 0x10203040u;

    memset(&capture, 0, sizeof(capture));
    request = base_request(ping, ARRAY_LEN(ping));
    request.is_unicast = 0u;
    rc = glsd_transport_handle(&ctx, &request, capture_send, &capture);
    assert(rc == GLSD_TRANSPORT_DROP_NON_UNICAST);
    assert(capture.calls == 0);

    request = base_request(ping, ARRAY_LEN(ping));
    request.destination_endpoint = 12u;
    rc = glsd_transport_handle(&ctx, &request, capture_send, &capture);
    assert(rc == GLSD_TRANSPORT_DROP_WRONG_ENDPOINT);
    assert(capture.calls == 0);

    request = base_request(ping, ARRAY_LEN(ping));
    request.client_to_server = 0u;
    rc = glsd_transport_handle(&ctx, &request, capture_send, &capture);
    assert(rc == GLSD_TRANSPORT_DROP_WRONG_DIRECTION);
    assert(capture.calls == 0);

    request = base_request(ping, ARRAY_LEN(ping));
    rc = glsd_transport_handle(&ctx, &request, capture_send, &capture);
    assert(rc == GLSD_TRANSPORT_OK);
    assert(capture.calls == 1);
    assert(capture.aps_secured == request.aps_secured);
    assert(capture.profile_id == request.profile_id);
    assert(capture.destination_short_addr == request.source_short_addr);
    assert(capture.destination_endpoint == request.source_endpoint);
    assert(capture.source_endpoint == GLSD_DUMP_ENDPOINT);
    assert(capture.zcl_sequence == request.zcl_sequence);
    assert(capture.response_command_id == (GLSD_CMD_PING | GLSD_RSP_BIT));
    assert(capture.payload_length == GLSD_PING_RESPONSE_SIZE);
    assert(capture.payload[0] == GLSD_DUMP_PROTOCOL_VERSION);
    assert(capture.payload[1] == 0x44u);
    assert(capture.payload[2] == 0x33u);
    assert(capture.payload[3] == 0x22u);
    assert(capture.payload[4] == 0x11u);

    memset(&capture, 0, sizeof(capture));
    request = base_request(NULL, 0u);
    request.command_id = 0x7Fu;
    rc = glsd_transport_handle(&ctx, &request, capture_send, &capture);
    assert(rc == GLSD_TRANSPORT_ERR_DISPATCH);
    assert(capture.calls == 0);

    puts("transport_adapter_test: PASS");
    return 0;
}
