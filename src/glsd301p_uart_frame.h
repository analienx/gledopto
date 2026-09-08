#ifndef GLSD301P_UART_FRAME_H
#define GLSD301P_UART_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLSD301P_CONTROL_FRAME_SIZE 6u

/* Confirmed control families. Their high-level semantics are intentionally
 * limited to what is stated in the clean-room interoperability specification. */
#define GLSD301P_CONTROL_FAMILY_LEVEL     0x01u
#define GLSD301P_CONTROL_FAMILY_OPERATION 0x02u

/**
 * Encode one confirmed GL-SD-301P controller control frame.
 *
 * Wire format:
 *   A5 5A <family> <value> 04 AA
 *
 * This function performs framing only. It does not decide which operation
 * value, level policy, transition sequence, startup message, or electrical-OFF
 * sequence is correct. Those remain the responsibility of a higher layer after
 * the relevant interoperability fields are confirmed.
 *
 * Returns false for an unsupported family or invalid output buffer.
 */
bool glsd301p_control_frame_encode(uint8_t family,
                                   uint8_t value,
                                   uint8_t out[GLSD301P_CONTROL_FRAME_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_UART_FRAME_H */
