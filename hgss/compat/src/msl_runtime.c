/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Metrowerks C library (MSL) helpers that the DS build gets from
 * lib/MSL_C/src/msl_fp_runtime.c and msl_string_extras.c.
 *
 * Those two files cannot replace themselves under GCC:
 *
 * - msl_fp_runtime.c is one 11 KB block of hand-written ARM assembly (MWCC
 *   `asm void` bodies with fall-through between routines and literal pools in
 *   .text). GCC cannot assemble it for a host, and lib/MSL_C is deliberately
 *   left out of the link (the host libc owns errno/ctype/printf).
 * - newlib/glibc have no `strnicmp` (the 2-argument MSL form) and no
 *   `__lower_mapC` table.
 *
 * This file supplies only the entry points the game actually references, with
 * the ABI the game declares. The important part is the calling convention, not
 * the name: MWCC's soft-float helpers exchange IEEE-754 bit patterns, so
 * `_fadd`, `_fdiv`, `_fflt`, `_ffix`, `_f_lltof` and `_f_ulltof` take and
 * return 32-bit integers on the DS. On x86-32 (and on ARM hard-float) a
 * `float` return travels in a different register/stack slot than an integer,
 * so a plain C `float _fadd(float, float)` would silently misread every call.
 * Callers that declared the typed forms are reconciled by patch
 * 1760-reconcile-the-MSL-soft-float-declarations.patch, which uses the helpers
 * in hgss/compat/include/hgss_port_msl.h to go between the two spellings where
 * the source needs a float value.
 *
 * Conversions below go through memcpy, never a punning cast: the compiler is
 * allowed to assume a float object holds a valid float, and type-punning
 * through a pointer is undefined behaviour (and can miscompile at -O2).
 *
 * This object is linked into the program only (tools/hgss_game.py
 * app_sources), not into libnitro.a: these are game-runtime helpers, not SDK
 * code.
 */

#include <stdint.h>
#include <string.h>

typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef uint8_t u8;

/* --------------------------------------------------------------- bit <-> float */

static float bits_to_f32(u32 bits)
{
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static u32 f32_to_bits(float value)
{
    u32 bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

/*
 * IEEE-754 single addition. The DS helper performs the same operation on its
 * two bit-pattern arguments; plain C float addition has identical results for
 * every finite input the game passes (the wifi-plaza effect only adds 0.5 to
 * small integer-converted floats).
 */
u32 _fadd(u32 a, u32 b)
{
    return f32_to_bits(bits_to_f32(a) + bits_to_f32(b));
}

/*
 * IEEE-754 single division. Division by zero follows IEEE-754 here (infinity
 * or NaN), as the ARM helper does; nothing in the game divides by zero.
 */
u32 _fdiv(u32 a, u32 b)
{
    return f32_to_bits(bits_to_f32(a) / bits_to_f32(b));
}

/*
 * Signed 32-bit integer to IEEE-754 single. MWCC's _fflt is signed (the
 * unsigned sibling is _ffltu); callers pass a signed int.
 */
u32 _fflt(s32 value)
{
    return f32_to_bits((float)value);
}

extern u32 _f_lltof(s64 value);
u32 _f_lltof(s64 value)
{
    return f32_to_bits((float)value);
}

extern u32 _f_ulltof(u64 value);
u32 _f_ulltof(u64 value)
{
    return f32_to_bits((float)value);
}

/*
 * Float to signed 32-bit, truncating toward zero (ARM VCVT). The ARM
 * conversion also saturates on overflow and returns 0 for NaN; C's plain cast
 * is undefined there, so the bounds are explicit. The game's inputs are
 * ordinary pixel/fx32 values well inside the range.
 */
s32 _ffix(u32 value)
{
    float number = bits_to_f32(value);

    if (number != number) {
        return 0;
    }
    if (number >= 2147483648.0f) {
        return (s32)0x7fffffff;
    }
    if (number <= -2147483648.0f) {
        return (s32)0x80000000;
    }
    return (s32)number;
}

/* ------------------------------------------------------------ 64-bit integer */

/*
 * 64x64 -> 64 multiply, wrapping. The unsigned product is computed so the
 * result does not depend on -fwrapv for signed overflow (the port does build
 * with it, but the helper should be self-contained).
 */
s64 _ll_mul(s64 a, s64 b)
{
    u64 ua, ub, product;
    s64 result;

    memcpy(&ua, &a, sizeof ua);
    memcpy(&ub, &b, sizeof ub);
    product = ua * ub;
    memcpy(&result, &product, sizeof result);
    return result;
}

/*
 * Unsigned 64/64 divide. The DS helper assumes a non-zero divisor; the game
 * never passes zero. Return a documented 0 instead of trapping, so a bad
 * divisor cannot take the port down before the game's checks run.
 */
u64 _ll_udiv(u64 a, u64 b)
{
    if (b == 0) {
        return 0;
    }
    return a / b;
}

/*
 * Signed 32/32 divide, truncating toward zero (C99 division). The DS helper
 * expects a non-zero divisor; return the documented 0 for zero (see
 * _ll_udiv). INT_MIN / -1 is the one C overflow case: wrap to INT_MIN, which
 * is what the ARM divide instruction would put in the low word.
 */
s32 _s32_div_f(s32 dividend, s32 divisor)
{
    if (divisor == 0) {
        return 0;
    }
    if (dividend == (s32)0x80000000 && divisor == -1) {
        return (s32)0x80000000;
    }
    return dividend / divisor;
}

/* ------------------------------------------------------------------- strings */

/*
 * ASCII lower-case table. MSL's copy maps A-Z to a-z and leaves every other
 * byte (including 0x80-0xFF) alone; callers index it only with 0..127 (the
 * chat channel code guards `c < 0 || c >= 128`). The other declarations in the
 * tree spell it `u8[]`, so the definition is deliberately non-const: the table
 * is read-only in practice, and a const definition would not match those
 * declarations' type.
 */
u8 __lower_mapC[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
    0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
    0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
    0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
    0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};

static int msl_lower(int c)
{
    return (c < 0 || c >= 128) ? c : __lower_mapC[c];
}

/*
 * MSL's strnicmp is not the C library's strncasecmp: it has no length
 * argument. lib/MSL_C/src/msl_string_extras.c defines it as a full
 * case-insensitive compare that returns -1, 0 or 1 (stricmp), stopping at the
 * terminating NUL, and every caller in src/chat and the lobby uses it that
 * way. Keep that exact contract.
 */
int strnicmp(const char *a, const char *b)
{
    u8 x, y;

    do {
        x = (u8)msl_lower((u8)*a++);
        y = (u8)msl_lower((u8)*b++);
        if (x < y) {
            return -1;
        }
        if (x > y) {
            return 1;
        }
    } while (x != 0);
    return 0;
}
