/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The MSL soft-float calling convention, for game sources that have to name it.
 *
 * On the DS, Metrowerks' runtime helpers (`_fadd`, `_fdiv`, `_fflt`, `_ffix`,
 * `_f_lltof`, `_f_ulltof`) exchange IEEE-754 single values as 32-bit bit
 * patterns: the compiler and the helper agree that a float is an integer word.
 * The port's hgss/compat/src/msl_runtime.c keeps that ABI (see its header for
 * why a typed C implementation cannot work on x86-32 or ARM hard-float).
 *
 * Three decompilation TUs declared the convenient typed forms instead
 * (`f32 _fflt(int)`, `float _fdiv(float, float)`, `f32 _f_lltof(s64)`). Passing
 * or returning a float through those prototypes puts the value in a different
 * register/stack slot on the port, so the call reads garbage. Patch
 * 1760-reconcile-the-MSL-soft-float-declarations.patch gives those TUs the
 * bit-pattern prototypes under SDK_PORT_3DS and uses these helpers where the
 * source genuinely needs a float expression.
 *
 * The helpers are `static inline` and use memcpy, so including this header
 * cannot introduce a second float ABI or a type-punning alias.
 */

#ifndef HGSS_PORT_MSL_H
#define HGSS_PORT_MSL_H

#include <stdint.h>
#include <string.h>

/* The IEEE-754 single bit pattern as the MSL helpers pass it. */
static inline float HGSS_PortBitsToF32(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static inline uint32_t HGSS_PortF32ToBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

#endif /* HGSS_PORT_MSL_H */
