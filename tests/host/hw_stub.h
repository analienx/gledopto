#ifndef GLSD301P_HOST_STUB_H
#define GLSD301P_HOST_STUB_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Scripted narrow stubs for hosted harnesses: the 16 MHz tick clock, IRQ
 * lock (single-threaded host: always succeeds), the SDK exception record,
 * and the glsd301p_hw_io seam (UART busy/send, PC2/PB4 levels).
 */

/* 16 MHz tick clock (u32, wraps like hardware). 1 ms == 16000 ticks. */
#define HOST_TICKS_PER_MS 16000u

void host_clock_set(uint32_t tick);
void host_clock_advance(uint32_t delta_ticks);
uint32_t host_clock_get(void);

/* SDK exception record (same shape/semantics as SDK ev.c). */
uint16_t host_exception_line(void);
uint8_t host_exception_code(void);
uint32_t host_exception_count(void);
void host_exception_reset(void);

/* UART/GPIO scripting. */
void host_uart_set_busy(bool busy);
void host_uart_set_send_accepts(bool accepts);
uint32_t host_uart_send_attempts(void);
bool host_uart_last_frame(uint8_t out[6]);
void host_gpio_set(bool pc2_high, bool pb4_high);

/*
 * Accepted-frame log: every DMA start the stub ACCEPTS is appended (up to
 * HOST_UART_ACCEPT_LOG_MAX entries; the total count never saturates).
 * Rejected attempts are counted by host_uart_send_attempts() but not logged.
 */
#define HOST_UART_ACCEPT_LOG_MAX 64u
uint32_t host_uart_accepted_count(void);
uint32_t host_uart_accepted_logged(void);
bool host_uart_accepted_frame(uint32_t idx, uint8_t out[6]);

/* Reset stub scripting/state (clock keeps its value; set it explicitly). */
void host_stub_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_HOST_STUB_H */
