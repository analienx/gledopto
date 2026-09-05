#ifndef GLSD_TRANSPORT_ADAPTER_H
#define GLSD_TRANSPORT_ADAPTER_H

#include <stddef.h>
#include <stdint.h>

#include "glsd_stager_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GLSD_DUMP_ENDPOINT 11u
#define GLSD_DUMP_CLUSTER_ID 0xFC00u

/*
 * Radio/stack-independent metadata normalized by a concrete SDK adapter.
 * The pure layer rejects non-unicast, wrong endpoint and wrong direction before
 * dispatching any request that could expose application bytes.
 */
typedef struct {
    uint8_t is_unicast;
    uint8_t client_to_server;
    uint16_t profile_id;
    uint16_t source_short_addr;
    uint8_t source_endpoint;
    uint8_t destination_endpoint;
    uint8_t zcl_sequence;
    uint8_t command_id;
    const uint8_t *payload;
    size_t payload_length;
} glsd_transport_request_t;

typedef int (*glsd_transport_send_fn)(
    void *user,
    uint16_t profile_id,
    uint16_t destination_short_addr,
    uint8_t destination_endpoint,
    uint8_t source_endpoint,
    uint8_t zcl_sequence,
    uint8_t response_command_id,
    const uint8_t *payload,
    size_t payload_length
);

typedef enum {
    GLSD_TRANSPORT_OK = 0,
    GLSD_TRANSPORT_DROP_NON_UNICAST = 1,
    GLSD_TRANSPORT_DROP_WRONG_ENDPOINT = 2,
    GLSD_TRANSPORT_DROP_WRONG_DIRECTION = 3,
    GLSD_TRANSPORT_ERR_ARGUMENT = -30,
    GLSD_TRANSPORT_ERR_DISPATCH = -31,
    GLSD_TRANSPORT_ERR_SEND = -32,
} glsd_transport_status_t;

glsd_transport_status_t glsd_transport_handle(
    const glsd_stager_core_t *ctx,
    const glsd_transport_request_t *request,
    glsd_transport_send_fn send_response,
    void *send_user
);

#ifdef __cplusplus
}
#endif

#endif
