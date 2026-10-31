// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nitro/math.h: the random helpers and bit-scan helpers with the rest.
 *
 * HGSS's lib/include/nitro/math.h pulls only math/crc.h and math/dgt.h, so
 * MATHRandContext32/MATH_Rand32 (new header nitro/math/rand.h below) and the
 * MATH_CountLeadingZeros family the port's tests use are invisible.
 * Bodies: MATH_CountLeadingZerosFunc/Inline live in hgss/compat/src/
 * sdk_standins.c (ntrtwl libraries/math/src/math.c's portable C version;
 * HGSS stripped them, keeping only the code16.h/code32.h macros that name
 * them).
 */

#ifndef HGSS_COMPAT_MATH_H
#define HGSS_COMPAT_MATH_H

#include_next <nitro/math.h>
#include <nitro/math/rand.h>

#ifdef __cplusplus
extern "C" {
#endif

u32 MATH_CountLeadingZerosFunc(u32 x);
u32 MATH_CountLeadingZerosInline(u32 x);
/* No MATH_CountPopulation declaration here on purpose. HGSS declares it
 * twice with different types: u8 in flat MATH_pop.h (matching the
 * definition in lib/NitroSDK/src/math/MATH_pop.c) and int in public
 * nitro/wm/wm_internal.h (and vct_audio_internal.h). Any declaration here
 * collides with one of them: SDK TUs such as wm_core.c see wm_internal.h,
 * while port TUs such as port_gx3d_raster.c include flat math.h too. Every
 * TU still has exactly one declaration (or an implicit one, as in
 * test_nitro.c, which only checks the value 17), values are 0..32, and the
 * u8 definition links everywhere. The proper fix is an SDK_PORT_3DS patch
 * correcting wm_internal.h to the u8 definition; then this header can
 * declare u8 as well. */

#ifndef MATH_CountLeadingZeros
#define MATH_CountLeadingZeros(x) MATH_CountLeadingZerosFunc(x)
#endif

static inline int MATH_ILog2(u32 x)
{
    return (int)(31 - MATH_CountLeadingZeros(x));
}

#ifdef __cplusplus
}
#endif

#endif /* HGSS_COMPAT_MATH_H */
