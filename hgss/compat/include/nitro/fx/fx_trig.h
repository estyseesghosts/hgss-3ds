// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/fx/fx_trig.h: FX_Atan2Idx, wrapping HGSS's header.
 *
 * HGSS's lib/include/nitro/fx/fx_trig.h declares the sine/cosine table and
 * its index helpers but not FX_Atan2Idx, although HGSS's fx_atanidx.c
 * defines it (u16 FX_Atan2Idx(fx32 y, fx32 x), as in ntrtwl's
 * include/nitro/fx/fx_trig.h).
 */

#ifndef HGSS_COMPAT_FX_FX_TRIG_H
#define HGSS_COMPAT_FX_FX_TRIG_H

#include_next <nitro/fx/fx_trig.h>

#ifdef __cplusplus
extern "C" {
#endif

u16 FX_Atan2Idx(fx32 y, fx32 x);

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_FX_FX_TRIG_H */
