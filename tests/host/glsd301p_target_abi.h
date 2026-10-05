/*
 * R26 compiler-verified target layout metadata. These sizes are the
 * packed TC32 (-fpack-struct) foundation record/command layouts plus
 * pool geometry. tools/build_glsd301p_ed_tc32.sh #includes this
 * header in its ABI probe and _Static_asserts every value against
 * the real pinned-SDK target compile in both TU flag contexts, so
 * any drift fails the TC32 build itself. The packed host binary
 * (GLSD301P_PACKED_TARGET_LAYOUT) asserts its own structs against
 * these values; a layout mismatch fails that compile. Host
 * (unpacked -m32) values are intentionally NOT listed here — see
 * test_r23_host_foundation_layout for the recorded host/target
 * deltas.
 */
#ifndef GLSD301P_TARGET_ABI_H
#define GLSD301P_TARGET_ABI_H

/* Packed record sizes (bytes). */
#define GLSD301P_TARGET_WRITE_REC 7u
#define GLSD301P_TARGET_REPORT_REC 7u
#define GLSD301P_TARGET_WRITE_RSP_REC 3u
#define GLSD301P_TARGET_READ_RSP_REC 8u
#define GLSD301P_TARGET_CFG_REC 14u
#define GLSD301P_TARGET_CFG_RSP_REC 4u
#define GLSD301P_TARGET_READCFG_REC 3u
#define GLSD301P_TARGET_READCFGRSP_REC 15u

/* Packed flexible command wrappers (numAttr + flex array). */
#define GLSD301P_TARGET_WRITE_CMD 1u
#define GLSD301P_TARGET_REPORT_CMD 1u
#define GLSD301P_TARGET_WRITE_RSP_CMD 1u
#define GLSD301P_TARGET_READ_RSP_CMD 1u
#define GLSD301P_TARGET_CFG_CMD 1u
#define GLSD301P_TARGET_CFG_RSP_CMD 1u
#define GLSD301P_TARGET_READCFG_CMD 1u
#define GLSD301P_TARGET_READCFGRSP_CMD 1u
#define GLSD301P_TARGET_DEFAULT_RSP_CMD 2u

/* Pool geometry (identical management on host and target). */
#define GLSD301P_TARGET_POOL_G0 24u
#define GLSD301P_TARGET_POOL_G1 60u
#define GLSD301P_TARGET_POOL_G2 152u
#define GLSD301P_TARGET_POOL_G3 512u
#define GLSD301P_TARGET_LARGE_BUFFER 504u

#endif
