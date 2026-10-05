#include "glsd301p_health.h"

#include <stddef.h>

void glsd301p_health_init(glsd301p_health_t *health)
{
    if (health == NULL) {
        return;
    }
    health->net_state = GLSD301P_HEALTH_NET_DISCONNECTED;
    health->bdb_status = 0u;
}

void glsd301p_health_note_network(glsd301p_health_t *health,
                                 uint8_t net_state)
{
    if (health == NULL) {
        return;
    }
    health->net_state = (net_state <= GLSD301P_HEALTH_NET_JOINING)
        ? net_state
        : GLSD301P_HEALTH_NET_DISCONNECTED;
}

void glsd301p_health_note_bdb_status(glsd301p_health_t *health,
                                    uint8_t status)
{
    if (health == NULL) {
        return;
    }
    health->bdb_status = status;
}

static void glsd301p_health_put_u16(uint8_t out[], uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void glsd301p_health_put_u32(uint8_t out[], uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

void glsd301p_health_snapshot(const glsd301p_health_t *health,
                              const glsd301p_health_inputs_t *inputs,
                              uint8_t out[GLSD301P_HEALTH_SIZE])
{
    static const glsd301p_health_inputs_t zero_inputs = {false};
    const glsd301p_health_inputs_t *in;
    uint8_t flags = 0u;

    if (out == NULL) {
        return;
    }
    in = (inputs != NULL) ? inputs : &zero_inputs;

    if (in->runtime_ready) {
        flags |= GLSD301P_HEALTH_FLAG_RUNTIME_READY;
    }
    if (in->fault_latched) {
        flags |= GLSD301P_HEALTH_FLAG_FAULT_LATCHED;
    }
    if (in->mac_rx_on_idle) {
        flags |= GLSD301P_HEALTH_FLAG_MAC_RX_ON_IDLE;
    }
    if (in->off_pending) {
        flags |= GLSD301P_HEALTH_FLAG_OFF_PENDING;
    }
    if (in->normal_pending) {
        flags |= GLSD301P_HEALTH_FLAG_NORMAL_PENDING;
    }
    if (in->uart_busy) {
        flags |= GLSD301P_HEALTH_FLAG_UART_BUSY;
    }
    if (in->io_registered) {
        flags |= GLSD301P_HEALTH_FLAG_IO_REGISTERED;
    }
    if (in->boot_off_complete) {
        flags |= GLSD301P_HEALTH_FLAG_BOOT_OFF_COMPLETE;
    }

    out[0] = GLSD301P_HEALTH_VERSION;
    out[1] = (health != NULL) ? health->net_state : GLSD301P_HEALTH_NET_DISCONNECTED;
    out[2] = flags;
    out[3] = (health != NULL) ? health->bdb_status : 0u;
    glsd301p_health_put_u32(out + 4u, in->uptime_ms);
    glsd301p_health_put_u32(out + 8u, in->last_io_age_ms);
    glsd301p_health_put_u32(out + 12u, in->uart_age_ms);
    glsd301p_health_put_u32(out + 16u, in->io_max_gap_ms);
    glsd301p_health_put_u32(out + 20u, in->uart_deadline_faults);
    glsd301p_health_put_u32(out + 24u, in->timer_reg_faults);
    glsd301p_health_put_u32(out + 28u, in->parent_losses);
    glsd301p_health_put_u32(out + 32u, in->rejoin_starts);
    glsd301p_health_put_u32(out + 36u, in->rejoin_failures);
    glsd301p_health_put_u32(out + 40u, in->rejoin_successes);
    glsd301p_health_put_u16(out + 44u, in->exc_line);
    out[46] = in->exc_code;
    out[47] = 0u;
}
