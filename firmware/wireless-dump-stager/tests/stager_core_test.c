#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../glsd_stager_core.h"

static uint8_t flash_mem[GLSD_FLASH_SIZE_512K];
static uint32_t max_read_end;
static uint32_t min_read_addr = 0xFFFFFFFFu;

static void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int fake_read(void *user, uint32_t address, uint8_t *dst, uint32_t length) {
    (void)user;
    if (address > sizeof(flash_mem) || length > sizeof(flash_mem) - address) {
        return -1;
    }
    if (address < min_read_addr) {
        min_read_addr = address;
    }
    if (address + length > max_read_end) {
        max_read_end = address + length;
    }
    memcpy(dst, &flash_mem[address], length);
    return 0;
}

static uint32_t crc_valid_form(const uint8_t *app, uint32_t size) {
    uint8_t copy[512];
    assert(size <= sizeof(copy));
    memcpy(copy, app, size);
    copy[8] = 0x4Bu;
    return glsd_telink_xcrc32_update(0xFFFFFFFFu, copy, size - 4u);
}

static void build_old_app(uint32_t base, uint32_t size, uint8_t marker0) {
    uint8_t *app = &flash_mem[base];
    uint32_t i;
    assert(size <= 512u);
    for (i = 0; i < size; ++i) {
        app[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    }
    put_u32le(&app[2], 0x26013001u);
    app[6] = 0x5Du;
    app[7] = 0x02u;
    app[8] = marker0;
    app[9] = 0x4Eu;
    app[10] = 0x4Cu;
    app[11] = 0x54u;
    app[0x12] = 0x4Fu;
    app[0x13] = 0x12u;
    app[0x14] = 0x16u;
    app[0x15] = 0x14u;
    put_u32le(&app[0x18], size);
    put_u32le(&app[size - 4u], crc_valid_form(app, size));
}

static glsd_stager_env_t base_env(void) {
    glsd_stager_env_t env;
    memset(&env, 0, sizeof(env));
    env.read = fake_read;
    env.flash_jedec_id = 0x00123456u;
    env.flash_size = GLSD_FLASH_SIZE_512K;
    env.stager_base = GLSD_BANK_B_BASE;
    env.stager_build_id = 0x20260905u;
    env.session_id = 0x11223344u;
    return env;
}

static void prepare_valid_layout(void) {
    memset(flash_mem, 0xFF, sizeof(flash_mem));
    max_read_end = 0u;
    min_read_addr = 0xFFFFFFFFu;
    build_old_app(GLSD_BANK_A_BASE, 240u, 0x00u);
    flash_mem[GLSD_BANK_B_BASE + 8u] = 0x4Bu;
    flash_mem[GLSD_BANK_B_BASE + 9u] = 0x4Eu;
    flash_mem[GLSD_BANK_B_BASE + 10u] = 0x4Cu;
    flash_mem[GLSD_BANK_B_BASE + 11u] = 0x54u;
}

static void test_valid_init_info_and_raw_read(void) {
    glsd_stager_core_t ctx;
    glsd_info_response_t info;
    glsd_stager_env_t env;
    uint8_t data[48];
    prepare_valid_layout();
    env = base_env();
    assert(glsd_stager_core_init(&ctx, &env) == GLSD_STAGER_OK);
    assert(ctx.old_base == GLSD_BANK_A_BASE);
    assert(ctx.old_declared_size == 240u);
    assert(ctx.old_tail_crc32 == ctx.old_reconstructed_crc32);
    assert(glsd_stager_core_info(&ctx, &info) == GLSD_STAGER_OK);
    assert(info.inferred_stager_base == GLSD_BANK_B_BASE);
    assert(info.inferred_old_base == GLSD_BANK_A_BASE);
    assert(info.allowed_read_start == 0u);
    assert(info.allowed_read_length == 240u);
    assert(info.rollback_compiled == 0u);
    assert(glsd_stager_core_read(&ctx, 0u, sizeof(data), data) == GLSD_STAGER_OK);
    /* Extraction returns the exact post-OTA raw bank, not silently reconstructed bytes. */
    assert(data[8] == 0x00u);
    assert(data[9] == 0x4Eu && data[10] == 0x4Cu && data[11] == 0x54u);
    assert(glsd_stager_core_read(&ctx, 220u, 21u, data) == GLSD_STAGER_ERR_RANGE);
    assert(glsd_stager_core_read(&ctx, 0u, 49u, data) == GLSD_STAGER_ERR_ARGUMENT);
    /* Core never had reason to touch NV/MAC/factory areas in this layout. */
    assert(max_read_end <= GLSD_BANK_B_BASE + 12u);
}

static void test_valid_old_marker_is_rejected(void) {
    glsd_stager_core_t ctx;
    glsd_stager_env_t env;
    prepare_valid_layout();
    build_old_app(GLSD_BANK_A_BASE, 240u, 0x4Bu);
    env = base_env();
    assert(glsd_stager_core_init(&ctx, &env) == GLSD_STAGER_ERR_MARKER);
}

static void test_bad_stager_marker_is_rejected(void) {
    glsd_stager_core_t ctx;
    glsd_stager_env_t env;
    prepare_valid_layout();
    flash_mem[GLSD_BANK_B_BASE + 8u] = 0xFFu;
    env = base_env();
    assert(glsd_stager_core_init(&ctx, &env) == GLSD_STAGER_ERR_MARKER);
}

static void test_bad_crc_is_rejected(void) {
    glsd_stager_core_t ctx;
    glsd_stager_env_t env;
    prepare_valid_layout();
    flash_mem[100] ^= 0x01u;
    env = base_env();
    assert(glsd_stager_core_init(&ctx, &env) == GLSD_STAGER_ERR_CRC);
}

static void test_bad_size_is_rejected(void) {
    glsd_stager_core_t ctx;
    glsd_stager_env_t env;
    prepare_valid_layout();
    put_u32le(&flash_mem[0x18], GLSD_APP_LIMIT);
    env = base_env();
    assert(glsd_stager_core_init(&ctx, &env) == GLSD_STAGER_ERR_SIZE);
}

static void test_wrong_geometry_is_rejected(void) {
    glsd_stager_core_t ctx;
    glsd_stager_env_t env;
    prepare_valid_layout();
    env = base_env();
    env.flash_size = 0x100000u;
    assert(glsd_stager_core_init(&ctx, &env) == GLSD_STAGER_ERR_GEOMETRY);
    env = base_env();
    env.stager_base = 0x20000u;
    assert(glsd_stager_core_init(&ctx, &env) == GLSD_STAGER_ERR_GEOMETRY);
}

int main(void) {
    test_valid_init_info_and_raw_read();
    test_valid_old_marker_is_rejected();
    test_bad_stager_marker_is_rejected();
    test_bad_crc_is_rejected();
    test_bad_size_is_rejected();
    test_wrong_geometry_is_rejected();
    puts("stager_core_test: PASS");
    return 0;
}
