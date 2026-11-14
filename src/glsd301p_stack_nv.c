#include "glsd301p_stack_nv.h"

#include <stddef.h>
#include <string.h>

/* Bump the last two bytes whenever stack NV written by an older build of
 * this firmware line must not be reused. */
static const uint8_t g_marker[GLSD301P_STACK_NV_MARKER_SIZE] = {
    'G', 'L', 'S', 'D', 'N', 'V', 0x00u, 0x01u,
};

void glsd301p_stack_nv_marker(uint8_t out[GLSD301P_STACK_NV_MARKER_SIZE])
{
    if (out != NULL) {
        memcpy(out, g_marker, sizeof(g_marker));
    }
}

static bool glsd301p_stack_nv_marker_matches(
    const glsd301p_stack_nv_ops_t *ops)
{
    uint8_t stored[GLSD301P_STACK_NV_MARKER_SIZE];

    memset(stored, 0, sizeof(stored));
    return ops->marker_read(stored) &&
           memcmp(stored, g_marker, sizeof(g_marker)) == 0;
}

glsd301p_stack_nv_result_t glsd301p_stack_nv_ensure_clean(
    const glsd301p_stack_nv_ops_t *ops, uint8_t module_count, bool force)
{
    uint8_t i;
    bool erased = true;

    if (ops == NULL || ops->marker_read == NULL ||
        ops->marker_write == NULL || ops->module_erase == NULL ||
        ops->module_blank == NULL || module_count == 0u ||
        module_count > GLSD301P_STACK_NV_MAX_MODULES) {
        return GLSD301P_STACK_NV_BAD_ARGS;
    }

    if (!force && glsd301p_stack_nv_marker_matches(ops)) {
        return GLSD301P_STACK_NV_CLEAN;
    }

    /* Index 0 (the marker's module) goes first: once it is gone, an
     * interrupted wipe is repeated in full on the next boot. Every module
     * is attempted even after a failure so as much as possible is clean. */
    for (i = 0u; i < module_count; i++) {
        if (!ops->module_erase(i)) {
            erased = false;
        }
    }
    for (i = 0u; i < module_count; i++) {
        if (!ops->module_blank(i)) {
            erased = false;
        }
    }
    if (!erased) {
        return GLSD301P_STACK_NV_ERASE_FAILED;
    }

    if (!ops->marker_write(g_marker) ||
        !glsd301p_stack_nv_marker_matches(ops)) {
        return GLSD301P_STACK_NV_MARKER_FAILED;
    }
    return GLSD301P_STACK_NV_WIPED;
}
