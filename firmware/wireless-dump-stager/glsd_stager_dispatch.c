#include "glsd_stager_dispatch.h"

#include <string.h>

#define GLSD_CRC_POLY 0xEDB88320u

static uint32_t u32le(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void put_u8(uint8_t **p, uint8_t v) {
    **p = v;
    ++(*p);
}

static void put_u32(uint8_t **p, uint32_t v) {
    put_u32le(*p, v);
    *p += 4;
}

uint32_t glsd_transport_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    unsigned bit;
    if (data == NULL && length != 0u) {
        return 0u;
    }
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? GLSD_CRC_POLY : 0u);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static glsd_dispatch_status_t dispatch_ping(
    const glsd_stager_core_t *ctx,
    const uint8_t *request,
    size_t request_len,
    uint8_t *response,
    size_t cap,
    size_t *response_len
) {
    uint8_t *p = response;
    uint32_t nonce;
    if (request_len != GLSD_PING_REQUEST_SIZE) {
        return GLSD_DISPATCH_ERR_LENGTH;
    }
    if (request[0] != GLSD_DUMP_PROTOCOL_VERSION) {
        return GLSD_DISPATCH_ERR_ARGUMENT;
    }
    if (cap < GLSD_PING_RESPONSE_SIZE) {
        return GLSD_DISPATCH_ERR_OUTPUT;
    }
    nonce = u32le(&request[1]);
    put_u8(&p, GLSD_DUMP_PROTOCOL_VERSION);
    put_u32(&p, nonce);
    put_u32(&p, ctx->env.stager_build_id);
    put_u32(&p, ctx->env.session_id);
    *response_len = (size_t)(p - response);
    return GLSD_DISPATCH_OK;
}

static glsd_dispatch_status_t dispatch_info(
    const glsd_stager_core_t *ctx,
    const uint8_t *request,
    size_t request_len,
    uint8_t *response,
    size_t cap,
    size_t *response_len
) {
    glsd_info_response_t info;
    uint8_t *p = response;
    (void)request;
    if (request_len != 0u) {
        return GLSD_DISPATCH_ERR_LENGTH;
    }
    if (cap < GLSD_INFO_WIRE_SIZE) {
        return GLSD_DISPATCH_ERR_OUTPUT;
    }
    if (glsd_stager_core_info(ctx, &info) != GLSD_STAGER_OK) {
        return GLSD_DISPATCH_ERR_CORE;
    }
    /* Explicit serialization: never depend on packed-struct ABI/endian behavior. */
    put_u8(&p, info.protocol_version);
    put_u32(&p, info.stager_build_id);
    put_u32(&p, info.session_id);
    put_u32(&p, info.flash_jedec_id);
    put_u32(&p, info.flash_size);
    put_u32(&p, info.bank_a_base);
    put_u32(&p, info.bank_b_base);
    put_u32(&p, info.bank_a_flag32);
    put_u32(&p, info.bank_b_flag32);
    put_u32(&p, info.inferred_stager_base);
    put_u32(&p, info.inferred_old_base);
    put_u32(&p, info.old_declared_size);
    put_u32(&p, info.old_tail_crc32);
    put_u8(&p, info.old_reconstructed_crc_valid);
    put_u32(&p, info.allowed_read_start);
    put_u32(&p, info.allowed_read_length);
    put_u8(&p, info.journal_state);
    put_u8(&p, info.rollback_compiled);
    *response_len = (size_t)(p - response);
    return *response_len == GLSD_INFO_WIRE_SIZE
        ? GLSD_DISPATCH_OK
        : GLSD_DISPATCH_ERR_OUTPUT;
}

static glsd_dispatch_status_t dispatch_read(
    const glsd_stager_core_t *ctx,
    const uint8_t *request,
    size_t request_len,
    uint8_t *response,
    size_t cap,
    size_t *response_len
) {
    uint32_t sid;
    uint32_t seq;
    uint32_t offset;
    uint8_t length;
    uint8_t *data;
    uint32_t crc;
    glsd_stager_status_t core_rc;
    if (request_len != GLSD_READ_REQUEST_SIZE) {
        return GLSD_DISPATCH_ERR_LENGTH;
    }
    sid = u32le(&request[0]);
    seq = u32le(&request[4]);
    offset = u32le(&request[8]);
    length = request[12];
    if (sid != ctx->env.session_id) {
        return GLSD_DISPATCH_ERR_SESSION;
    }
    if (length == 0u || length > GLSD_DUMP_MAX_CHUNK) {
        return GLSD_DISPATCH_ERR_LENGTH;
    }
    if (cap < (size_t)13u + length + 5u) {
        return GLSD_DISPATCH_ERR_OUTPUT;
    }
    put_u32le(&response[0], sid);
    put_u32le(&response[4], seq);
    put_u32le(&response[8], offset);
    response[12] = length;
    data = &response[13];
    core_rc = glsd_stager_core_read(ctx, offset, length, data);
    if (core_rc != GLSD_STAGER_OK) {
        return GLSD_DISPATCH_ERR_CORE;
    }
    crc = glsd_transport_crc32(data, length);
    put_u32le(&response[13u + length], crc);
    response[17u + length] = 0u;
    *response_len = (size_t)18u + length;
    return GLSD_DISPATCH_OK;
}

glsd_dispatch_status_t glsd_stager_dispatch(
    const glsd_stager_core_t *ctx,
    uint8_t command_id,
    const uint8_t *request,
    size_t request_len,
    uint8_t *response_command_id,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_len
) {
    glsd_dispatch_status_t rc;
    if (ctx == NULL || !ctx->ready || response_command_id == NULL ||
        response == NULL || response_len == NULL || (request == NULL && request_len != 0u)) {
        return GLSD_DISPATCH_ERR_ARGUMENT;
    }
    *response_len = 0u;
    switch (command_id) {
        case GLSD_CMD_PING:
            rc = dispatch_ping(ctx, request, request_len, response, response_capacity, response_len);
            break;
        case GLSD_CMD_INFO:
            rc = dispatch_info(ctx, request, request_len, response, response_capacity, response_len);
            break;
        case GLSD_CMD_READ:
            rc = dispatch_read(ctx, request, request_len, response, response_capacity, response_len);
            break;
        case GLSD_CMD_ABORT:
            if (request_len != 0u) {
                return GLSD_DISPATCH_ERR_LENGTH;
            }
            /* ABORT is deliberately a no-op for flash/network state in v1. */
            *response_len = 0u;
            rc = GLSD_DISPATCH_OK;
            break;
        default:
            return GLSD_DISPATCH_ERR_UNSUPPORTED;
    }
    if (rc == GLSD_DISPATCH_OK) {
        *response_command_id = (uint8_t)(command_id | GLSD_RSP_BIT);
    }
    return rc;
}
