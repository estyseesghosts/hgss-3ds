// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/fx/fx_mtx33.h: the 3x3 helpers, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/fx/fx_mtx33.h covers identity/rotation/concat/
 * mult-vec/scale-apply (the underscore bodies are C under SDK_PORT_3DS via
 * patch 0101). The names below have no HGSS source; their bodies live in
 * hgss/compat/src/sdk_standins.c and match ntrtwl's
 * (libraries/fx/src/fx_mtx33.c): Copy33To43 zeroes the translation row,
 * Copy33To44 sets the fourth column to 0 (row 3: FX32_ONE), Transpose33 is
 * alias-safe, Scale33 writes the diagonal, RotAxis33 is ntrtwl's C verbatim.
 */

#ifndef HGSS_COMPAT_FX_MTX33_H
#define HGSS_COMPAT_FX_MTX33_H

#include_next <nitro/fx/fx_mtx33.h>

#ifdef __cplusplus
extern "C" {
#endif

void MTX_Copy33To43(const MtxFx33 *pSrc, MtxFx43 *pDst);
void MTX_Copy33To44(const MtxFx33 *pSrc, MtxFx44 *pDst);
void MTX_Transpose33(const MtxFx33 *pSrc, MtxFx33 *pDst);
void MTX_Scale33(MtxFx33 *pDst, fx32 x, fx32 y, fx32 z);
void MTX_RotAxis33(MtxFx33 *pDst, const VecFx32 *vec, fx32 sinVal, fx32 cosVal);
int MTX_Inverse33(const MtxFx33 *pSrc, MtxFx33 *pDst);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_FX_MTX33_H */
