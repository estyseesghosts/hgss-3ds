// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/fx/fx_mtx43.h: the 4x3 helpers, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/fx/fx_mtx43.h covers identity/rotation/scale/
 * concat/trans-apply (the underscore bodies are C under SDK_PORT_3DS via
 * patch 0101, and MTX_Copy43To44_ too). MTX_Copy43To33 is ntrtwl's inline
 * over MI_Copy36B (the 3x3 part is the first 36 bytes); MTX_Copy43To44 is
 * the same over HGSS's MTX_Copy43To44_. MTX_Transpose43 / MTX_RotZ43 have no
 * HGSS source; their bodies live in hgss/compat/src/sdk_standins.c and
 * match ntrtwl's (libraries/fx/src/fx_mtx43.c): alias-safe transpose plus a
 * zeroed translation row, and the Z-rotation layout of MTX_RotZ33_ with a
 * zeroed translation row.
 */

#ifndef HGSS_COMPAT_FX_MTX43_H
#define HGSS_COMPAT_FX_MTX43_H

#include_next <nitro/fx/fx_mtx43.h>
#include <nitro/mi/memory.h>

void MTX_Copy43To44_(const MtxFx43 *pSrc, MtxFx44 *pDst);

static inline void MTX_Copy43To33(const MtxFx43 *pSrc, MtxFx33 *pDst)
{
    MI_Copy36B(pSrc, pDst);
}

static inline void MTX_Copy43To44(const MtxFx43 *pSrc, MtxFx44 *pDst)
{
    MTX_Copy43To44_(pSrc, pDst);
}

#ifdef __cplusplus
extern "C" {
#endif

void MTX_Transpose43(const MtxFx43 *pSrc, MtxFx43 *pDst);
void MTX_RotZ43(MtxFx43 *pDst, fx32 sinVal, fx32 cosVal);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_FX_MTX43_H */
