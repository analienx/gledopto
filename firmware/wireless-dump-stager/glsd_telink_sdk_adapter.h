#ifndef GLSD_TELINK_SDK_ADAPTER_H
#define GLSD_TELINK_SDK_ADAPTER_H

#include "glsd_stager_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register the read-only 0xFC00 extraction cluster on endpoint 11.
 *
 * A real Telink build must define GLSD_TELINK_SDK and provide the pinned
 * TLSR8258 Zigbee SDK headers/libraries. The host/native build intentionally
 * exposes only a fail-closed stub so CI never pretends to be a target build.
 *
 * Returns 0 on successful target registration, nonzero otherwise.
 */
int glsd_telink_sdk_adapter_register(glsd_stager_core_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
