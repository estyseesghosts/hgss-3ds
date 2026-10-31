// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/math/rand.h: the game's 32/16-bit random-number helpers.
 *
 * HGSS has no such header (nothing in its tree names MATHRandContext32;
 * its lib/include/nitro/math/ holds only crc.h and dgt.h). The structs and
 * helpers below are ntrtwl NitroSDK's (include/nitro/math/rand.h): the
 * multiplier/addend constants fix the game's random sequence, and the port's
 * test checks it exactly (seed 12345 -> 0x62da0dd6, 0x33271c22, then 28 for
 * max 100).
 */

#ifndef HGSS_COMPAT_MATH_RAND_H
#define HGSS_COMPAT_MATH_RAND_H

#include <nitro/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u64 x;
    u64 mul;
    u64 add;
} MATHRandContext32;

typedef struct {
    u32 x;
    u32 mul;
    u32 add;
} MATHRandContext16;

static inline void MATH_InitRand32(MATHRandContext32 *context, u64 seed)
{
    context->x = seed;
    context->mul = (1566083941LL << 32) + 1812433253LL;
    context->add = 2531011;
}

static inline u32 MATH_Rand32(MATHRandContext32 *context, u32 max)
{
    context->x = context->mul * context->x + context->add;

    if (max == 0) {
        return (u32)(context->x >> 32);
    } else {
        return (u32)(((context->x >> 32) * max) >> 32);
    }
}

static inline void MATH_InitRand16(MATHRandContext16 *context, u32 seed)
{
    context->x = seed;
    context->mul = 1566083941LL;
    context->add = 2531011;
}

static inline u16 MATH_Rand16(MATHRandContext16 *context, u16 max)
{
    context->x = context->mul * context->x + context->add;

    if (max == 0) {
        return (u16)(context->x >> 16);
    } else {
        return (u16)(((context->x >> 16) * max) >> 16);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_MATH_RAND_H */
