// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/fx/fx_mtx22.h: the 2x2 helpers, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/fx/fx_mtx22.h only declares MTX_Rot22_ (fx16
 * sine/cosine) and MTX_ScaleApply22. The ntrtwl names below
 * (include/nitro/fx/fx_mtx22.h) have no HGSS source at all; their bodies
 * live in hgss/compat/src/sdk_standins.c and store exactly what ntrtwl's
 * assembly stored (identity diagonal, scale diagonal, Z-rotation layout,
 * alias-safe transpose).
 */

#ifndef HGSS_COMPAT_FX_MTX22_H
#define HGSS_COMPAT_FX_MTX22_H

#include_next <nitro/fx/fx_mtx22.h>

#ifdef __cplusplus
extern "C" {
#endif

void MTX_Identity22(MtxFx22 *pDst);
void MTX_Scale22(MtxFx22 *pDst, fx32 x, fx32 y);
void MTX_Rot22(MtxFx22 *pDst, fx32 sinVal, fx32 cosVal);
void MTX_Transpose22(const MtxFx22 *pSrc, MtxFx22 *pDst);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_FX_MTX22_H */
