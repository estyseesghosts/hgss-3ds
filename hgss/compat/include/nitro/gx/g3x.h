// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/gx/g3x.h: geometry-FIFO interrupt condition, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/gx/g3x.h provides GXShading/GXFogBlend/GXFogSlope
 * but no GX_FIFOINTR_COND_*; the flat lib/NitroSDK/include/GX_g3x.h has them
 * (with the same values). The enum below is ntrtwl's
 * (include/nitro/gx/g3x.h). It is skipped when the flat header was already
 * included (GUARD_GX_G3X_H), so the two never define the enumerators twice
 * in that order. Note the reverse order (this wrapper first, flat GX_g3x.h
 * later in the same translation unit) would still collide; no translation
 * unit in the build does that today (HGSS's public g3x.h already defines
 * GXShading and friends, so mixing the two worlds there already fails).
 */

#ifndef HGSS_COMPAT_GX_G3X_H
#define HGSS_COMPAT_GX_G3X_H

#include_next <nitro/gx/g3x.h>

#ifndef GUARD_GX_G3X_H
typedef enum {
    GX_FIFOINTR_COND_DISABLE      = 0,
    GX_FIFOINTR_COND_UNDERHALF    = 1,
    GX_FIFOINTR_COND_EMPTY        = 2
} GXFifoIntrCond;
#endif

#endif /* HGSS_COMPAT_GX_G3X_H */
