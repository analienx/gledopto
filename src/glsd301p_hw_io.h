#ifndef GLSD301P_HW_IO_H
#define GLSD301P_HW_IO_H

#include <stdbool.h>
#include <stdint.h>

#include "glsd301p_uart_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Narrow hardware seam for the shared GL-SD-301P control plane.
 *
 * Firmware implements these against the pinned TLSR8258 SDK drivers; host
 * harnesses implement them as scripted stubs. No other hardware or SDK
 * dependency may enter the shared control modules.
 */

/* True while a UART TX DMA transfer is still owned by hardware. */
bool glsd301p_hw_uart_busy(void);

/*
 * Attempt one nonblocking DMA transfer of an already-guarded 6-byte frame.
 * Returns true when hardware accepted the transfer, false on DMA rejection
 * (caller retries with bounded work; never spins). The implementation owns
 * its aligned static DMA buffer and must never rewrite it while busy.
 */
bool glsd301p_hw_uart_send_frame(
    const uint8_t frame[GLSD301P_CONTROL_FRAME_SIZE]);

/* Sampled physical inputs, read once per IO service call by the target. */
bool glsd301p_hw_gpio_pc2_high(void);
bool glsd301p_hw_gpio_pb4_high(void);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_HW_IO_H */
