#ifndef GLSD_DUMP_PROTOCOL_H
#define GLSD_DUMP_PROTOCOL_H

#include <stdint.h>

#define GLSD_DUMP_PROTOCOL_VERSION 1u
#define GLSD_DUMP_CLUSTER_ID       0xFC00u
#define GLSD_DUMP_MAX_CHUNK        64u

#define GLSD_CMD_HELLO             0x00u
#define GLSD_CMD_INFO              0x01u
#define GLSD_CMD_READ_REQUEST      0x10u
#define GLSD_CMD_DATA              0x11u
#define GLSD_CMD_FINISH            0x12u
#define GLSD_CMD_ABORT             0x7Fu

/* Wire fields are little-endian. No command in protocol v1 writes flash. */
typedef struct __attribute__((packed)) {
    uint8_t  protocol_version;
    uint32_t active_bank_start;
    uint32_t source_bank_start;
    uint32_t readable_length;
    uint16_t max_chunk;
} glsd_dump_info_t;

typedef struct __attribute__((packed)) {
    uint32_t stream_id;
    uint32_t offset;
    uint8_t  length;
    /* followed by length bytes data, then uint32_t CRC32(data) */
} glsd_dump_data_prefix_t;

#endif
