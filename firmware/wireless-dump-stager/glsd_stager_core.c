#include "glsd_stager_core.h"

#include <string.h>

#define GLSD_CRC_POLY 0xEDB88320u
#define GLSD_SCAN_CHUNK 64u

static uint32_t u32le(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static glsd_stager_status_t read_exact(
    const glsd_stager_env_t *env,
    uint32_t address,
    uint8_t *dst,
    uint32_t length
) {
    if (env == NULL || env->read == NULL || dst == NULL) {
        return GLSD_STAGER_ERR_ARGUMENT;
    }
    if (length == 0u || address > env->flash_size || length > env->flash_size - address) {
        return GLSD_STAGER_ERR_RANGE;
    }
    return env->read(env->read_user, address, dst, length) == 0
        ? GLSD_STAGER_OK
        : GLSD_STAGER_ERR_FLASH_READ;
}

uint32_t glsd_telink_xcrc32_update(uint32_t crc, const uint8_t *data, size_t length) {
    size_t i;
    unsigned bit;
    if (data == NULL && length != 0u) {
        return crc;
    }
    for (i = 0; i < length; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? GLSD_CRC_POLY : 0u);
        }
    }
    return crc;
}

static glsd_stager_status_t read_flag32(
    const glsd_stager_env_t *env,
    uint32_t bank_base,
    uint32_t *out
) {
    uint8_t b[4];
    glsd_stager_status_t rc = read_exact(env, bank_base + GLSD_TELINK_MARKER_OFFSET, b, sizeof(b));
    if (rc != GLSD_STAGER_OK) {
        return rc;
    }
    *out = u32le(b);
    return GLSD_STAGER_OK;
}

static glsd_stager_status_t compute_reconstructed_crc(
    const glsd_stager_env_t *env,
    uint32_t old_base,
    uint32_t declared_size,
    uint32_t *out_crc
) {
    uint8_t buf[GLSD_SCAN_CHUNK];
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t pos = 0u;
    uint32_t body_len = declared_size - 4u;

    while (pos < body_len) {
        uint32_t n = body_len - pos;
        uint32_t i;
        glsd_stager_status_t rc;
        if (n > GLSD_SCAN_CHUNK) {
            n = GLSD_SCAN_CHUNK;
        }
        rc = read_exact(env, old_base + pos, buf, n);
        if (rc != GLSD_STAGER_OK) {
            return rc;
        }
        for (i = 0u; i < n; ++i) {
            if (pos + i == GLSD_TELINK_MARKER_OFFSET) {
                buf[i] = GLSD_TELINK_VALID_BYTE;
            }
        }
        crc = glsd_telink_xcrc32_update(crc, buf, n);
        pos += n;
    }
    *out_crc = crc;
    return GLSD_STAGER_OK;
}

glsd_stager_status_t glsd_stager_core_init(
    glsd_stager_core_t *ctx,
    const glsd_stager_env_t *env
) {
    uint8_t header[0x1Cu];
    uint8_t tail[4];
    uint8_t expected_marker[4] = {0x00u, 0x4Eu, 0x4Cu, 0x54u};
    glsd_stager_status_t rc;
    uint32_t old_base;
    uint32_t declared;
    uint32_t reconstructed_crc;

    if (ctx == NULL || env == NULL || env->read == NULL) {
        return GLSD_STAGER_ERR_ARGUMENT;
    }
    memset(ctx, 0, sizeof(*ctx));

    if (env->flash_size != GLSD_FLASH_SIZE_512K) {
        return GLSD_STAGER_ERR_GEOMETRY;
    }
    if (env->stager_base == GLSD_BANK_A_BASE) {
        old_base = GLSD_BANK_B_BASE;
    } else if (env->stager_base == GLSD_BANK_B_BASE) {
        old_base = GLSD_BANK_A_BASE;
    } else {
        return GLSD_STAGER_ERR_GEOMETRY;
    }

    if (old_base + GLSD_APP_LIMIT > env->flash_size ||
        env->stager_base + GLSD_APP_LIMIT > env->flash_size) {
        return GLSD_STAGER_ERR_GEOMETRY;
    }

    rc = read_flag32(env, GLSD_BANK_A_BASE, &ctx->bank_a_flag32);
    if (rc != GLSD_STAGER_OK) {
        return rc;
    }
    rc = read_flag32(env, GLSD_BANK_B_BASE, &ctx->bank_b_flag32);
    if (rc != GLSD_STAGER_OK) {
        return rc;
    }
    if ((env->stager_base == GLSD_BANK_A_BASE ? ctx->bank_a_flag32 : ctx->bank_b_flag32)
        != GLSD_TELINK_STARTUP_FLAG) {
        return GLSD_STAGER_ERR_MARKER;
    }
    rc = read_exact(env, old_base, header, sizeof(header));
    if (rc != GLSD_STAGER_OK) {
        return rc;
    }

    if (header[6] != 0x5Du || header[7] != 0x02u) {
        return GLSD_STAGER_ERR_HEADER;
    }
    if (memcmp(&header[GLSD_TELINK_MARKER_OFFSET], expected_marker, sizeof(expected_marker)) != 0) {
        return GLSD_STAGER_ERR_MARKER;
    }

    declared = u32le(&header[GLSD_TELINK_DECLARED_SIZE_OFF]);
    if (declared < GLSD_TELINK_MIN_APP_SIZE || declared >= GLSD_APP_LIMIT) {
        return GLSD_STAGER_ERR_SIZE;
    }
    if (declared > env->flash_size - old_base) {
        return GLSD_STAGER_ERR_SIZE;
    }

    rc = read_exact(env, old_base + declared - 4u, tail, sizeof(tail));
    if (rc != GLSD_STAGER_OK) {
        return rc;
    }
    rc = compute_reconstructed_crc(env, old_base, declared, &reconstructed_crc);
    if (rc != GLSD_STAGER_OK) {
        return rc;
    }
    if (reconstructed_crc != u32le(tail)) {
        return GLSD_STAGER_ERR_CRC;
    }

    ctx->env = *env;
    ctx->old_base = old_base;
    ctx->old_declared_size = declared;
    ctx->old_tail_crc32 = u32le(tail);
    ctx->old_reconstructed_crc32 = reconstructed_crc;
    ctx->ready = 1u;
    return GLSD_STAGER_OK;
}

glsd_stager_status_t glsd_stager_core_info(
    const glsd_stager_core_t *ctx,
    glsd_info_response_t *out
) {
    if (ctx == NULL || out == NULL) {
        return GLSD_STAGER_ERR_ARGUMENT;
    }
    if (!ctx->ready) {
        return GLSD_STAGER_ERR_NOT_READY;
    }
    memset(out, 0, sizeof(*out));
    out->protocol_version = GLSD_DUMP_PROTOCOL_VERSION;
    out->stager_build_id = ctx->env.stager_build_id;
    out->session_id = ctx->env.session_id;
    out->flash_jedec_id = ctx->env.flash_jedec_id;
    out->flash_size = ctx->env.flash_size;
    out->bank_a_base = GLSD_BANK_A_BASE;
    out->bank_b_base = GLSD_BANK_B_BASE;
    out->bank_a_flag32 = ctx->bank_a_flag32;
    out->bank_b_flag32 = ctx->bank_b_flag32;
    out->inferred_stager_base = ctx->env.stager_base;
    out->inferred_old_base = ctx->old_base;
    out->old_declared_size = ctx->old_declared_size;
    out->old_tail_crc32 = ctx->old_tail_crc32;
    out->old_reconstructed_crc_valid = 1u;
    out->allowed_read_start = 0u;
    out->allowed_read_length = ctx->old_declared_size;
    out->journal_state = 0xFFu;
    out->rollback_compiled = 0u;
    return GLSD_STAGER_OK;
}

glsd_stager_status_t glsd_stager_core_read(
    const glsd_stager_core_t *ctx,
    uint32_t offset,
    uint8_t length,
    uint8_t *dst
) {
    if (ctx == NULL || dst == NULL || length == 0u || length > GLSD_DUMP_MAX_CHUNK) {
        return GLSD_STAGER_ERR_ARGUMENT;
    }
    if (!ctx->ready) {
        return GLSD_STAGER_ERR_NOT_READY;
    }
    if (offset >= ctx->old_declared_size ||
        (uint32_t)length > ctx->old_declared_size - offset) {
        return GLSD_STAGER_ERR_RANGE;
    }
    return read_exact(&ctx->env, ctx->old_base + offset, dst, length);
}
