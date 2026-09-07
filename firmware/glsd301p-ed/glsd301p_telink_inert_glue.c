/*
 * Minimal application-owned closure for Telink stack hooks that remain linked
 * even though the GL-SD-301P target does not expose the corresponding feature.
 *
 * Touchlink is disabled in app_cfg.h and no Touchlink cluster is registered.
 * The public Telink End Device stack nevertheless references this response-state
 * byte from its inter-PAN receive path. Keeping it permanently zero closes that
 * optional hook without importing Touchlink behavior.
 */

#include "tl_common.h"

u8 deviceInfoRsp = 0u;
