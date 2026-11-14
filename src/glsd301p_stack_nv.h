#ifndef GLSD301P_STACK_NV_H
#define GLSD301P_STACK_NV_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One-time clean start for the Zigbee stack's persistent storage.
 *
 * This firmware replaces a vendor Router image in place, so the stack's NV
 * modules arrive holding another firmware line's records. The SWire dump of
 * the first migrated unit (2026-11-12) shows what that means in practice:
 * its NV layout matches this SDK exactly, the vendor's 192-byte ZB_INFO is
 * unreadable here (so the unit counts as factory-new and steers), but the
 * vendor's 81-byte security record (ss_ib: trust-centre address, the live
 * network key as active material) and its 36-byte TC key pair (a VERIFIED
 * unique link key no backup of the current coordinator can reproduce) have
 * exactly this SDK's sizes and stay loadable. That unit associated but never
 * completed a join and never wrote a single NV record of its own. Active
 * sectors were 13-48% full, so sector compaction is not the issue; foreign
 * security state is. An empty module set removes all of it.
 *
 * Policy: a marker item in the application NV module names the firmware
 * line whose stack state the modules hold. If it is missing or different,
 * every listed module is erased, each erase is verified blank, and only
 * then is the marker written and read back. The marker's own module is
 * erased first: that is the commit point, so an interrupted wipe leaves no
 * marker and the next boot repeats it. The NWK frame counter module is
 * never listed (the outgoing counter must not regress on the network).
 *
 * The same routine with force=true is the reset gesture's wipe, so the
 * gesture always ends in genuinely empty stack storage, also on a device
 * that already counts as factory-new.
 *
 * Runs in user_init before zb_init, after the power-stage OFF vector has
 * been issued. Hardware access goes through the ops seam so the policy is
 * identical in firmware and hosted tests.
 */

#define GLSD301P_STACK_NV_MARKER_SIZE 8u
#define GLSD301P_STACK_NV_MAX_MODULES 8u

typedef struct {
    /* false when the marker item is absent or unreadable. */
    bool (*marker_read)(uint8_t out[GLSD301P_STACK_NV_MARKER_SIZE]);
    bool (*marker_write)(const uint8_t marker[GLSD301P_STACK_NV_MARKER_SIZE]);
    /* index is a position in the caller's module list, 0..module_count-1;
     * index 0 is the module that holds the marker. */
    bool (*module_erase)(uint8_t index);
    bool (*module_blank)(uint8_t index);
} glsd301p_stack_nv_ops_t;

typedef enum {
    /* Marker matched: storage already belongs to this firmware line. */
    GLSD301P_STACK_NV_CLEAN = 0,
    /* Wiped, verified blank, marker written and read back. */
    GLSD301P_STACK_NV_WIPED = 1,
    /* An erase failed or left data behind; no marker written (retried on
     * the next boot). */
    GLSD301P_STACK_NV_ERASE_FAILED = 2,
    /* Storage is blank but the marker could not be persisted (the wipe
     * repeats on the next boot). */
    GLSD301P_STACK_NV_MARKER_FAILED = 3,
    GLSD301P_STACK_NV_BAD_ARGS = 4
} glsd301p_stack_nv_result_t;

/* The marker of this firmware line (ASCII tag plus NV epoch). */
void glsd301p_stack_nv_marker(uint8_t out[GLSD301P_STACK_NV_MARKER_SIZE]);

glsd301p_stack_nv_result_t glsd301p_stack_nv_ensure_clean(
    const glsd301p_stack_nv_ops_t *ops, uint8_t module_count, bool force);

#ifdef __cplusplus
}
#endif

#endif /* GLSD301P_STACK_NV_H */
