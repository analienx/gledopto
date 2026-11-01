#ifndef GLSD301P_HOST_TYPES_H
#define GLSD301P_HOST_TYPES_H

#include <stdint.h>

/*
 * SDK scalar spellings for hosted harnesses. Test and stub translation
 * units use these (plus <stdbool.h>) to match SDK declarations without
 * pulling the SDK common header; only the staged SDK bodies themselves
 * compile against the shim tl_common.h.
 */

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif /* GLSD301P_HOST_TYPES_H */
