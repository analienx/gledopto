#ifndef GLSD_STAGER_DISPATCH_H
#define GLSD_STAGER_DISPATCH_H

#include <stddef.h>
#include <stdint.h>

#include "glsd_stager_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum command payload emitted by dispatcher v1: DATA response. */
#define GLSD_DISPATCH_MAX_RESPONSE (13u + GLSD_DUMP_MAX_CHUNK + 5u)
#define GLSD_PING_REQUEST_SIZE 5u
#define GLSD_PING_RESPONSE_SIZE 13u
#define GLSD_INFO_WIRE_SIZE 60u
#define GLSD_READ_REQUEST_SIZE 13u

typedef enum {
    GLSD_DISPATCH_OK = 0,
    GLSD_DISPATCH_ERR_ARGUMENT = -20,
    GLSD_DISPATCH_ERR_UNSUPPORTED = -21,
    GLSD_DISPATCH_ERR_LENGTH = -22,
    GLSD_DISPATCH_ERR_SESSION = -23,
    GLSD_DISPATCH_ERR_CORE = -24,
    GLSD_DISPATCH_ERR_OUTPUT = -25,
} glsd_dispatch_status_t;

/* Standard finalized CRC-32 used only for per-DATA transport integrity. */
uint32_t glsd_transport_crc32(const uint8_t *data, size_t length);

/*
 * Pure command dispatcher. No Zigbee stack calls and no flash mutation exist
 * here. `command_id` is the future ZCL cluster-specific command ID; caller
 * supplies only its payload. Response command ID is request | GLSD_RSP_BIT.
 */
glsd_dispatch_status_t glsd_stager_dispatch(
    const glsd_stager_core_t *ctx,
    uint8_t command_id,
    const uint8_t *request,
    size_t request_len,
    uint8_t *response_command_id,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_len
);

#ifdef __cplusplus
}
#endif

#endif
