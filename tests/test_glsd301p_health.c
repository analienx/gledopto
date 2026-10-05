#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "glsd301p_health.h"

typedef char health_size_is_48[(GLSD301P_HEALTH_SIZE == 48u) ? 1 : -1];

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void test_defaults(void)
{
    glsd301p_health_t h;
    glsd301p_health_inputs_t in;
    uint8_t out[GLSD301P_HEALTH_SIZE];

    memset(&in, 0, sizeof(in));
    glsd301p_health_init(&h);
    assert(h.net_state == GLSD301P_HEALTH_NET_DISCONNECTED);
    assert(h.bdb_status == 0u);
    glsd301p_health_snapshot(&h, &in, out);
    assert(out[0] == 2u);
    assert(out[1] == GLSD301P_HEALTH_NET_DISCONNECTED);
    assert(out[2] == 0u);
    assert(out[3] == 0u);
    assert(get_u32(out + 4u) == 0u);
    assert(out[47] == 0u);
    glsd301p_health_init(NULL);
}

static void test_layout_and_endian(void)
{
    glsd301p_health_t h;
    glsd301p_health_inputs_t in;
    uint8_t out[GLSD301P_HEALTH_SIZE];

    memset(&in, 0, sizeof(in));
    glsd301p_health_init(&h);
    glsd301p_health_note_network(&h, GLSD301P_HEALTH_NET_JOINED);
    glsd301p_health_note_bdb_status(&h, 0xA5u);
    in.runtime_ready = true;
    in.mac_rx_on_idle = true;
    in.io_registered = true;
    in.boot_off_complete = true;
    in.uptime_ms = 0x12345678u;
    in.last_io_age_ms = 0xFFFFFFFFu;
    in.uart_age_ms = 0x00000001u;
    in.io_max_gap_ms = 0x0000FF00u;
    in.uart_deadline_faults = 0xDEADBEEFu;
    in.timer_reg_faults = 0x00000002u;
    in.parent_losses = 0x00000003u;
    in.rejoin_starts = 0x00000004u;
    in.rejoin_failures = 0x00000005u;
    in.rejoin_successes = 0x00000006u;
    in.exc_line = 0xBEEFu;
    in.exc_code = 0x42u;

    glsd301p_health_snapshot(&h, &in, out);
    assert(out[0] == 2u);
    assert(out[1] == GLSD301P_HEALTH_NET_JOINED);
    assert(out[2] == (uint8_t)(GLSD301P_HEALTH_FLAG_RUNTIME_READY |
                               GLSD301P_HEALTH_FLAG_MAC_RX_ON_IDLE |
                               GLSD301P_HEALTH_FLAG_IO_REGISTERED |
                               GLSD301P_HEALTH_FLAG_BOOT_OFF_COMPLETE));
    assert(out[3] == 0xA5u);
    assert(get_u32(out + 4u) == 0x12345678u);
    assert(out[4] == 0x78u && out[5] == 0x56u && out[6] == 0x34u &&
           out[7] == 0x12u);
    assert(get_u32(out + 8u) == 0xFFFFFFFFu);
    assert(get_u32(out + 12u) == 0x00000001u);
    assert(get_u32(out + 16u) == 0x0000FF00u);
    assert(get_u32(out + 20u) == 0xDEADBEEFu);
    assert(get_u32(out + 24u) == 0x00000002u);
    assert(get_u32(out + 28u) == 0x00000003u);
    assert(get_u32(out + 32u) == 0x00000004u);
    assert(get_u32(out + 36u) == 0x00000005u);
    assert(get_u32(out + 40u) == 0x00000006u);
    assert(out[44] == 0xEFu && out[45] == 0xBEu);
    assert(out[46] == 0x42u);
    assert(out[47] == 0u);
}

static void test_flag_bits(void)
{
    glsd301p_health_t h;
    glsd301p_health_inputs_t in;
    uint8_t out[GLSD301P_HEALTH_SIZE];
    unsigned i;

    glsd301p_health_init(&h);
    for (i = 0u; i < 8u; i++) {
        memset(&in, 0, sizeof(in));
        switch (i) {
        case 0u: in.runtime_ready = true; break;
        case 1u: in.fault_latched = true; break;
        case 2u: in.mac_rx_on_idle = true; break;
        case 3u: in.off_pending = true; break;
        case 4u: in.normal_pending = true; break;
        case 5u: in.uart_busy = true; break;
        case 6u: in.io_registered = true; break;
        default: in.boot_off_complete = true; break;
        }
        glsd301p_health_snapshot(&h, &in, out);
        assert(out[2] == (uint8_t)(1u << i));
    }

    memset(&in, 0, sizeof(in));
    in.runtime_ready = true;
    in.fault_latched = true;
    in.mac_rx_on_idle = true;
    in.off_pending = true;
    in.normal_pending = true;
    in.uart_busy = true;
    in.io_registered = true;
    in.boot_off_complete = true;
    glsd301p_health_snapshot(&h, &in, out);
    assert(out[2] == 0xFFu);
}

static void test_network_notes_and_clamp(void)
{
    glsd301p_health_t h;
    glsd301p_health_inputs_t in;
    uint8_t out[GLSD301P_HEALTH_SIZE];

    memset(&in, 0, sizeof(in));
    glsd301p_health_init(&h);
    glsd301p_health_note_network(&h, GLSD301P_HEALTH_NET_JOINING);
    glsd301p_health_snapshot(&h, &in, out);
    assert(out[1] == GLSD301P_HEALTH_NET_JOINING);
    glsd301p_health_note_network(&h, 0x03u);
    glsd301p_health_snapshot(&h, &in, out);
    assert(out[1] == GLSD301P_HEALTH_NET_DISCONNECTED);
    glsd301p_health_note_network(&h, 0xFFu);
    glsd301p_health_snapshot(&h, &in, out);
    assert(out[1] == GLSD301P_HEALTH_NET_DISCONNECTED);
    glsd301p_health_note_network(NULL, GLSD301P_HEALTH_NET_JOINED);
    glsd301p_health_note_bdb_status(NULL, 1u);
}

static void test_saturation_passthrough_and_coherency(void)
{
    glsd301p_health_t h;
    glsd301p_health_inputs_t in;
    uint8_t first[GLSD301P_HEALTH_SIZE];
    uint8_t second[GLSD301P_HEALTH_SIZE];

    /* Saturated sources encode bit-exact; nothing truncates or wraps. */
    memset(&in, 0, sizeof(in));
    in.runtime_ready = true;
    in.fault_latched = true;
    in.mac_rx_on_idle = true;
    in.off_pending = true;
    in.normal_pending = true;
    in.uart_busy = true;
    in.io_registered = true;
    in.boot_off_complete = true;
    in.uptime_ms = 0xFFFFFFFFu;
    in.last_io_age_ms = 0xFFFFFFFFu;
    in.uart_age_ms = 0xFFFFFFFFu;
    in.io_max_gap_ms = 0xFFFFFFFFu;
    in.uart_deadline_faults = 0xFFFFFFFFu;
    in.timer_reg_faults = 0xFFFFFFFFu;
    in.parent_losses = 0xFFFFFFFFu;
    in.rejoin_starts = 0xFFFFFFFFu;
    in.rejoin_failures = 0xFFFFFFFFu;
    in.rejoin_successes = 0xFFFFFFFFu;
    in.exc_line = 0xFFFFu;
    in.exc_code = 0xFFu;
    glsd301p_health_init(&h);
    glsd301p_health_note_network(&h, GLSD301P_HEALTH_NET_JOINED);
    glsd301p_health_note_bdb_status(&h, 0xFFu);
    glsd301p_health_snapshot(&h, &in, first);
    assert(first[0] == 2u);
    assert(first[1] == GLSD301P_HEALTH_NET_JOINED);
    assert(first[2] == 0xFFu);
    assert(first[3] == 0xFFu);
    assert(get_u32(first + 20u) == 0xFFFFFFFFu);
    assert(get_u32(first + 24u) == 0xFFFFFFFFu);
    assert(get_u32(first + 28u) == 0xFFFFFFFFu);
    assert(first[44] == 0xFFu && first[45] == 0xFFu);
    assert(first[46] == 0xFFu);
    assert(first[47] == 0u);

    /* Same inputs twice: byte-identical snapshots (single-pass build). */
    glsd301p_health_snapshot(&h, &in, second);
    assert(memcmp(first, second, sizeof(first)) == 0);
}

static void test_null_safety(void)
{
    glsd301p_health_t h;
    glsd301p_health_inputs_t in;
    uint8_t out[GLSD301P_HEALTH_SIZE];

    memset(&in, 0, sizeof(in));
    memset(out, 0xA5, sizeof(out));
    glsd301p_health_init(&h);
    glsd301p_health_snapshot(NULL, &in, out);
    assert(out[0] == 2u && out[1] == 0u && out[3] == 0u);
    glsd301p_health_snapshot(&h, NULL, out);
    assert(out[0] == 2u && out[2] == 0u && get_u32(out + 4u) == 0u);
    glsd301p_health_snapshot(NULL, NULL, out);
    assert(out[0] == 2u);
    glsd301p_health_snapshot(&h, &in, NULL);
}

int main(void)
{
    test_defaults();
    test_layout_and_endian();
    test_flag_bits();
    test_network_notes_and_clamp();
    test_saturation_passthrough_and_coherency();
    test_null_safety();
    printf("GLSD301P_HEALTH=PASS\n");
    return 0;
}
