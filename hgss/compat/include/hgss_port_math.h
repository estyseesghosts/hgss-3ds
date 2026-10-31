/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * C99 math surface for the vendored 3ds_port sources.
 *
 * Several vendored files (port_gx3d.c, port_gx3d_gpusim.c, port_gx3d_prune.c,
 * port_gx3d_raster.c, port_gx3d_vertex.c, port_shadow.c, port_xbrz.c,
 * port_wideart.c, ...) call the C99 math functions and predicates without
 * including <math.h>. An implicit declaration is not merely a warning: it
 * makes GCC assume an int return, so a double result comes back through the
 * wrong register class and the value is garbage. isfinite is worse still: it
 * is a macro over a compiler builtin, and an undeclared call leaves an
 * undefined `isfinite` symbol at link time.
 *
 * The system <math.h> cannot simply be force-included: HGSS's flat
 * lib/NitroSDK/include/math.h is earlier on the -I path and shadows it, and it
 * does not declare this surface. So declare exactly what the port uses, with
 * the standard prototypes, and define the classification macros over the
 * compiler builtins (no libm symbol, no header dependency). Including the real
 * <math.h> as well is harmless: these are the same prototypes.
 *
 * tools/hgss_nitro.py force-includes this file by its absolute repo path for
 * every port compile, so it is also picked up by the TUs that never include
 * anything themselves.
 */

#ifndef HGSS_PORT_MATH_H
#define HGSS_PORT_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

extern double fabs(double);
extern float fabsf(float);
extern double floor(double);
extern float floorf(float);
extern double ceil(double);
extern float ceilf(float);
extern double fmin(double, double);
extern float fminf(float, float);
extern double fmax(double, double);
extern float fmaxf(float, float);
extern double sqrt(double);
extern float sqrtf(float);
extern double sin(double);
extern float sinf(float);
extern double cos(double);
extern float cosf(float);
extern double tan(double);
extern float tanf(float);
extern double exp2(double);
extern float exp2f(float);
extern double log2(double);
extern float log2f(float);
extern double pow(double, double);
extern float powf(float, float);

#ifndef isfinite
#define isfinite(x) __builtin_isfinite(x)
#endif
#ifndef isnan
#define isnan(x) __builtin_isnan(x)
#endif
#ifndef isinf
#define isinf(x) __builtin_isinf_sign(x)
#endif
#ifndef signbit
#define signbit(x) __builtin_signbit(x)
#endif

#ifdef __cplusplus
}
#endif

#endif /* HGSS_PORT_MATH_H */
