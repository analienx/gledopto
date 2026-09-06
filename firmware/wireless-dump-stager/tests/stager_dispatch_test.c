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

static uint32_t u32le(const uint8_t *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int fake_read(void *user, uint32_t address, uint8_t *dst, uint32_t length) {
    (void)user;
    if (address > sizeof(flash_mem) || length > sizeof(flash_mem) - address) {
        return -1;
    }
    memcpy(dst, &flash_mem[address], length);
    return 0;
}

static void make_layout(void) {
    uint8_t *app = &flash_mem[GLSD_BANK_A_BASE];
    uint32_t i;
    uint32_t size = 240u;
    uint32_t crc;
    memset(flash_mem, 0xFF, sizeof(flash_mem));
    for (i = 0; i < size; ++i) {
        app[i] = (uint8_t)(i ^ 0x5Au);
    }
    put_u32le(&app[2], 0x26013001u);
    app[6] = 0x5Du; app[7] = 0x02u;
    app[8] = 0x4Bu; app[9] = 0x4Eu; app[10] = 0x4Cu; app[11] = 0x54u;
    put_u32le(&app[0x18], size);
    crc = glsd_telink_xcrc32_update(0xFFFFFFFFu, app, size - 4u);
    put_u32le(&app[size - 4u], crc);
    app[8] = 0x00u; /* post-OTA old-bank invalidation */
    flash_mem[GLSD_BANK_B_BASE + 8u] = 0x4Bu;
    flash_mem[GLSD_BANK_B_BASE + 9u] = 0x4Eu;
    flash_mem[GLSD_BANK_B_BASE + 10u] = 0x4Cu;
    flash_mem[GLSD_BANK_B_BASE + 11u] = 0x54u;
}

static glsd_stager_core_t init_core(void) {
    glsd_stager_core_t ctx;
    glsd_stager_env_t env;
    memset(&env, 0, sizeof(env));
    env.read = fake_read;
    env.flash_size = GLSD_FLASH_SIZE_512K;
    env.stager_base = GLSD_BANK_B_BASE;
    env.flash_jedec_id = 0x00123456u;
    env.stager_build_id = 0x20260905u;
    env.session_id = 0x11223344u;
    make_layout();
    assert(glsd_stager_core_init(&ctx, &env) == GLSD_STAGER_OK);
    return ctx;
}

static void test_crc_vector(void) {
    static const uint8_t s[] = "123456789";
    assert(glsd_transport_crc32(s, 9u) == 0xCBF43926u);
}

static void test_ping_info_and_read(void) {
    glsd_stager_core_t ctx = init_core();
    uint8_t req[32];
    uint8_t rsp[GLSD_DISPATCH_MAX_RESPONSE];
    uint8_t rsp_cmd = 0u;
    size_t n = 0u;
    uint32_t data_crc;

    req[0] = GLSD_DUMP_PROTOCOL_VERSION;
    put_u32le(&req[1], 0xA1B2C3D4u);
    assert(glsd_stager_dispatch(&ctx, GLSD_CMD_PING, req, 5u, &rsp_cmd, rsp, sizeof(rsp), &n) == GLSD_DISPATCH_OK);
    assert(rsp_cmd == (GLSD_CMD_PING | GLSD_RSP_BIT));
    assert(n == GLSD_PING_RESPONSE_SIZE);
    assert(rsp[0] == GLSD_DUMP_PROTOCOL_VERSION);
    assert(u32le(&rsp[1]) == 0xA1B2C3D4u);
    assert(u32le(&rsp[5]) == 0x20260905u);
    assert(u32le(&rsp[9]) == 0x11223344u);

    assert(glsd_stager_dispatch(&ctx, GLSD_CMD_INFO, NULL, 0u, &rsp_cmd, rsp, sizeof(rsp), &n) == GLSD_DISPATCH_OK);
    assert(rsp_cmd == (GLSD_CMD_INFO | GLSD_RSP_BIT));
    assert(n == GLSD_INFO_WIRE_SIZE);
    assert(rsp[0] == 1u);
    assert(u32le(&rsp[5]) == 0x11223344u);
    assert(u32le(&rsp[13]) == GLSD_FLASH_SIZE_512K);

    put_u32le(&req[0], 0x11223344u);
    put_u32le(&req[4], 7u);
    put_u32le(&req[8], 0u);
    req[12] = 48u;
    assert(glsd_stager_dispatch(&ctx, GLSD_CMD_READ, req, 13u, &rsp_cmd, rsp, sizeof(rsp), &n) == GLSD_DISPATCH_OK);
    assert(rsp_cmd == (GLSD_CMD_READ | GLSD_RSP_BIT));
    assert(n == 66u);
    assert(u32le(&rsp[0]) == 0x11223344u);
    assert(u32le(&rsp[4]) == 7u);
    assert(u32le(&rsp[8]) == 0u);
    assert(rsp[12] == 48u);
    assert(rsp[13 + 8] == 0x00u); /* exact raw invalidated marker */
    data_crc = u32le(&rsp[61]);
    assert(data_crc == glsd_transport_crc32(&rsp[13], 48u));
    assert(rsp[65] == 0u);
}

static void test_fail_closed_requests(void) {
    glsd_stager_core_t ctx = init_core();
    uint8_t req[13] = {0};
    uint8_t rsp[GLSD_DISPATCH_MAX_RESPONSE];
    uint8_t rsp_cmd = 0u;
    size_t n = 0u;
    put_u32le(&req[0], 0xDEADBEEFu);
    put_u32le(&req[4], 1u);
    put_u32le(&req[8], 0u);
    req[12] = 48u;
    assert(glsd_stager_dispatch(&ctx, GLSD_CMD_READ, req, sizeof(req), &rsp_cmd, rsp, sizeof(rsp), &n) == GLSD_DISPATCH_ERR_SESSION);
    assert(glsd_stager_dispatch(&ctx, 0x55u, NULL, 0u, &rsp_cmd, rsp, sizeof(rsp), &n) == GLSD_DISPATCH_ERR_UNSUPPORTED);
    assert(glsd_stager_dispatch(&ctx, GLSD_CMD_ABORT, NULL, 0u, &rsp_cmd, rsp, sizeof(rsp), &n) == GLSD_DISPATCH_OK);
    assert(n == 0u);
}

int main(void) {
    test_crc_vector();
    test_ping_info_and_read();
    test_fail_closed_requests();
    puts("stager_dispatch_test: PASS");
    return 0;
}
