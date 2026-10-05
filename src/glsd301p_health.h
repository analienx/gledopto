#ifndef GLSD301P_HEALTH_H
#define GLSD301P_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RAM-only GLSD-specific health snapshot for the GL-SD-301P End Device.
 *
 * A read-only Basic cluster octet-string attribute (0xFF10) carries a
 * 48-byte version-2 little-endian payload refreshed from RAM state by the
 * owned 1 s health event. There are no NVM writes, no automatic reports,
 * no enable commands, and no operational control surface: the attribute
 * access is read-only and the snapshot builder below is a pure encoder
 * over caller-latched inputs plus two sticky bytes (network state and
 * latest BDB/startup status). Counter saturation and boot reset are
 * source properties (every counter below saturates at its origin and
 * lives in RAM); uptime is intentionally modulo uint32.
 *
 * Layout (offset, type, meaning):
 *   0  u8   version (2)
 *   1  u8   network: 0 disconnected, 1 joined, 2 joining
 *   2  u8   flags (bits: 0 runtime ready, 1 fault latched,
 *               2 actual MAC rxOnWhenIdle, 3 OFF pending,
 *               4 normal frame pending, 5 UART busy, 6 IO event
 *               registered, 7 boot OFF TX complete)
 *   3  u8   latest BDB/startup status
 *   4  u32  uptime ms, modulo uint32
 *   8  u32  last IO age; 0xFFFFFFFF before first service
 *  12  u32  oldest pending/in-flight UART age; 0 when idle
 *  16  u32  maximum IO gap
 *  20  u32  UART deadline faults
 *  24  u32  timer registration faults
 *  28  u32  parent-loss count
 *  32  u32  rejoin-start count
 *  36  u32  rejoin-failure count
 *  40  u32  rejoin-success count
 *  44  u16  SDK exception line
 *  46  u8   SDK exception code
 *  47  u8   reserved, zero
 */

#define GLSD301P_HEALTH_VERSION 2u
#define GLSD301P_HEALTH_SIZE 48u
#define GLSD301P_HEALTH_ATTR_ID 0xFF10u

#define GLSD301P_HEALTH_NET_DISCONNECTED 0u
#define GLSD301P_HEALTH_NET_JOINED 1u
#define GLSD301P_HEALTH_NET_JOINING 2u

#define GLSD301P_HEALTH_FLAG_RUNTIME_READY (1u << 0)
#define GLSD301P_HEALTH_FLAG_FAULT_LATCHED (1u << 1)
#define GLSD301P_HEALTH_FLAG_MAC_RX_ON_IDLE (1u << 2)
#define GLSD301P_HEALTH_FLAG_OFF_PENDING (1u << 3)
#define GLSD301P_HEALTH_FLAG_NORMAL_PENDING (1u << 4)
#define GLSD301P_HEALTH_FLAG_UART_BUSY (1u << 5)
#define GLSD301P_HEALTH_FLAG_IO_REGISTERED (1u << 6)
#define GLSD301P_HEALTH_FLAG_BOOT_OFF_COMPLETE (1u << 7)

typedef struct {
    uint8_t net_state;
    uint8_t bdb_status;
} glsd301p_health_t;

typedef struct {
    bool runtime_ready;
    bool fault_latched;
    bool mac_rx_on_idle;
    bool off_pending;
    bool normal_pending;
    bool uart_busy;
    bool io_registered;
    bool boot_off_complete;
    uint32_t uptime_ms;
    uint32_t last_io_age_ms;
    uint32_t uart_age_ms;
    uint32_t io_max_gap_ms;
    uint32_t uart_deadline_faults;
    uint32_t timer_reg_faults;
    uint32_t parent_losses;
    uint32_t rejoin_starts;
    uint32_t rejoin_failures;
    uint32_t rejoin_successes;
    uint16_t exc_line;
    uint8_t exc_code;
} glsd301p_health_inputs_t;

void glsd301p_health_init(glsd301p_health_t *health);

/* Out-of-range network states clamp to DISCONNECTED. */
void glsd301p_health_note_network(glsd301p_health_t *health,
                                 uint8_t net_state);
void glsd301p_health_note_bdb_status(glsd301p_health_t *health,
                                    uint8_t status);

/*
 * Encode one coherent snapshot in a single pass. A NULL health latches
 * zero sticky bytes; NULL inputs encode as zero; a NULL out is a no-op.
 */
void glsd301p_health_snapshot(const glsd301p_health_t *health,
                              const glsd301p_health_inputs_t *inputs,
                              uint8_t out[GLSD301P_HEALTH_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_HEALTH_H */
