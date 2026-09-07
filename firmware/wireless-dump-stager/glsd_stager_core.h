#ifndef GLSD_STAGER_CORE_H
#define GLSD_STAGER_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "glsd_dump_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GLSD_TELINK_STARTUP_FLAG        0x544C4E4Bu
#define GLSD_TELINK_MARKER_OFFSET       0x08u
#define GLSD_TELINK_DECLARED_SIZE_OFF   0x18u
#define GLSD_TELINK_MIN_APP_SIZE        0x20u
#define GLSD_TELINK_INVALIDATED_BYTE    0x00u
#define GLSD_TELINK_VALID_BYTE          0x4Bu

/*
 * Read-only flash abstraction. Returning 0 means success. There is
 * intentionally no erase/write callback anywhere in extraction-core v1.
 */
typedef int (*glsd_flash_read_fn)(void *user, uint32_t address, uint8_t *dst, uint32_t length);

typedef enum {
    GLSD_STAGER_OK = 0,
    GLSD_STAGER_ERR_ARGUMENT = -1,
    GLSD_STAGER_ERR_FLASH_READ = -2,
    GLSD_STAGER_ERR_GEOMETRY = -3,
    GLSD_STAGER_ERR_HEADER = -4,
    GLSD_STAGER_ERR_MARKER = -5,
    GLSD_STAGER_ERR_SIZE = -6,
    GLSD_STAGER_ERR_CRC = -7,
    GLSD_STAGER_ERR_NOT_READY = -8,
    GLSD_STAGER_ERR_RANGE = -9,
} glsd_stager_status_t;

typedef struct {
    glsd_flash_read_fn read;
    void *read_user;
    uint32_t flash_jedec_id;
    uint32_t flash_size;
    uint32_t stager_base;
    uint32_t stager_build_id;
    uint32_t session_id;
} glsd_stager_env_t;

typedef struct {
    glsd_stager_env_t env;
    uint32_t old_base;
    uint32_t bank_a_flag32;
    uint32_t bank_b_flag32;
    uint32_t old_declared_size;
    uint32_t old_tail_crc32;
    uint32_t old_reconstructed_crc32;
    uint8_t ready;
} glsd_stager_core_t;

/* Telink xcrc32: init 0xFFFFFFFF, reflected polynomial, no final XOR. */
uint32_t glsd_telink_xcrc32_update(uint32_t crc, const uint8_t *data, size_t length);

/*
 * Initialize and prove that the opposite bank is the expected invalidated old
 * Telink application and that its virtual +0x08 reconstruction passes xcrc32.
 */
glsd_stager_status_t glsd_stager_core_init(
    glsd_stager_core_t *ctx,
    const glsd_stager_env_t *env
);

/* Fill protocol INFO from an already validated read-only context. */
glsd_stager_status_t glsd_stager_core_info(
    const glsd_stager_core_t *ctx,
    glsd_info_response_t *out
);

/*
 * Read exact raw bytes from the old application bank. Reads are relative to
 * old_base and hard-limited to old_declared_size and GLSD_DUMP_MAX_CHUNK.
 */
glsd_stager_status_t glsd_stager_core_read(
    const glsd_stager_core_t *ctx,
    uint32_t offset,
    uint8_t length,
    uint8_t *dst
);

#ifdef __cplusplus
}
#endif

#endif
