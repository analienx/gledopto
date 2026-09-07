#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../glsd_stager_dispatch.h"

static uint8_t flash_mem[GLSD_FLASH_SIZE_512K];

static void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int fake_read(void *user, uint32_t address, uint8_t *dst, uint32_t length) {
    (void)user;
    if (address > sizeof(flash_mem) || length > sizeof(flash_mem) - address) return -1;
    memcpy(dst, &flash_mem[address], length);
    return 0;
}

static void print_hex(const uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) printf("%02x", p[i]);
    putchar('\n');
}

static void prepare(glsd_stager_core_t *ctx) {
    glsd_stager_env_t env;
    uint8_t *app = &flash_mem[GLSD_BANK_A_BASE];
    uint32_t size = 240u;
    uint32_t i;
    uint32_t crc;
    memset(flash_mem, 0xFF, sizeof(flash_mem));
    for (i = 0; i < size; ++i) app[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    put_u32le(&app[2], 0x26013001u);
    app[6] = 0x5Du; app[7] = 0x02u;
    app[8] = 0x4Bu; app[9] = 0x4Eu; app[10] = 0x4Cu; app[11] = 0x54u;
    app[0x12] = 0x4Fu; app[0x13] = 0x12u;
    app[0x14] = 0x16u; app[0x15] = 0x14u;
    put_u32le(&app[0x18], size);
    crc = glsd_telink_xcrc32_update(0xFFFFFFFFu, app, size - 4u);
    put_u32le(&app[size - 4u], crc);
    app[8] = 0x00u;
    flash_mem[GLSD_BANK_B_BASE + 8u] = 0x4Bu;
    flash_mem[GLSD_BANK_B_BASE + 9u] = 0x4Eu;
    flash_mem[GLSD_BANK_B_BASE + 10u] = 0x4Cu;
    flash_mem[GLSD_BANK_B_BASE + 11u] = 0x54u;
    memset(&env, 0, sizeof(env));
    env.read = fake_read;
    env.flash_jedec_id = 0x00123456u;
    env.flash_size = GLSD_FLASH_SIZE_512K;
    env.stager_base = GLSD_BANK_B_BASE;
    env.stager_build_id = 0x20260905u;
    env.session_id = 0x11223344u;
    assert(glsd_stager_core_init(ctx, &env) == GLSD_STAGER_OK);
}

int main(void) {
    glsd_stager_core_t ctx;
    uint8_t req[GLSD_READ_REQUEST_SIZE];
    uint8_t rsp[GLSD_DISPATCH_MAX_RESPONSE];
    uint8_t rsp_cmd;
    size_t rsp_len;
    uint32_t seq = 100u;
    uint32_t off;
    prepare(&ctx);

    assert(glsd_stager_dispatch(&ctx, GLSD_CMD_INFO, NULL, 0u, &rsp_cmd, rsp, sizeof(rsp), &rsp_len) == GLSD_DISPATCH_OK);
    assert(rsp_cmd == (GLSD_CMD_INFO | GLSD_RSP_BIT));
    fputs("INFO=", stdout); print_hex(rsp, rsp_len);

    for (off = 0u; off < ctx.old_declared_size; off += GLSD_DUMP_MAX_CHUNK, ++seq) {
        uint32_t remain = ctx.old_declared_size - off;
        uint8_t length = (uint8_t)(remain < GLSD_DUMP_MAX_CHUNK ? remain : GLSD_DUMP_MAX_CHUNK);
        put_u32le(&req[0], ctx.env.session_id);
        put_u32le(&req[4], seq);
        put_u32le(&req[8], off);
        req[12] = length;
        assert(glsd_stager_dispatch(&ctx, GLSD_CMD_READ, req, sizeof(req), &rsp_cmd, rsp, sizeof(rsp), &rsp_len) == GLSD_DISPATCH_OK);
        assert(rsp_cmd == (GLSD_CMD_READ | GLSD_RSP_BIT));
        fputs("DATA=", stdout); print_hex(rsp, rsp_len);
    }
    return 0;
}
