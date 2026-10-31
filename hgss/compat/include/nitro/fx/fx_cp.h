// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/fx/fx_cp.h: the square-root and 32-bit division entry points,
 * wrapping HGSS's header.
 *
 * HGSS's fx_cp.h declares FX_Div/FX_ModS32/FX_Inv but not FX_Sqrt/
 * FX_DivS32, although HGSS's fx.c/fx_cp.c define them (ntrtwl declares all
 * five in include/nitro/fx/fx_cp.h). The declarations below match those
 * definitions.
 */

#ifndef HGSS_COMPAT_FX_FX_CP_H
#define HGSS_COMPAT_FX_FX_CP_H

#include_next <nitro/fx/fx_cp.h>

#ifdef __cplusplus
extern "C" {
#endif

fx32 FX_Sqrt(fx32 x);
s32 FX_DivS32(s32 a, s32 b);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_FX_FX_CP_H */
