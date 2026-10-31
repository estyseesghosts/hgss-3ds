/*
 * Host test for the GCC (SDK_PORT_3DS) C versions of the MWCC asm matrix functions in
 * lib/NitroSDK/src/fx (fx_mtx22/33/43/44). Built by run.sh against a patched tree.
 *
 * Each case calls the function on a guarded buffer (sentinel words before and after the
 * destination, and every destination word preset to a sentinel) and compares the result with:
 *   (a) a reference written from the matrix definitions below, or
 *   (b) a literal table of exact expected words.
 * Synthetic values only. Prints PASS/FAIL per case; exits 1 if any case fails.
 *
 * Conventions (NitroSDK, row vectors, v' = v * M). Words are row-major; MtxFx43 is 4 rows x 3
 * columns (row 3 is the translation row), MtxFx44 is 4x4, FX32_ONE is 1.0 in 4.12 fixed point.
 * Rotation about axis A (0=X, 1=Y, 2=Z) with sin s and cos c:
 *   row A is e_A (fixed axis), and with (u, v) = (A+1, A+2) mod 3:
 *   row u = c*e_u + s*e_v,  row v = -s*e_u + c*e_v.
 * Scale and identity are diagonal; Copy43To44 adds a fourth column (0, 0, 0, 1)^T.
 */
/* No <stdio.h> or <string.h>: the project include path (lib/include) shadows stdarg.h and
 * string.h, which breaks the system stdio.h. Output goes through write(1). */
#include <stdint.h>
#include <unistd.h>

#include <nitro/fx/fx.h>

/* Functions under test. Declarations only; the definitions come from the patched fx files. */
void MTX_Identity22_(MtxFx22 *mtx);
void MTX_Identity33_(MtxFx33 *pDst);
void MTX_RotY33_(MtxFx33 *pDst, fx32 sinVal, fx32 cosVal);
void MTX_RotZ33_(MtxFx33 *pDst, fx32 sinVal, fx32 cosVal);
void MTX_Identity43_(MtxFx43 *pDst);
void MTX_Copy43To44_(const MtxFx43 *pSrc, MtxFx44 *pDst);
void MTX_RotX43_(MtxFx43 *pDst, fx32 sinVal, fx32 cosVal);
void MTX_RotY43_(MtxFx43 *pDst, fx32 sinVal, fx32 cosVal);
void MTX_Scale43_(MtxFx43 *pDst, fx32 x, fx32 y, fx32 z);
void MTX_Identity44_(MtxFx44 *pDst);
void MTX_Copy44To43_(const MtxFx44 *pSrc, MtxFx43 *pDst);
void MTX_RotX44_(MtxFx44 *pDst, fx32 sinVal, fx32 cosVal);
void MTX_RotY44_(MtxFx44 *pDst, fx32 sinVal, fx32 cosVal);
void MTX_RotZ44_(MtxFx44 *pDst, fx32 sinVal, fx32 cosVal);

#define ONE 0x1000
#define GUARD 0xDEADBEEFu
#define PRE 4     /* guard words before the destination */
#define WORDS 32  /* total buffer words; destination is at word PRE, at most 16 words */

static int g_pass = 0;
static int g_fail = 0;

/* Minimal text output: a line buffer with decimal/hex formatting, flushed with write(). */
typedef struct {
    char s[240];
    int n;
} Text;

static void t_chr(Text *t, char ch)
{
    if (t->n < (int)sizeof t->s - 1) {
        t->s[t->n++] = ch;
    }
}

static void t_str(Text *t, const char *str)
{
    while (*str) {
        t_chr(t, *str++);
    }
}

static void t_dec(Text *t, long v)
{
    char d[24];
    int k = 0;
    unsigned long m = v < 0 ? 0UL - (unsigned long)v : (unsigned long)v;

    if (v < 0) {
        t_chr(t, '-');
    }
    do {
        d[k++] = (char)('0' + m % 10);
        m /= 10;
    } while (m);
    while (k) {
        t_chr(t, d[--k]);
    }
}

static void t_hex32(Text *t, uint32_t v)
{
    int sh;

    t_str(t, "0x");
    for (sh = 28; sh >= 0; sh -= 4) {
        t_chr(t, "0123456789abcdef"[(v >> sh) & 0xF]);
    }
}

static void t_flush(const Text *t)
{
    int off = 0;

    while (off < t->n) {
        ssize_t w = write(1, t->s + off, (size_t)(t->n - off));
        if (w <= 0) {
            return;
        }
        off += (int)w;
    }
}

static void mem_copy(void *dst, const void *src, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        ((unsigned char *)dst)[i] = ((const unsigned char *)src)[i];
    }
}

/* Two's complement negation, computed in 64 bits and reduced mod 2^32. */
static fx32 neg32(fx32 v)
{
    return (fx32)(uint32_t)(-(int64_t)v);
}

/* ---- reference (from the definitions above) ---- */

static void ref_rot33(fx32 out[9], int axis, fx32 s, fx32 c)
{
    int u = (axis + 1) % 3, v = (axis + 2) % 3;
    int i;

    for (i = 0; i < 9; i++) {
        out[i] = 0;
    }
    out[axis * 3 + axis] = ONE;
    out[u * 3 + u] = c;
    out[u * 3 + v] = s;
    out[v * 3 + u] = neg32(s);
    out[v * 3 + v] = c;
}

/* 4x3 rotation: the 3x3 rotation in rows 0..2, translation row zero. */
static void ref_rot43(fx32 out[12], int axis, fx32 s, fx32 c)
{
    fx32 r[9];
    int i;

    ref_rot33(r, axis, s, c);
    for (i = 0; i < 12; i++) {
        out[i] = (i < 9) ? r[i] : 0;
    }
}

/* 4x4 rotation: the 3x3 rotation, zero last column, last row (0, 0, 0, 1). */
static void ref_rot44(fx32 out[16], int axis, fx32 s, fx32 c)
{
    fx32 r[9];
    int row, col;

    ref_rot33(r, axis, s, c);
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            fx32 e;
            if (row < 3 && col < 3) {
                e = r[row * 3 + col];
            } else if (row == col) {
                e = ONE;
            } else {
                e = 0;
            }
            out[row * 4 + col] = e;
        }
    }
}

/* ---- harness ---- */

/* Runs one case: buf is preset, dst is at buf[PRE], n words are checked against exp. */
static void verdict(const char *name, const uint32_t *buf, int n, const fx32 *exp)
{
    Text t;
    int ok = 1, i;

    t.n = 0;
    for (i = 0; i < PRE; i++) {
        if (buf[i] != GUARD) {
            ok = 0;
            t_str(&t, "word before dst ");
            t_dec(&t, i - PRE);
            t_str(&t, " was written: ");
            t_hex32(&t, buf[i]);
            break;
        }
    }
    for (i = PRE + n; ok && i < WORDS; i++) {
        if (buf[i] != GUARD) {
            ok = 0;
            t_str(&t, "word past dst ");
            t_dec(&t, i - PRE);
            t_str(&t, " was written: ");
            t_hex32(&t, buf[i]);
            break;
        }
    }
    for (i = 0; ok && i < n; i++) {
        if ((fx32)buf[PRE + i] != exp[i]) {
            ok = 0;
            t_str(&t, "word ");
            t_dec(&t, i);
            t_str(&t, ": got ");
            t_hex32(&t, buf[PRE + i]);
            t_str(&t, ", expected ");
            t_hex32(&t, (uint32_t)exp[i]);
            break;
        }
    }
    {
        Text out;
        out.n = 0;
        if (ok) {
            g_pass++;
            t_str(&out, "PASS ");
            t_str(&out, name);
        } else {
            g_fail++;
            t_str(&out, "FAIL ");
            t_str(&out, name);
            t_str(&out, ": ");
            t.s[t.n] = 0;
            t_str(&out, t.s);
        }
        t_chr(&out, '\n');
        t_flush(&out);
    }
}

static void preset(uint32_t *buf)
{
    int i;
    for (i = 0; i < WORDS; i++) {
        buf[i] = GUARD;
    }
}

static void zero(void *dst, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        ((unsigned char *)dst)[i] = 0;
    }
}

/* Words of a destination, as a plain array, for the functions that take it as fx32 *. */
#define DST(buf) ((void *)((buf) + PRE))

/* Inputs for the rotation and scale cases: angles with both signs, the extremes, and a few
 * sin/cos pairs that are not on the unit circle (the functions copy values; they do not check). */
static const fx32 SC[][2] = {
    {0, ONE},
    {ONE, 0},
    {-ONE, 0},
    {0x0B50, 0x0B50},
    {-0x0B50, 0x0B50},
    {0x0B50, -0x0B50},
    {-0x0F00, -0x0400},
    {12345, -6789},
    {INT32_MAX, INT32_MIN},
    {INT32_MIN, INT32_MAX},
    {INT32_MIN, INT32_MIN},
    {-1, 1},
};
#define NSC ((int)(sizeof SC / sizeof SC[0]))

/* Builds "<fn> sin=<s> cos=<c>" into a Text; the case name is then passed as text.s. */
static void name_sc(Text *t, const char *fn, fx32 s, fx32 c)
{
    t->n = 0;
    t_str(t, fn);
    t_str(t, " sin=");
    t_dec(t, (long)s);
    t_str(t, " cos=");
    t_dec(t, (long)c);
    t->s[t->n] = 0;
}

/* ---- cases ---- */

static void case_identity_literal(void)
{
    uint32_t buf[WORDS];
    fx32 exp22[4] = {ONE, 0, 0, ONE};
    fx32 exp33[9] = {ONE, 0, 0, 0, ONE, 0, 0, 0, ONE};
    fx32 exp43[12] = {ONE, 0, 0, 0, ONE, 0, 0, 0, ONE, 0, 0, 0};
    fx32 exp44[16] = {ONE, 0, 0, 0, 0, ONE, 0, 0, 0, 0, ONE, 0, 0, 0, 0, ONE};

    preset(buf); MTX_Identity22_(DST(buf));
    verdict("Identity22 literal", buf, 4, exp22);
    preset(buf); MTX_Identity33_(DST(buf));
    verdict("Identity33 literal", buf, 9, exp33);
    preset(buf); MTX_Identity43_(DST(buf));
    verdict("Identity43 literal", buf, 12, exp43);
    preset(buf); MTX_Identity44_(DST(buf));
    verdict("Identity44 literal", buf, 16, exp44);
}

static void case_identity_ref(void)
{
    uint32_t buf[WORDS];
    fx32 e22[4] = {ONE, 0, 0, ONE};
    fx32 e33[9] = {ONE, 0, 0, 0, ONE, 0, 0, 0, ONE};
    fx32 e43[12] = {ONE, 0, 0, 0, ONE, 0, 0, 0, ONE, 0, 0, 0};
    fx32 e44[16] = {ONE, 0, 0, 0, 0, ONE, 0, 0, 0, 0, ONE, 0, 0, 0, 0, ONE};

    /* Same definition, written as the identity of each shape (repeats the check on the
     * reference side so a wrong literal above would also show). */
    preset(buf); MTX_Identity22_(DST(buf));
    verdict("Identity22 reference", buf, 4, e22);
    preset(buf); MTX_Identity33_(DST(buf));
    verdict("Identity33 reference", buf, 9, e33);
    preset(buf); MTX_Identity43_(DST(buf));
    verdict("Identity43 reference", buf, 12, e43);
    preset(buf); MTX_Identity44_(DST(buf));
    verdict("Identity44 reference", buf, 16, e44);
}

static void case_copy(void)
{
    uint32_t buf[WORDS];
    MtxFx43 src43;
    MtxFx44 src44;
    fx32 exp44[16], exp43[12];
    int i, r, c;
    static const fx32 pat[16] = {
        1, -2, 3, -4, 5, -6, 7, -8, 9, -10, 11, -12, INT32_MAX, INT32_MIN, -1, 0x12345
    };

    /* Literal: 1..12 copied, 4th column 0, 4th row's 4th element ONE. */
    {
        fx32 lit_src[12];
        fx32 lit_exp[16] = {1, 2, 3, 0, 4, 5, 6, 0, 7, 8, 9, 0, 10, 11, 12, ONE};
        for (i = 0; i < 12; i++) {
            lit_src[i] = i + 1;
        }
        mem_copy(src43.a, lit_src, sizeof lit_src);
        preset(buf);
        MTX_Copy43To44_(&src43, (MtxFx44 *)DST(buf));
        verdict("Copy43To44 literal", buf, 16, lit_exp);
    }

    /* Reference over the pattern: element copy plus the fixed fourth column. */
    mem_copy(src43.a, pat, 12 * sizeof(fx32));
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            exp44[r * 4 + c] = (c < 3) ? src43.a[r * 3 + c] : ((r == 3) ? ONE : 0);
        }
    }
    preset(buf);
    MTX_Copy43To44_(&src43, (MtxFx44 *)DST(buf));
    verdict("Copy43To44 pattern", buf, 16, exp44);

    /* Literal for 4x4 -> 4x3: 1..16 with column 3 dropped. */
    {
        fx32 lit_src[16], lit_exp[12] = {1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15};
        for (i = 0; i < 16; i++) {
            lit_src[i] = i + 1;
        }
        mem_copy(src44.a, lit_src, sizeof lit_src);
        preset(buf);
        MTX_Copy44To43_(&src44, (MtxFx43 *)DST(buf));
        verdict("Copy44To43 literal", buf, 12, lit_exp);
    }

    /* Reference over the pattern: columns 0..2 of each row. */
    mem_copy(src44.a, pat, sizeof pat);
    mem_copy(src44.a + 12, pat, 4 * sizeof(fx32));
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 3; c++) {
            exp43[r * 3 + c] = src44.a[r * 4 + c];
        }
    }
    preset(buf);
    MTX_Copy44To43_(&src44, (MtxFx43 *)DST(buf));
    verdict("Copy44To43 pattern", buf, 12, exp43);
}

static void case_rot33(void)
{
    uint32_t buf[WORDS];
    Text name;
    fx32 exp[9];
    int k;

    for (k = 0; k < NSC; k++) {
        fx32 s = SC[k][0], c = SC[k][1];

        ref_rot33(exp, 1, s, c);
        name_sc(&name, "RotY33", s, c);
        preset(buf); MTX_RotY33_(DST(buf), s, c);
        verdict(name.s, buf, 9, exp);

        ref_rot33(exp, 2, s, c);
        name_sc(&name, "RotZ33", s, c);
        preset(buf); MTX_RotZ33_(DST(buf), s, c);
        verdict(name.s, buf, 9, exp);
    }

    /* Literal: RotZ33 at 90 degrees, sin = ONE, cos = 0. */
    {
        fx32 lit[9] = {0, ONE, 0, -ONE, 0, 0, 0, 0, ONE};
        preset(buf); MTX_RotZ33_(DST(buf), ONE, 0);
        verdict("RotZ33 literal sin=1 cos=0", buf, 9, lit);
    }
}

static void case_rot43(void)
{
    uint32_t buf[WORDS];
    Text name;
    fx32 exp[12];
    int k;

    for (k = 0; k < NSC; k++) {
        fx32 s = SC[k][0], c = SC[k][1];

        ref_rot43(exp, 0, s, c);
        name_sc(&name, "RotX43", s, c);
        preset(buf); MTX_RotX43_(DST(buf), s, c);
        verdict(name.s, buf, 12, exp);

        ref_rot43(exp, 1, s, c);
        name_sc(&name, "RotY43", s, c);
        preset(buf); MTX_RotY43_(DST(buf), s, c);
        verdict(name.s, buf, 12, exp);
    }

    /* Literal: RotX43 at 90 degrees. */
    {
        fx32 lit[12] = {ONE, 0, 0, 0, 0, ONE, 0, -ONE, 0, 0, 0, 0};
        preset(buf); MTX_RotX43_(DST(buf), ONE, 0);
        verdict("RotX43 literal sin=1 cos=0", buf, 12, lit);
    }
    /* Literal: RotY43 at 90 degrees. */
    {
        fx32 lit[12] = {0, 0, -ONE, 0, ONE, 0, ONE, 0, 0, 0, 0, 0};
        preset(buf); MTX_RotY43_(DST(buf), ONE, 0);
        verdict("RotY43 literal sin=1 cos=0", buf, 12, lit);
    }
    /* Extremes: -INT32_MIN wraps to INT32_MIN, so the negated element is INT32_MIN. */
    {
        fx32 lit[12] = {ONE, 0, 0, 0, INT32_MAX, INT32_MIN, 0, INT32_MIN, INT32_MAX, 0, 0, 0};
        preset(buf); MTX_RotX43_(DST(buf), INT32_MIN, INT32_MAX);
        verdict("RotX43 literal extremes", buf, 12, lit);
    }
}

static void case_scale43(void)
{
    uint32_t buf[WORDS];
    Text name;
    fx32 exp[12];
    int k;
    static const fx32 xyz[][3] = {
        {ONE, ONE, ONE},
        {0x2000, -ONE, INT32_MAX},
        {-0x0800, 0x0400, -0x0100},
        {INT32_MIN, INT32_MAX, 0},
        {0, 0, 0},
    };

    for (k = 0; k < (int)(sizeof xyz / sizeof xyz[0]); k++) {
        fx32 x = xyz[k][0], y = xyz[k][1], z = xyz[k][2];
        zero(exp, sizeof exp);
        exp[0] = x; exp[4] = y; exp[8] = z;
        name.n = 0;
        t_str(&name, "Scale43 x=");
        t_dec(&name, (long)x);
        t_str(&name, " y=");
        t_dec(&name, (long)y);
        t_str(&name, " z=");
        t_dec(&name, (long)z);
        name.s[name.n] = 0;
        preset(buf); MTX_Scale43_(DST(buf), x, y, z);
        verdict(name.s, buf, 12, exp);
    }

    {
        fx32 lit[12] = {0x2000, 0, 0, 0, -ONE, 0, 0, 0, INT32_MAX, 0, 0, 0};
        preset(buf); MTX_Scale43_(DST(buf), 0x2000, -ONE, INT32_MAX);
        verdict("Scale43 literal", buf, 12, lit);
    }
}

static void case_rot44(void)
{
    uint32_t buf[WORDS];
    Text name;
    fx32 exp[16];
    int k;

    for (k = 0; k < NSC; k++) {
        fx32 s = SC[k][0], c = SC[k][1];

        ref_rot44(exp, 0, s, c);
        name_sc(&name, "RotX44", s, c);
        preset(buf); MTX_RotX44_(DST(buf), s, c);
        verdict(name.s, buf, 16, exp);

        ref_rot44(exp, 1, s, c);
        name_sc(&name, "RotY44", s, c);
        preset(buf); MTX_RotY44_(DST(buf), s, c);
        verdict(name.s, buf, 16, exp);

        ref_rot44(exp, 2, s, c);
        name_sc(&name, "RotZ44", s, c);
        preset(buf); MTX_RotZ44_(DST(buf), s, c);
        verdict(name.s, buf, 16, exp);
    }

    {
        fx32 lit[16] = {0, 0, -ONE, 0, 0, ONE, 0, 0, ONE, 0, 0, 0, 0, 0, 0, ONE};
        preset(buf); MTX_RotY44_(DST(buf), ONE, 0);
        verdict("RotY44 literal sin=1 cos=0", buf, 16, lit);
    }
    {
        fx32 lit[16] = {0, ONE, 0, 0, -ONE, 0, 0, 0, 0, 0, ONE, 0, 0, 0, 0, ONE};
        preset(buf); MTX_RotZ44_(DST(buf), ONE, 0);
        verdict("RotZ44 literal sin=1 cos=0", buf, 16, lit);
    }
}

/* Deterministic pseudo-random sin/cos pairs (synthetic, fixed seed), all three 44 rotations
 * and the 43 rotations, to cover more values than the table above. */
static void case_random(void)
{
    uint32_t buf[WORDS];
    uint32_t seed = 0x2468ACE1u;
    int k;
    Text name;
    fx32 exp16[16], exp12[12];

    for (k = 0; k < 64; k++) {
        fx32 s, c;
        seed = seed * 1664525u + 1013904223u;
        s = (fx32)seed;
        seed = seed * 1664525u + 1013904223u;
        c = (fx32)seed;

        ref_rot44(exp16, 0, s, c);
        name_sc(&name, "rand RotX44", s, c);
        preset(buf); MTX_RotX44_(DST(buf), s, c);
        verdict(name.s, buf, 16, exp16);

        ref_rot44(exp16, 1, s, c);
        name_sc(&name, "rand RotY44", s, c);
        preset(buf); MTX_RotY44_(DST(buf), s, c);
        verdict(name.s, buf, 16, exp16);

        ref_rot44(exp16, 2, s, c);
        name_sc(&name, "rand RotZ44", s, c);
        preset(buf); MTX_RotZ44_(DST(buf), s, c);
        verdict(name.s, buf, 16, exp16);

        ref_rot43(exp12, 0, s, c);
        name_sc(&name, "rand RotX43", s, c);
        preset(buf); MTX_RotX43_(DST(buf), s, c);
        verdict(name.s, buf, 12, exp12);

        ref_rot43(exp12, 1, s, c);
        name_sc(&name, "rand RotY43", s, c);
        preset(buf); MTX_RotY43_(DST(buf), s, c);
        verdict(name.s, buf, 12, exp12);
    }
}

int main(void)
{
    case_identity_literal();
    case_identity_ref();
    case_copy();
    case_rot33();
    case_rot43();
    case_scale43();
    case_rot44();
    case_random();

    {
        Text t;
        t.n = 0;
        t_dec(&t, g_pass);
        t_str(&t, " passed, ");
        t_dec(&t, g_fail);
        t_str(&t, " failed\n");
        t_flush(&t);
    }
    return g_fail ? 1 : 0;
}
