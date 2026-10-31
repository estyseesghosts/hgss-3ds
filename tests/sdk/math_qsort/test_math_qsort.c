/* Host test for the SDK_PORT_3DS C version of MATH_QSort (lib/NitroSDK/src/math/math_qsort.c).
 *
 * The golden values come from a scratch interpreter that runs the UNCHANGED MWCC asm of
 * MATH_QSort (ARM subset; `blx r11` calls the comparator). EXPLICIT rows list every comparator
 * call {element index a, element index b, return value} and the final order. HASHED rows give
 * the call count, an FNV-1a digest of the call log, and an FNV-1a digest of the final bytes.
 * All data is synthetic.
 *
 * Element format: the key is the first min(width,4) bytes, little-endian. For width >= 8 bytes
 * 4..7 hold the original index (a tag) and bytes from 8 on hold a filler pattern. For widths
 * 5..7 bytes from 4 on are filler. The comparator returns 5 (greater), -2 (less) or 0 (equal),
 * and logs each call. Every case runs with a NULL work buffer and with a work buffer.
 *
 * Usage: tests/sdk/math_qsort/run.sh [ROOT]   (patched NitroSDK tree; default build/upstream)
 */
#include <stdio.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned int u32;
typedef int s32;

void MATH_QSort(void *base, u32 count, u32 width, int (*compare)(const void *, const void *), void *work);

#define LOG_CAP 32
#define MAX_N 1000
#define FNV_INIT 2166136261u

static u32 gWidth, gBase, gKeyBytes;
static u32 gCalls, gHash;
static s32 gLog[LOG_CAP][3];

static u32 Fnv(u32 h, u32 v)
{
    int k;
    for (k = 0; k < 4; k++) {
        h ^= (v >> (8 * k)) & 0xFFu;
        h *= 16777619u;
    }
    return h;
}

static u32 FnvBytes(const u8 *p, u32 n)
{
    u32 h = FNV_INIT, i;
    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static u32 KeyOf(const u8 *p, u32 kbytes)
{
    u32 k = 0, i;
    for (i = 0; i < kbytes; i++) {
        k |= (u32)p[i] << (8 * i);
    }
    return k;
}

static int Compare(const void *a, const void *b)
{
    u32 ka = KeyOf((const u8 *)a, gKeyBytes);
    u32 kb = KeyOf((const u8 *)b, gKeyBytes);
    s32 ia = (s32)((u32)(size_t)a - gBase) / (s32)gWidth;
    s32 ib = (s32)((u32)(size_t)b - gBase) / (s32)gWidth;
    s32 ret = ka > kb ? 5 : (ka < kb ? -2 : 0);

    gHash = Fnv(Fnv(Fnv(gHash, (u32)ia), (u32)ib), (u32)ret);
    if (gCalls < LOG_CAP) {
        gLog[gCalls][0] = ia;
        gLog[gCalls][1] = ib;
        gLog[gCalls][2] = ret;
    }
    gCalls++;
    return ret;
}

/* kind: 0 random below mod, 1 ascending, 2 descending, 3 all equal, 4 three values. */
static void MakeKeys(u32 kind, u32 mod, u32 seed, u32 n, u32 *keys)
{
    u32 s = seed, i;
    for (i = 0; i < n; i++) {
        if (kind == 0) {
            s = s * 1664525u + 1013904223u;
            keys[i] = (s >> 8) % mod;
        } else if (kind == 1) {
            keys[i] = i;
        } else if (kind == 2) {
            keys[i] = n - 1 - i;
        } else if (kind == 3) {
            keys[i] = 7;
        } else {
            s = s * 1664525u + 1013904223u;
            keys[i] = (s >> 8) % 3;
        }
    }
}

static void MakeElem(u8 *e, u32 idx, u32 key, u32 width)
{
    u32 kw = width < 4 ? width : 4, j, start = 4;
    for (j = 0; j < kw; j++) {
        e[j] = (u8)(key >> (8 * j));
    }
    if (width >= 8) {
        for (j = 0; j < 4; j++) {
            e[4 + j] = (u8)(idx >> (8 * j));
        }
        start = 8;
    }
    for (j = start; j < width; j++) {
        e[j] = (u8)(idx * 31u + j * 7u + 0x5Au);
    }
}

static void MakeElems(u8 *arena, const u32 *keys, u32 n, u32 width)
{
    u32 i;
    for (i = 0; i < n; i++) {
        MakeElem(arena + i * width, i, keys[i], width);
    }
}

/* Sorts arena in place and reports the call count, log digest and final digest. */
static void RunSort(u8 *arena, u32 n, u32 width, int useWork, u32 *calls, u32 *logHash, u32 *finalHash)
{
    static u32 workBuf[64];
    memset(workBuf, 0xCC, sizeof workBuf);
    gWidth = width;
    gBase = (u32)(size_t)arena;
    gKeyBytes = width < 4 ? width : 4;
    gCalls = 0;
    gHash = FNV_INIT;
    MATH_QSort(arena, n, width, Compare, useWork ? (void *)workBuf : NULL);
    *calls = gCalls;
    *logHash = gHash;
    *finalHash = FnvBytes(arena, n * width);
}

static int fails, checks;
#define CHECK(c, ...)                                                    \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            fails++;                                                     \
            printf("FAIL line %d: ", __LINE__);                          \
            printf(__VA_ARGS__);                                         \
            printf("\n");                                                \
        }                                                                \
    } while (0)

/* Value that identifies an element after the sort: its tag (width >= 8) or its key. */
static u32 FinalValue(const u8 *arena, u32 i, u32 width)
{
    const u8 *e = arena + i * width;
    return width >= 8 ? KeyOf(e + 4, 4) : KeyOf(e, width < 4 ? width : 4);
}

struct Explicit {
    u32 width, n;
    u32 keys[12];
    u32 calls;
    s32 log[LOG_CAP][3];
    u32 final[12];
};

struct Hashed {
    u32 width, n, kind, mod, seed, calls, logHash, finalHash;
};

static const struct Explicit kExplicit[] = {
    {8, 7, {5, 3, 5, 1, 3, 3, 9}, 18, {{1, 0, 5}, {6, 0, 5}, {5, 0, 5}, {4, 0, 5}, {3, 0, 5}, {2, 0, 5}, {1, 0, 5}, {0, 0, 0}, {2, 1, 0}, {6, 1, 5}, {5, 1, -2}, {3, 1, -2}, {4, 1, -2}, {5, 1, 0}, {4, 1, -2}, {5, 6, -2}, {2, 1, 0}, {3, 1, 0}}, {3, 1, 5, 4, 0, 2, 6}},
    {4, 5, {5, 3, 5, 1, 3}, 8, {{1, 0, -2}, {2, 0, 0}, {4, 0, -2}, {3, 0, -2}, {3, 0, -2}, {1, 0, -2}, {2, 0, 0}, {0, 1, 5}}, {1, 3, 3, 5, 5}},
    {1, 6, {6, 1, 4, 4, 2, 6}, 12, {{1, 0, -2}, {2, 0, 5}, {5, 0, 5}, {4, 0, -2}, {3, 0, 0}, {3, 0, 0}, {4, 5, 0}, {1, 0, 5}, {2, 0, 5}, {1, 0, 5}, {0, 0, 0}, {1, 2, 5}}, {1, 2, 4, 4, 6, 6}},
    {12, 4, {7, 7, 7, 7}, 5, {{1, 0, 0}, {3, 0, 0}, {2, 0, 0}, {2, 0, 0}, {0, 1, 0}}, {0, 3, 2, 1}},
    {16, 9, {9, 2, 9, 2, 5, 9, 1, 5, 0}, 24, {{1, 0, -2}, {2, 0, 5}, {8, 0, -2}, {3, 0, -2}, {4, 0, 5}, {7, 0, 0}, {5, 0, 5}, {6, 0, -2}, {7, 6, 0}, {8, 6, 0}, {1, 0, 5}, {4, 0, 5}, {3, 0, 5}, {2, 0, 5}, {1, 0, 5}, {0, 0, 0}, {2, 1, 5}, {4, 1, 5}, {3, 1, 5}, {2, 1, 5}, {1, 1, 0}, {3, 2, 0}, {4, 2, 5}, {3, 2, 0}}, {8, 6, 1, 3, 7, 4, 2, 0, 5}},
    {3, 5, {2, 0, 1, 2, 0}, 7, {{1, 0, -2}, {2, 0, -2}, {3, 0, 0}, {4, 0, -2}, {1, 0, 0}, {2, 0, 5}, {1, 0, 0}}, {0, 0, 1, 2, 2}},
};

static const struct Hashed kHashed[] = {
    {1, 2, 0, 5, 24040, 1, 0xE2BFC741u, 0x0F7694A7u},
    {1, 2, 1, 0, 25017, 1, 0xE2BFC741u, 0x1076963Au},
    {1, 2, 2, 0, 25994, 1, 0x94564661u, 0x1076963Au},
    {1, 3, 0, 5, 26971, 2, 0x3FFD84E3u, 0x10834CD3u},
    {1, 3, 1, 0, 27948, 3, 0xDDBF6ED2u, 0x22AE7A28u},
    {1, 3, 2, 0, 28925, 2, 0xDC51A26Eu, 0x22AE7A28u},
    {1, 7, 0, 5, 29902, 13, 0x8B4C933Eu, 0x4FE77536u},
    {1, 7, 1, 0, 30879, 14, 0xA06E0895u, 0xE4991188u},
    {1, 7, 2, 0, 31856, 16, 0x659D656Au, 0xE4991188u},
    {1, 64, 0, 5, 32833, 309, 0xD3A1616Du, 0xAC3BEE83u},
    {1, 64, 1, 0, 33810, 311, 0x319CAC08u, 0x6D3A0905u},
    {1, 64, 2, 0, 34787, 347, 0x4B2B8305u, 0x6D3A0905u},
    {1, 1000, 0, 5, 35764, 8932, 0xE6E13ED6u, 0xA1FD09E3u},
    {1, 1000, 1, 0, 36741, 11824, 0x0BB20B93u, 0x6CAC001Du},
    {1, 1000, 2, 0, 37718, 15002, 0x2DB31E51u, 0x6CAC001Du},
    {2, 2, 0, 5, 38695, 1, 0x94564661u, 0x6D71253Fu},
    {2, 2, 1, 0, 39672, 1, 0xE2BFC741u, 0x25937AACu},
    {2, 2, 2, 0, 40649, 1, 0x94564661u, 0x25937AACu},
    {2, 3, 0, 5, 41626, 5, 0x4FCEB5C1u, 0xF9A62997u},
    {2, 3, 1, 0, 42603, 3, 0xDDBF6ED2u, 0x7B503D5Eu},
    {2, 3, 2, 0, 43580, 2, 0xDC51A26Eu, 0x7B503D5Eu},
    {2, 7, 0, 5, 44557, 17, 0xF18B16CEu, 0x53401B6Au},
    {2, 7, 1, 0, 45534, 14, 0xA06E0895u, 0x6882547Au},
    {2, 7, 2, 0, 46511, 16, 0x659D656Au, 0x6882547Au},
    {2, 64, 0, 5, 47488, 297, 0x80A925CEu, 0xF4AA873Au},
    {2, 64, 1, 0, 48465, 311, 0x319CAC08u, 0x499151C5u},
    {2, 64, 2, 0, 49442, 347, 0x4B2B8305u, 0x499151C5u},
    {2, 1000, 0, 5, 50419, 8817, 0x1AD9CC3Cu, 0xC8BC8FB3u},
    {2, 1000, 1, 0, 51396, 8730, 0x4438A739u, 0xB8ED8AB5u},
    {2, 1000, 2, 0, 52373, 9340, 0x76180B2Du, 0xB8ED8AB5u},
    {4, 2, 0, 5, 53350, 1, 0x84DB0394u, 0xB64BB185u},
    {4, 2, 1, 0, 54327, 1, 0xE2BFC741u, 0x4BB53254u},
    {4, 2, 2, 0, 55304, 1, 0x94564661u, 0x4BB53254u},
    {4, 3, 0, 5, 56281, 3, 0xDDBF6ED2u, 0x88C1B482u},
    {4, 3, 1, 0, 57258, 3, 0xDDBF6ED2u, 0x253381B6u},
    {4, 3, 2, 0, 58235, 2, 0xDC51A26Eu, 0x253381B6u},
    {4, 7, 0, 5, 59212, 17, 0x182BD0EAu, 0xBEB7A6D4u},
    {4, 7, 1, 0, 60189, 14, 0xA06E0895u, 0xA2E2B772u},
    {4, 7, 2, 0, 61166, 16, 0x659D656Au, 0xA2E2B772u},
    {4, 64, 0, 5, 62143, 310, 0x927CF53Du, 0x76210E34u},
    {4, 64, 1, 0, 63120, 311, 0x319CAC08u, 0xF16D17C5u},
    {4, 64, 2, 0, 64097, 347, 0x4B2B8305u, 0xF16D17C5u},
    {4, 1000, 0, 5, 65074, 8699, 0xD4A3F7E1u, 0x67DA09A7u},
    {4, 1000, 1, 0, 66051, 8730, 0x4438A739u, 0x8E15CB75u},
    {4, 1000, 2, 0, 67028, 9340, 0x76180B2Du, 0x8E15CB75u},
    {8, 2, 0, 5, 68005, 1, 0xE2BFC741u, 0x16F6B737u},
    {8, 2, 1, 0, 68982, 1, 0xE2BFC741u, 0x5C33F8F5u},
    {8, 2, 2, 0, 69959, 1, 0x94564661u, 0xF33E07F5u},
    {8, 3, 0, 5, 70936, 3, 0xEEF7A887u, 0x37F494D6u},
    {8, 3, 1, 0, 71913, 3, 0xDDBF6ED2u, 0x9C5542B5u},
    {8, 3, 2, 0, 72890, 2, 0xDC51A26Eu, 0x2BC38EF5u},
    {8, 7, 0, 5, 73867, 14, 0xA3EB9B7Eu, 0x475421D3u},
    {8, 7, 1, 0, 74844, 14, 0xA06E0895u, 0x52FDCC95u},
    {8, 7, 2, 0, 75821, 16, 0x659D656Au, 0xB520F415u},
    {8, 64, 0, 5, 76798, 318, 0xC10BCE19u, 0x704E07A1u},
    {8, 64, 1, 0, 77775, 311, 0x319CAC08u, 0xBCAA4FC5u},
    {8, 64, 2, 0, 78752, 347, 0x4B2B8305u, 0xF3DBBBC5u},
    {8, 1000, 0, 5, 79729, 8459, 0x6E2DC0EAu, 0x07F6FBB1u},
    {8, 1000, 1, 0, 80706, 8730, 0x4438A739u, 0xD65E0185u},
    {8, 1000, 2, 0, 81683, 9340, 0x76180B2Du, 0x678DB9C5u},
    {12, 2, 0, 5, 82660, 1, 0x84DB0394u, 0xA06D1390u},
    {12, 2, 1, 0, 83637, 1, 0xE2BFC741u, 0x7C58F539u},
    {12, 2, 2, 0, 84614, 1, 0x94564661u, 0xF19EACBDu},
    {12, 3, 0, 5, 85591, 3, 0x1553E3D2u, 0x636F7602u},
    {12, 3, 1, 0, 86568, 3, 0xDDBF6ED2u, 0xBEFCFEC1u},
    {12, 3, 2, 0, 87545, 2, 0xDC51A26Eu, 0x1107E2F9u},
    {12, 7, 0, 5, 88522, 16, 0x84995375u, 0xE996424Du},
    {12, 7, 1, 0, 89499, 14, 0xDE0A2233u, 0xCA37EE01u},
    {12, 7, 2, 0, 90476, 13, 0x1EF51770u, 0x5A2DF101u},
    {12, 64, 0, 5, 91453, 306, 0x607CD57Cu, 0x621C84F1u},
    {12, 64, 1, 0, 92430, 348, 0x556500CFu, 0x5CA7F6C5u},
    {12, 64, 2, 0, 93407, 351, 0x02EF4F28u, 0x45EE1EC5u},
    {12, 1000, 0, 5, 94384, 8834, 0x6D66B86Cu, 0x0E017A08u},
    {12, 1000, 1, 0, 95361, 10183, 0x44647EE0u, 0x9D75EAC5u},
    {12, 1000, 2, 0, 96338, 10481, 0xD8AA50A1u, 0x32E61935u},
    {16, 2, 0, 5, 97315, 1, 0x84DB0394u, 0x22CA596Cu},
    {16, 2, 1, 0, 98292, 1, 0xE2BFC741u, 0xEF68196Du},
    {16, 2, 2, 0, 99269, 1, 0x94564661u, 0x537DF28Du},
    {16, 3, 0, 5, 100246, 3, 0xEEF7A887u, 0x85136A0Fu},
    {16, 3, 1, 0, 101223, 3, 0xDDBF6ED2u, 0x5E3E4E2Du},
    {16, 3, 2, 0, 102200, 2, 0xDC51A26Eu, 0xFFB25C4Du},
    {16, 7, 0, 5, 103177, 16, 0x0913204Bu, 0xEDCED0CAu},
    {16, 7, 1, 0, 104154, 14, 0xA06E0895u, 0x2818C6E5u},
    {16, 7, 2, 0, 105131, 16, 0x659D656Au, 0xA7CFEB35u},
    {16, 64, 0, 5, 106108, 317, 0x37EBC59Du, 0x6762A1AAu},
    {16, 64, 1, 0, 107085, 311, 0x319CAC08u, 0xACC4CAC5u},
    {16, 64, 2, 0, 108062, 347, 0x4B2B8305u, 0xD42D2945u},
    {16, 1000, 0, 5, 109039, 8436, 0x2ABE0BADu, 0x40752A41u},
    {16, 1000, 1, 0, 110016, 8730, 0x4438A739u, 0x28343B95u},
    {16, 1000, 2, 0, 110993, 9340, 0x76180B2Du, 0x253CB665u},
};

static u32 arenaWords[MAX_N * 16 / 4];
static u32 copyWords[MAX_N * 16 / 4];
static u32 keyBuf[MAX_N];
static u8 used[MAX_N];

static void TestExplicit(void)
{
    u32 t, i;
    for (t = 0; t < sizeof kExplicit / sizeof kExplicit[0]; t++) {
        const struct Explicit *e = &kExplicit[t];
        u8 *arena = (u8 *)arenaWords;
        u32 calls, logHash, finalHash, calls2, logHash2, finalHash2;
        int logOk = 1, finOk = 1;

        MakeElems(arena, e->keys, e->n, e->width);
        RunSort(arena, e->n, e->width, 0, &calls, &logHash, &finalHash);
        CHECK(calls == e->calls, "explicit %u: %u calls, want %u", t, calls, e->calls);
        for (i = 0; i < e->calls && i < LOG_CAP; i++) {
            if (gLog[i][0] != e->log[i][0] || gLog[i][1] != e->log[i][1] || gLog[i][2] != e->log[i][2]) {
                logOk = 0;
            }
        }
        CHECK(logOk, "explicit %u: comparator call log differs", t);
        for (i = 0; i < e->n; i++) {
            if (FinalValue(arena, i, e->width) != e->final[i]) {
                finOk = 0;
            }
        }
        CHECK(finOk, "explicit %u: final order differs", t);

        /* The work-buffer path must give the same calls and order. */
        MakeElems(arena, e->keys, e->n, e->width);
        RunSort(arena, e->n, e->width, 1, &calls2, &logHash2, &finalHash2);
        CHECK(calls2 == calls && logHash2 == logHash && finalHash2 == finalHash,
              "explicit %u: work-buffer run differs", t);
    }
}

static void TestHashed(void)
{
    u32 t;
    for (t = 0; t < sizeof kHashed / sizeof kHashed[0]; t++) {
        const struct Hashed *h = &kHashed[t];
        u8 *arena = (u8 *)arenaWords;
        u32 calls, logHash, finalHash, k;
        for (k = 0; k < 2; k++) {
            MakeKeys(h->kind, h->mod, h->seed, h->n, keyBuf);
            MakeElems(arena, keyBuf, h->n, h->width);
            RunSort(arena, h->n, h->width, (int)k, &calls, &logHash, &finalHash);
            CHECK(calls == h->calls, "hashed row %u (work=%u): %u calls, want %u", t, k, calls, h->calls);
            CHECK(logHash == h->logHash, "hashed row %u (work=%u): call log digest differs", t, k);
            CHECK(finalHash == h->finalHash, "hashed row %u (work=%u): final digest differs", t, k);
        }
    }
}

/* Property sweep over many sizes, widths and key shapes: sorted, a permutation of the input,
 * the same calls with and without a work buffer, and no calls for count <= 1. */
static void TestProperties(void)
{
    static const u32 widths[] = {1, 2, 3, 4, 5, 6, 8, 12, 16};
    static const u32 kinds[6][2] = {{0, 1u << 24}, {0, 5}, {1, 0}, {2, 0}, {3, 0}, {4, 0}};
    u32 wi, si, ki;
    for (wi = 0; wi < sizeof widths / sizeof widths[0]; wi++) {
        u32 w = widths[wi];
        for (si = 0; si < 67; si++) {
            /* sizes 0..65, then 1000 (only for widths 4, 8 and 16, to keep the run short) */
            u32 n = si < 66 ? si : 1000;
            if (n == 1000 && w != 4 && w != 8 && w != 16) {
                continue;
            }
            for (ki = 0; ki < 6; ki++) {
                u32 seed = 1000 + n * 37 + w * 11 + ki;
                u8 *arena = (u8 *)arenaWords;
                u8 *orig = (u8 *)copyWords;
                u32 calls, logHash, finalHash, calls2, logHash2, finalHash2, i, j;
                int sorted = 1, perm = 1;
                MakeKeys(kinds[ki][0], kinds[ki][1], seed, n, keyBuf);
                MakeElems(arena, keyBuf, n, w);
                memcpy(orig, arena, n * w);
                RunSort(arena, n, w, 0, &calls, &logHash, &finalHash);
                if (n <= 1) {
                    CHECK(calls == 0, "w=%u n=%u: comparator called for count<=1", w, n);
                    CHECK(memcmp(arena, orig, n * w) == 0, "w=%u n=%u: count<=1 changed data", w, n);
                    continue;
                }
                for (i = 0; i + 1 < n; i++) {
                    if (KeyOf(arena + i * w, w < 4 ? w : 4) > KeyOf(arena + (i + 1) * w, w < 4 ? w : 4)) {
                        sorted = 0;
                    }
                }
                memset(used, 0, n);
                for (i = 0; i < n && perm; i++) {
                    perm = 0;
                    for (j = 0; j < n; j++) {
                        if (!used[j] && memcmp(arena + i * w, orig + j * w, w) == 0) {
                            used[j] = 1;
                            perm = 1;
                            break;
                        }
                    }
                }
                CHECK(sorted, "w=%u n=%u kind=%u: not sorted", w, n, ki);
                CHECK(perm, "w=%u n=%u kind=%u: not a permutation of the input", w, n, ki);

                /* Same input again through the work-buffer path. */
                memcpy(arena, orig, n * w);
                RunSort(arena, n, w, 1, &calls2, &logHash2, &finalHash2);
                CHECK(calls2 == calls && logHash2 == logHash && finalHash2 == finalHash,
                      "w=%u n=%u kind=%u: work-buffer run differs", w, n, ki);
            }
        }
    }
}

int main(void)
{
    TestExplicit();
    TestHashed();
    TestProperties();
    printf("math_qsort: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
