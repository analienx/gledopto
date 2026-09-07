#ifndef GLSD_DUMP_PROTOCOL_H
#define GLSD_DUMP_PROTOCOL_H

#include <stdint.h>

#define GLSD_DUMP_PROTOCOL_VERSION 1u
#define GLSD_DUMP_CLUSTER_ID       0xFC00u
#define GLSD_DUMP_MAX_CHUNK        48u

#define GLSD_CMD_PING              0x00u
#define GLSD_CMD_INFO              0x01u
#define GLSD_CMD_READ              0x02u
#define GLSD_CMD_ABORT             0x03u
#define GLSD_CMD_STATUS            0x04u
#define GLSD_RSP_BIT               0x80u

#define GLSD_BANK_A_BASE           0x00000u
#define GLSD_BANK_B_BASE           0x40000u
#define GLSD_APP_LIMIT             0x34000u
#define GLSD_FLASH_SIZE_512K       0x80000u

/* Wire fields are little-endian. Extraction protocol v1 exposes no writes. */
typedef struct __attribute__((packed)) {
    uint32_t session_id;
    uint32_t seq;
    uint32_t offset;
    uint8_t  length;
} glsd_read_request_t;

typedef struct __attribute__((packed)) {
    uint32_t session_id;
    uint32_t seq;
    uint32_t offset;
    uint8_t  length;
    /* followed by:
     *   uint8_t data[length]
     *   uint32_t crc32_data
     *   uint8_t status
     */
} glsd_data_prefix_t;

typedef struct __attribute__((packed)) {
    uint8_t  protocol_version;
    uint32_t stager_build_id;
    uint32_t session_id;
    uint32_t flash_jedec_id;
    uint32_t flash_size;
    uint32_t bank_a_base;
    uint32_t bank_b_base;
    uint32_t bank_a_flag32;
    uint32_t bank_b_flag32;
    uint32_t inferred_stager_base;
    uint32_t inferred_old_base;
    uint32_t old_declared_size;
    uint32_t old_tail_crc32;
    uint8_t  old_reconstructed_crc_valid;
    uint32_t allowed_read_start;
    uint32_t allowed_read_length;
    uint8_t  journal_state;
    uint8_t  rollback_compiled;
} glsd_info_response_t;

#endif
