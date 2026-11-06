/* Half-A L4 host fixture: synthetic engine-B states only (no game data).
 * Drives NitroPort_Render2DSplitIfChanged with partialB on (and off for
 * the control), comparing every frame bit-exactly against a whole render,
 * with the diagnostic dual-path compare asserting zero mismatches.
 * Includes the real implementation (like gpu_scroll_reuse.c); external
 * services below have no game/art/3D image. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include RENDER_SOURCE

_Static_assert(sizeof(u32) == 4 && sizeof(s32) == 4, "DS register widths");
_Static_assert(sizeof(u64) == 8, "DS tick width");

unsigned char NitroPort_IoReg[0x2000] __attribute__((aligned(8)));
unsigned char NitroPort_BgVram[0x80000], NitroPort_ObjVram[0x40000];
unsigned char NitroPort_DbBgVram[0x20000], NitroPort_DbObjVram[0x20000];
unsigned char NitroPort_LcdcVram[0xA4000], NitroPort_Pltt[0x800], NitroPort_Oam[0x800];
NitroLineRegs NitroPort_LineRegs;

/* Absent external services only: validation, hashing, sorting, pixels and
 * quads are all the real implementation included above. */
void NitroPort_Log(const char *fmt, ...) { (void)fmt; }
int NitroPort_Unimplemented(const char *what) { fprintf(stderr, "unexpected: %s\n", what); abort(); }
const uint16_t *NitroPort_Gx3dAcquireImage(void) { return NULL; }
void NitroPort_Gx3dReleaseImage(const uint16_t *p) { (void)p; }
const uint8_t *NitroPort_Gx3dImageAlpha(const uint16_t *p) { (void)p; return NULL; }
int NitroPort_Gx3dImageTranslucent(const uint16_t *p) { (void)p; return 0; }
uint32_t NitroPort_Gx3dImageCount(void) { return 0; }
int NitroPort_WideWindows(int e, int bg, NitroWideWindow *out, int max) { (void)e; (void)bg; (void)out; (void)max; return 0; }
int NitroPort_WidePictures(void) { return 0; }
uint32_t NitroPort_WideWindowsSerial(void) { return 0; }
int NitroPort_WideSplits(int e, NitroWideSplit *out) { (void)e; (void)out; return 0; }
int NitroPort_WideParts(int e, NitroWidePart *out) { (void)e; (void)out; return 0; }
void NitroPort_WideLayout(int e, NitroRender2DSplit *split) { (void)e; (void)split; }
int NitroPort_WideArtCount(void) { return 0; }
uint32_t NitroPort_WideArtSerial(void) { return 0; }
const uint16_t *NitroPort_WideArtPicture(int p, int *w, int *h, int *dx, int *dy)
{ (void)p; (void)w; (void)h; (void)dx; (void)dy; return NULL; }
const uint16_t *NitroPort_WideArtFindNear(uint64_t key, int ch, int cv, int ph, int pv,
                                       int mh, int mv, int *h, int *v, int *full)
{ (void)key; (void)ch; (void)cv; (void)ph; (void)pv; (void)mh; (void)mv; (void)h; (void)v; (void)full; return NULL; }
int NitroPort_WideArtExtent(const uint16_t *p, int *l, int *r) { (void)p; (void)l; (void)r; return 0; }
const uint8_t *NitroPort_WideArtRows(const uint16_t *p) { (void)p; return NULL; }
int NitroPort_WideArtMovedOffset(int m, int l, int r) { (void)m; (void)l; (void)r; return m; }
int NitroPort_WideArtUnshared(void) { return 0; }
int NitroPort_WideArtToShare(uint64_t key, int i, const uint16_t **p, int *h, int *v,
                            int *full, char *name, size_t n)
{ (void)key; (void)i; (void)p; (void)h; (void)v; (void)full; (void)name; (void)n; return 0; }
int NitroPort_WideArtShareWrite(const char *name, const uint16_t *p) { (void)name; (void)p; return 0; }

static int failures, checks;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "%d: %s\n", __LINE__, #x); } } while (0)

static void b_wr16(unsigned off, u16 v) { memcpy(NitroPort_IoReg + 0x1000 + off, &v, 2); }
static void b_wr32(unsigned off, u32 v) { memcpy(NitroPort_IoReg + 0x1000 + off, &v, 4); }
static void wr16(unsigned off, u16 v) { memcpy(NitroPort_IoReg + off, &v, 2); }

/* Engine-B OAM entry i. */
static void spr(int i, int x, int y, int shape, int size, int tile, int pal, int prio, int mode)
{
    u8 *o = NitroPort_Oam + 0x400 + (u32)i * 8;
    u16 a0 = (u16)((y & 0xFF) | (mode << 10) | (shape << 14));
    u16 a1 = (u16)((x & 0x1FF) | (size << 14));
    u16 a2 = (u16)((tile & 0x3FF) | (prio << 10) | (pal << 12));

    memcpy(o, &a0, 2);
    memcpy(o + 2, &a1, 2);
    memcpy(o + 4, &a2, 2);
    memset(o + 6, 0, 2);
}
static void spr_hide(int i)
{
    u16 h = 0x200;

    memcpy(NitroPort_Oam + 0x400 + (u32)i * 8, &h, 2);
}

static u16 under[256 * 192], over[256 * 192], refOver[256 * 192];
static u8 alpha[256 * 192], refAlpha[256 * 192];
static u16 canaryOver[256 * 192];
static u8 canaryAlpha[256 * 192];
static u16 wideUnder[320 * 192], wideOver[320 * 192], wideRef[320 * 192], wideRefOver[320 * 192];
static u8 wideAlpha[320 * 192], wideRefAlpha[320 * 192];

/* A field-menu-like engine-B scene: two text BGs, a grid of icon sprites. */
static void reset_menu(void)
{
    unsigned i;

    memset(NitroPort_IoReg, 0, sizeof(NitroPort_IoReg));
    memset(NitroPort_Pltt, 0, sizeof(NitroPort_Pltt));
    memset(NitroPort_DbBgVram, 0, sizeof(NitroPort_DbBgVram));
    memset(NitroPort_DbObjVram, 0, sizeof(NitroPort_DbObjVram));
    memset(NitroPort_Oam, 0, sizeof(NitroPort_Oam));
    memset(&NitroPort_LineRegs, 0, sizeof(NitroPort_LineRegs));
    wr16(0x304, 0x202); /* both engines powered */
    /* Mode 1 graphics, BG1+BG2, OBJ, 1D OBJ mapping. */
    b_wr32(0, 0x10000u | 0x1u | 0x200u | 0x400u | 0x1000u | 0x10u);
    b_wr16(0x0A, (4u << 8) | 0u); /* BG1: prio 0, map at 0x2000, 256x256 4bpp */
    b_wr16(0x0C, (6u << 8) | 1u); /* BG2: prio 1, map at 0x3000 */
    /* One 4bpp tile per index pair: tile t is mostly colour t. */
    for (i = 1; i < 64; i++) {
        unsigned r;

        for (r = 0; r < 8; r++) {
            NitroPort_DbBgVram[i * 32 + r * 4] = (u8)(i | (i << 4));
            NitroPort_DbBgVram[i * 32 + r * 4 + 1] = (u8)(i | (i << 4));
            NitroPort_DbBgVram[i * 32 + r * 4 + 2] = (u8)(i | (i << 4));
            NitroPort_DbBgVram[i * 32 + r * 4 + 3] = (u8)(i | (i << 4));
        }
    }
    for (i = 0; i < 32 * 32; i++) {
        u16 tile = (u16)(1 + (i % 62));

        memcpy(NitroPort_DbBgVram + 0x2000 + i * 2, &tile, 2);
        tile = (u16)(1 + ((i + 31) % 62));
        memcpy(NitroPort_DbBgVram + 0x3000 + i * 2, &tile, 2);
    }
    ((u16 *)NitroPort_Pltt)[0x200] = 0x421;
    for (i = 1; i < 64; i++) {
        ((u16 *)NitroPort_Pltt)[0x200 + i] = (u16)(i | (i << 5) | (i << 10));
    }
    /* OBJ tiles + palette. */
    for (i = 0; i < 16; i++) {
        unsigned r;

        for (r = 0; r < 8; r++) {
            memset(NitroPort_DbObjVram + i * 1024 + r * 128, (int)(0x11 * (i + 1)), 128);
        }
    }
    ((u16 *)NitroPort_Pltt)[0x300] = 0x421;
    for (i = 1; i < 16; i++) {
        ((u16 *)NitroPort_Pltt)[0x300 + i] = (u16)(31 - i);
    }
    /* Ten 16x16 icons in two rows, rest hidden. */
    for (i = 0; i < 128; i++) {
        spr_hide((int)i);
    }
    for (i = 0; i < 10; i++) {
        spr((int)i, 20 + (int)(i % 5) * 44, 40 + (int)(i / 5) * 44, 0, 1, (int)(i % 16), 0, 1, 0);
    }
    memset(under, 0x55, sizeof(under));
    memset(over, 0x55, sizeof(over));
    memset(alpha, 0x55, sizeof(alpha));
    /* over/overAlpha are canaries: engine B never draws into them, and the
     * fixture asserts neither the band path nor the whole path does. */
    memset(refOver, 0x55, sizeof(refOver));
    memset(refAlpha, 0x55, sizeof(refAlpha));
    memset(canaryOver, 0x55, sizeof(canaryOver));
    memset(canaryAlpha, 0x55, sizeof(canaryAlpha));
}

static NitroRender2DSplit make_split(u16 *o, u8 *a)
{
    NitroRender2DSplit s;

    memset(&s, 0, sizeof(s));
    s.snapshot = 1;
    s.over = o;
    s.overAlpha = a;
    s.windowsToEdge = 1;
    return s;
}

/* One frame: the fast decision into under, snapshotted, then a whole
 * reference render into the same buffer; they must match exactly. The
 * same buffer keeps the retained-buffer contract the drawer honors, so
 * the fast path is exercised exactly as in production. over/overAlpha
 * are canaries: engine B never touches them, and neither path may. */
static u16 saved[256 * 192];
static u64 gFast;
static int gKind, gY0, gY1;
static u64 lines1(void) { return sProfile[1].lines; }
#ifdef HGSS_A0_DIAG
static u64 reason_total(const NitroPartialBReasons *p)
{
    return p->shared + p->objWide + p->bgKind + p->bgRegs + p->bgLines +
           p->bgChar + p->bgNoMap + p->bgNoBand + p->bgWide + p->mixed;
}
#endif
static int frame(void)
{
    NitroPort_Render2DTakeState(1);
    NitroRender2DSplit cand = make_split(over, alpha);
    u64 l0 = lines1();
    u64 f0 = sPartial.frames;
#ifdef HGSS_A0_DIAG
    NitroPartialBReasons before, after;
    u64 whole0 = sPartial.fallbacks;
    NitroPort_Render2DPartialBReasons(&before);
#endif
    int drawn = NitroPort_Render2DSplitIfChanged(1, under, &cand);

#ifdef HGSS_A0_DIAG
    NitroPort_Render2DPartialBReasons(&after);
    CHECK(reason_total(&after) - reason_total(&before) == sPartial.fallbacks - whole0);
#endif
    gFast = lines1() - l0;
    CHECK(memcmp(over, canaryOver, sizeof(over)) == 0);
    CHECK(memcmp(alpha, canaryAlpha, sizeof(alpha)) == 0);
    /* The L5 handoff, read where the drawer thread's publish would read
     * it: right after the render, before the reference render below. */
    if (sPartialB) {
        int k = -1, a = -9, b = -9;

        NitroPort_Render2DPartialBand(1, &k, &a, &b);
        gKind = k;
        gY0 = a;
        gY1 = b;
        if (sPartial.frames == f0) {
            /* The legacy path drew (a fill invalidates, a whole render
             * refreshes): nothing L4-specific to assert. */
            CHECK(k == 1 || k == 0);
        } else if (drawn == 0) {
            CHECK(k == 0);
        } else {
            CHECK(k == 1 || k == 2);
        }
        if (k == 2) {
            CHECK(a >= 0 && b <= 192 && a < b);
        }
    } else {
        gKind = -1;
        gY0 = gY1 = -1;
    }
    memcpy(saved, under, sizeof(under));
    {
        NitroRender2DSplit ref = make_split(refOver, refAlpha);

        NitroPort_Render2DSplit(1, under, &ref);
    }
    CHECK(memcmp(saved, under, sizeof(under)) == 0);
    CHECK(memcmp(refOver, canaryOver, sizeof(refOver)) == 0);
    CHECK(memcmp(refAlpha, canaryAlpha, sizeof(refAlpha)) == 0);
    return drawn;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int d, i;

    /* Establish + control: partialB off behaves like today (full renders). */
    reset_menu();
    NitroPort_Render2DSetPartialB(0, 0);
    NitroPort_Render2DTakeState(1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DSplit(1, under, &s);
    }
    {
        u64 fast = 0;

        for (i = 0; i < 8; i++) {
            spr(0, 20 + i, 40, 0, 1, 0, 0, 1, 0);
            d = frame();
            CHECK(d == 1);
            fast += gFast;
        }
        CHECK(sPartial.frames == 0);
        CHECK(fast == 8u * 192u);
        fflush(stdout);
    printf("control off: 8 frames, %llu fast scanlines (whole each), exact\n",
               (unsigned long long)fast);
    }

    /* Icon animation: one sprite steps right. Every frame must take the
     * OBJ band path and match the whole render. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    {
        u64 takes0 = sPartial.objTakes;

        for (i = 0; i < 24; i++) {
            spr(0, 21 + i, 40, 0, 1, 0, 0, 1, 0);
            d = frame();
            CHECK(d == 1);
            CHECK(gKind == 2);
            CHECK(gY0 <= 40 && gY1 >= 56);
        }
        CHECK(sPartial.objTakes - takes0 == 24);
        CHECK(sPartial.diagMismatch == 0);
        fflush(stdout);
    printf("icon anim: 24 frames exact, diag 0 (scanlines measured diag-off below)\n");
    }

    /* Highlight: the OBJ palette alone changes. The band must cover all
     * visible sprites. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    for (i = 0; i < 8; i++) {
        ((u16 *)NitroPort_Pltt)[0x301] = (u16)(i + 1);
        d = frame();
        CHECK(d == 1);
        CHECK(gKind == 2);
        CHECK(gY0 <= 40 && gY1 >= 40 + 44 + 16);
    }
    CHECK(sPartial.diagMismatch == 0);
    fflush(stdout);
    printf("highlight: 8 frames of OBJ-palette change, exact, diag 0\n");

    /* The renderer uses the taken palette. A later live palette write must
     * not be adopted until the next snapshot, or that snapshot can be
     * incorrectly skipped with stale sprite colours. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        u64 takes, fallbacks;
        u32 snapshotHash;
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSetReferenceHash(1, 0);
        snapshotHash = EngineVramSignature(1, sState[1].io + 0x1000, sState[1].oam);
        NitroPort_Pltt[0x602] ^= 0x1f;
        CHECK(EngineVramSignature(1, sState[1].io + 0x1000, sState[1].oam) == snapshotHash);
        NitroPort_Render2DSetReferenceHash(0, 0);
        s = make_split(over, alpha);
        d = NitroPort_Render2DSplitIfChanged(1, under, &s);
        CHECK(d == 0); /* taken palette has not changed yet */
        takes = sPartial.objTakes;
        fallbacks = sPartial.fallbacks;
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.objTakes == takes + 1 || sPartial.fallbacks == fallbacks + 1);
        CHECK(sPartial.diagMismatch == 0);
    }

    /* Hide toggle on one icon, then on an already-hidden entry. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    spr_hide(3);
    d = frame();
    CHECK(d == 1);
    CHECK(sPartial.diagMismatch == 0);
    /* Nudge a hidden entry (OAM differs, no lines): nothing can have
     * changed. A spr_hide only writes attr0, so nudge attr1 explicitly:
     * this entry was hidden with stale coordinates. */
    {
        u16 x = 10;

        memcpy((u8 *)NitroPort_Oam + 0x400 + 100 * 8 + 2, &x, 2);
    }
    d = frame();
    CHECK(d == 0);
    CHECK(sPartial.unchangedZero > 0);
    fflush(stdout);
    printf("hide: take then unchanged-zero, exact\n");

    /* Wrap-around sprite at oy 250 and an offscreen slide. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    spr(11, 100, 250, 0, 1, 1, 0, 1, 0);
    d = frame();
    CHECK(d == 1);
    /* Showing a sprite changes the referenced-tile chain, so the band
     * conservatively covers all visible sprites' lines. */
    CHECK(gKind == 2 && gY0 == 0 && gY1 <= 100);
    for (i = 1; i <= 4; i++) {
        spr(11, 100 + i, 250, 0, 1, 1, 0, 1, 0);
        d = frame();
        CHECK(d == 1);
    }
    spr(12, 300, 60, 0, 1, 2, 0, 1, 0);
    d = frame();
    CHECK(d == 1);
    for (i = 1; i <= 4; i++) {
        spr(12, 300 - i * 8, 60, 0, 1, 2, 0, 1, 0);
        d = frame();
        CHECK(d == 1);
    }
    CHECK(sPartial.diagMismatch == 0);
    fflush(stdout);
    printf("wrap+offscreen: exact, diag 0\n");

    /* One text tile edited (bag-list-like): only its rows re-render. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    {
        u16 tile = 7;
        u64 takes0 = sPartial.bgTakes;

        memcpy(NitroPort_DbBgVram + 0x2000 + (10 * 32 + 5) * 2, &tile, 2);
        d = frame();
        CHECK(d == 1);
        CHECK(gKind == 2);
        CHECK(gY0 == 80 && gY1 == 88);
        CHECK(sPartial.bgTakes - takes0 == 1);
        CHECK(sPartial.diagMismatch == 0);
        fflush(stdout);
    printf("tile edit: band [%d,%d), exact (scanlines measured diag-off below)\n", gY0, gY1);
    }

    /* A scroll moves the BG: whole. */
    {
        u64 fb = sPartial.fallbacks;

        b_wr16(0x16, 8);
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.fallbacks - fb == 1);
        CHECK(sPartial.diagMismatch == 0);
        fflush(stdout);
    printf("scroll: whole fallback, exact\n");
    }

    /* Blend on: whole. Brightness ramp: no redraw at all (fades). */
    {
        u64 fb = sPartial.fallbacks;
        u64 unchanged;
#ifdef HGSS_A0_DIAG
        NitroPartialBReasons before, after;
        NitroPort_Render2DPartialBReasons(&before);
#endif

        b_wr16(0x50, 0x1F | (1u << 6));
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.fallbacks - fb == 1);
#ifdef HGSS_A0_DIAG
        NitroPort_Render2DPartialBReasons(&after);
        CHECK(after.shared == before.shared + 1);
        CHECK(after.rest == before.rest + 1);
        CHECK(after.restRegs == before.restRegs + 1);
#endif
        b_wr16(0x50, 0);
        d = frame();
        CHECK(d == 1);
        unchanged = sPartial.unchangedZero;
        for (i = 1; i <= 5; i++) {
            b_wr16(0x6C, (u16)(0x4000 | i));
            d = frame();
            CHECK(d == 0);
        }
        CHECK(sPartial.unchangedZero - unchanged == 5);
        CHECK(sPartial.diagMismatch == 0);
        fflush(stdout);
    printf("blend+fades: whole then 5 unchanged, exact\n");
    }

    /* Window 0 on: whole. */
    {
        u64 fb = sPartial.fallbacks;

        b_wr32(0, 0x10000u | 0x1u | 0x200u | 0x400u | 0x1000u | 0x10u | 0x2000u);
        b_wr16(0x40, 0x0010);
        b_wr16(0x44, 0x0010);
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.fallbacks - fb == 1);
        CHECK(sPartial.diagMismatch == 0);
        fflush(stdout);
    printf("window: whole fallback, exact\n");
    }

    /* Affine BG3 + icon move: OBJ band still exact (affine re-renders in
     * the band). Affine map content change: whole. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    b_wr32(0, 0x10000u | 0x1u | 0x200u | 0x400u | 0x800u | 0x1000u | 0x10u);
    b_wr16(0x0E, (2u << 8) | 3u); /* BG3 affine, map at 0x1000, 128x128 */
    b_wr16(0x20, 256); b_wr16(0x22, 0); b_wr16(0x24, 0); b_wr16(0x26, 256);
    memset(NitroPort_DbBgVram + 0x1000, 3, 128 * 128);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    for (i = 0; i < 4; i++) {
        spr(1, 64 + i * 2, 84, 0, 1, 1, 0, 1, 0);
        d = frame();
        CHECK(d == 1);
    }
    {
        u64 fb = sPartial.fallbacks;

        NitroPort_DbBgVram[0x1000 + 100] ^= 0xFF;
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.fallbacks - fb == 1);
    }
    CHECK(sPartial.diagMismatch == 0);
    fflush(stdout);
    printf("affine: takes then whole on map change, exact\n");

    /* H-blank line scrolls: OAM moves stay banded, text edits go whole. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    NitroPort_LineRegs.active = 1;
    for (i = 0; i < 192; i++) {
        memcpy(NitroPort_LineRegs.regs[1][i], NitroPort_IoReg + 0x1000, 0x70);
    }
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    for (i = 0; i < 4; i++) {
        spr(2, 108 + i, 84, 0, 1, 2, 0, 1, 0);
        d = frame();
        CHECK(d == 1);
    }
    {
        u64 fb = sPartial.fallbacks;
        u16 tile = 9;

        memcpy(NitroPort_DbBgVram + 0x2000 + (20 * 32 + 2) * 2, &tile, 2);
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.fallbacks - fb == 1);
    }
    CHECK(sPartial.diagMismatch == 0);
    fflush(stdout);
    printf("hblank: obj takes, text edit whole, exact\n");

    /* Semi-transparent + affine sprites, mosaic steady, OBJ window. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    spr(20, 30, 30, 0, 1, 3, 0, 0, 1); /* semi-transparent */
    spr(21, 200, 150, 0, 2, 4, 0, 1, 0);
    b_wr16(0x4C, 0x1111); /* mosaic on both planes */
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    for (i = 1; i <= 4; i++) {
        spr(20, 30 + i, 30, 0, 1, 3, 0, 0, 1);
        spr(21, 200 - i, 150, 0, 2, 4, 0, 1, 0);
        d = frame();
        CHECK(d == 1);
    }
    {
        u64 fb = sPartial.fallbacks;

        b_wr32(0, 0x10000u | 0x1u | 0x200u | 0x400u | 0x1000u | 0x10u | 0x8000u);
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.fallbacks - fb == 1);
        for (i = 0; i < 4; i++) {
            spr(20, 35 + i, 30, 0, 1, 3, 0, 0, 1);
            d = frame();
            CHECK(d == 1);
        }
    }
    CHECK(sPartial.diagMismatch == 0);
    fflush(stdout);
    printf("semi/objwin/mosaic: exact, diag 0\n");

    /* Display-mode switch, power toggle, wide layout: legacy whole frames,
     * then L4 resumes banding. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    spr(0, 21, 40, 0, 1, 0, 0, 1, 0);
    d = frame();
    CHECK(d == 1);
    b_wr32(0, (2u << 16) | (0u << 18));
    d = frame();
    CHECK(d == 1);
    b_wr32(0, 0x10000u | 0x1u | 0x200u | 0x400u | 0x1000u | 0x10u);
    d = frame();
    CHECK(d == 1);
    wr16(0x304, 0x002);
    d = frame();
    CHECK(d == 1);
    wr16(0x304, 0x202);
    d = frame();
    CHECK(d == 1);
    {
        u64 takes0 = sPartial.objTakes;

        for (i = 0; i < 4; i++) {
            spr(0, 22 + i, 40, 0, 1, 0, 0, 1, 0);
            d = frame();
            CHECK(d == 1);
        }
        CHECK(sPartial.objTakes - takes0 == 4);
    }
    CHECK(sPartial.diagMismatch == 0);
    fflush(stdout);
    printf("modes/power: legacy wholes then resumed takes, exact\n");

    /* Wide engine-B picture: legacy whole, then resume. */
    {
        NitroRender2DSplit ws;

        memset(&ws, 0, sizeof(ws));
        ws.snapshot = 1;
        ws.over = wideOver;
        ws.overAlpha = wideAlpha;
        ws.windowsToEdge = 1;
        ws.width = 320;
        NitroPort_Render2DTakeState(1);
        d = NitroPort_Render2DSplitIfChanged(1, wideUnder, &ws);
        CHECK(d == 1);
        NitroPort_Render2DTakeState(1);
        memset(&ws, 0, sizeof(ws));
        ws.snapshot = 1;
        ws.over = wideRefOver;
        ws.overAlpha = wideRefAlpha;
        ws.windowsToEdge = 1;
        ws.width = 320;
        NitroPort_Render2DSplit(1, wideRef, &ws);
        CHECK(memcmp(wideUnder, wideRef, sizeof(wideUnder)) == 0);
        /* The first narrow frame re-establishes (foreign buffer guard);
         * the next one bands again. */
        spr(0, 26, 40, 0, 1, 0, 0, 1, 0);
        d = frame();
        CHECK(d == 1);
        {
            u64 takes0 = sPartial.objTakes;

            spr(0, 27, 40, 0, 1, 0, 0, 1, 0);
            d = frame();
            CHECK(d == 1);
            CHECK(sPartial.objTakes - takes0 == 1);
        }
    }
    CHECK(sPartial.diagMismatch == 0);
    fflush(stdout);
    printf("wide: legacy whole then resumed take, exact\n");

    /* Buffer swap guard: a foreign buffer never band-renders (the legacy
     * path draws it whole, declining L4 without counting an L4 frame);
     * the original buffer keeps working after. */
    {
        static u16 other[256 * 192];
        u64 fr0 = sPartial.frames;

        memset(other, 0x55, sizeof(other));
        NitroPort_Render2DTakeState(1);
        {
            NitroRender2DSplit s = make_split(over, alpha);
            (void)NitroPort_Render2DSplitIfChanged(1, other, &s);
        }
        CHECK(sPartial.frames - fr0 == 0);
        spr(0, 27, 40, 0, 1, 0, 0, 1, 0);
        d = frame();
        CHECK(d == 1);
        fflush(stdout);
    printf("swap guard: no crash, original buffer still exact\n");
    }

    /* A band covering most lines falls back to whole. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    for (i = 0; i < 24; i++) {
        spr(30 + i, 40, i * 8, 1, 1, (int)(i % 16), 0, 1, 0);
    }
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    {
        u64 fb = sPartial.fallbacks;

        for (i = 0; i < 24; i++) {
            spr(30 + i, 41, i * 8, 1, 1, (int)(i % 16), 0, 1, 0);
        }
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.fallbacks - fb == 1);
    }
    CHECK(sPartial.diagMismatch == 0);
    fflush(stdout);
    printf("big band: whole fallback, exact\n");

    /* No visible sprites: tile churn draws nothing. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    for (i = 0; i < 128; i++) {
        spr_hide(i);
    }
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    spr(0, 50, 50, 0, 1, 5, 0, 1, 0);
    d = frame();
    CHECK(d == 1);
    spr_hide(0);
    d = frame();
    CHECK(d == 1);
    {
        u64 z0 = sPartial.unchangedZero;

        /* Nudge a hidden entry (OAM differs, no lines) while churning
         * tiles no visible sprite uses: nothing can have changed. A
         * spr_hide only writes attr0, so nudge attr1 explicitly: the
         * establish hid this entry with stale coordinates. */
        {
            u16 x = 10;

            memcpy((u8 *)NitroPort_Oam + 0x400 + 100 * 8 + 2, &x, 2);
        }
        NitroPort_DbObjVram[5 * 32] ^= 0xFF;
        d = frame();
        CHECK(d == 0);
        CHECK(sPartial.unchangedZero - z0 == 1);
    }
    CHECK(sPartial.diagMismatch == 0);
    fflush(stdout);
    printf("obj churn invisible: unchanged-zero, exact\n");

    /* Character changes with a stable text map localize to the rows that
     * reference the changed tile. The oracle checks every pixel. */
    reset_menu();
    NitroPort_Render2DSetPartialBChar(1);
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        u16 one = 1, three = 3;
        u64 takes;
        b_wr32(0, 0x10000u | 0x200u);
        b_wr16(0x16, 4); /* BG1 vertical scroll */
        for (i = 0; i < 32 * 32; i++)
            memcpy(NitroPort_DbBgVram + 0x2000 + i * 2, &one, 2);
        memcpy(NitroPort_DbBgVram + 0x2000 + (5 * 32 + 5) * 2, &three, 2);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
        takes = sPartial.charTakes;
        NitroPort_DbBgVram[3 * 32] ^= 0xff;
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.charTakes == takes + 1);
        CHECK(gKind == 2 && gY0 == 36 && gY1 == 44);
        NitroPort_DbBgVram[2 * 32] ^= 0xff; /* included in hash, not map */
        d = frame();
        CHECK(d == 0);
        CHECK(sPartial.charTakes == takes + 1);
        CHECK(sPartial.diagMismatch == 0);
        {
            /* A tile referenced only below the visible region changes no
             * output; later scrolling forces a whole redraw and reveals it. */
            u16 four = 4;
            memcpy(NitroPort_DbBgVram + 0x2000 + (25 * 32 + 5) * 2, &four, 2);
            d = frame();
            CHECK(d == 1); /* map change is conservatively whole if offscreen */
            NitroPort_DbBgVram[4 * 32] ^= 0xff;
            d = frame();
            CHECK(d == 0);
            b_wr16(0x16, 80);
            d = frame();
            CHECK(d == 1);
            CHECK(sPartial.diagMismatch == 0);
        }
    }
    /* 8bpp tiles use 64 bytes and the same conservative row union. */
    reset_menu();
    {
        NitroRender2DSplit s = make_split(over, alpha);
        u16 one = 1, three = 3;
        u64 takes;
        b_wr32(0, 0x10000u | 0x200u);
        b_wr16(0x0a, (4u << 8) | 0x80);
        for (i = 0; i < 32 * 32; i++)
            memcpy(NitroPort_DbBgVram + 0x2000 + i * 2, &one, 2);
        memcpy(NitroPort_DbBgVram + 0x2000 + (8 * 32 + 5) * 2, &three, 2);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
        takes = sPartial.charTakes;
        NitroPort_DbBgVram[3 * 64] ^= 0xff;
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.charTakes == takes + 1);
        CHECK(gKind == 2 && gY0 == 64 && gY1 == 72);
        CHECK(sPartial.diagMismatch == 0);
        {
            u64 fb = sPartial.fallbacks;
            /* Simultaneous map and character changes stay whole. */
            memcpy(NitroPort_DbBgVram + 0x2000 + (9 * 32 + 5) * 2, &three, 2);
            NitroPort_DbBgVram[3 * 64] ^= 0xff;
            d = frame();
            CHECK(d == 1 && sPartial.fallbacks == fb + 1);
        }
        /* Mosaic can spread a source row, so character changes decline. */
        b_wr16(0x0a, (4u << 8) | 0x80 | 0x40);
        d = frame();
        CHECK(d == 1);
        {
            u64 fb = sPartial.fallbacks;
            NitroPort_DbBgVram[3 * 64] ^= 0xff;
            d = frame();
            CHECK(d == 1 && sPartial.fallbacks == fb + 1);
        }
        /* Missing character storage must never reuse stale pixels. */
        b_wr16(0x0a, (4u << 8) | 0x80);
        d = frame();
        CHECK(d == 1);
        {
            u64 fb = sPartial.fallbacks;
            free(sPartial.charData[1]);
            sPartial.charData[1] = NULL;
            sPartial.charBytes[1] = 0;
            NitroPort_DbBgVram[3 * 64] ^= 0xff;
            d = frame();
            CHECK(d == 1 && sPartial.fallbacks == fb + 1);
        }
        {
            unsigned mismatches = sPartial.diagMismatch;
            /* A stale pixel outside the band must trigger the full oracle
             * and recover, not pass silently as a verified BG band. */
            under[0] ^= 1;
            NitroPort_DbBgVram[3 * 64] ^= 0xff;
            d = frame();
            CHECK(d == 1 && sPartial.diagMismatch == mismatches + 1);
        }
    }
    NitroPort_Render2DSetPartialBChar(0);

    /* With the new switch off, character changes still fall back. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    {
        u64 fb = sPartial.fallbacks;
        u16 tile = 11;

        NitroPort_DbBgVram[3 * 32] ^= 0xFF;
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.fallbacks - fb == 1);
        memcpy(NitroPort_DbBgVram + 0x2000 + (5 * 32 + 5) * 2, &tile, 2);
        memcpy(NitroPort_DbBgVram + 0x3000 + (6 * 32 + 6) * 2, &tile, 2);
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.fallbacks - fb == 2);
    }
    CHECK(sPartial.diagMismatch == 0);
    fflush(stdout);
    printf("char-only + two-bg: whole fallbacks, exact\n");

    /* Three changed BGs must stay a multi-layer fallback. The sentinel for
     * two changed layers must not turn back into a single-layer index. */
    reset_menu();
    b_wr32(0, 0x10000u | 0x100u | 0x200u | 0x400u | 0x1000u | 0x10u);
    b_wr16(0x08, (8u << 8) | 2u);
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        u16 tile = 7;
        u64 fb;

        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
        fb = sPartial.fallbacks;
        memcpy(NitroPort_DbBgVram + 0x2000 + (3 * 32 + 3) * 2, &tile, 2);
        memcpy(NitroPort_DbBgVram + 0x3000 + (8 * 32 + 3) * 2, &tile, 2);
        memcpy(NitroPort_DbBgVram + 0x4000 + (13 * 32 + 3) * 2, &tile, 2);
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.fallbacks - fb == 1);
        CHECK(sPartial.diagMismatch == 0);
    }

    /* Affine maps contain byte-sized tile IDs. An odd map byte naming tile
     * 252 must include that tile in the referenced signature. */
    reset_menu();
    {
        PartialBParts before, after;
        int modelled = 1;
        b_wr32(0, 0x10000u | 1u | 0x800u); /* graphics, mode 1, BG3 */
        b_wr16(0x0e, 2u << 8);             /* affine map at 0x1000 */
        memset(NitroPort_DbBgVram + 0x1000, 0, 16 * 16);
        NitroPort_DbBgVram[0x1001] = 252;
        (void)EngineBReferencedSignature(NitroPort_IoReg + 0x1000, NitroPort_Oam, NitroPort_Pltt, &modelled, &before);
        CHECK(modelled);
        NitroPort_DbBgVram[252 * 64] ^= 0xff;
        modelled = 1;
        (void)EngineBReferencedSignature(NitroPort_IoReg + 0x1000, NitroPort_Oam, NitroPort_Pltt, &modelled, &after);
        CHECK(modelled && before.bgChar[3] != after.bgChar[3]);
        /* Extended bitmap addressing is deliberately not modelled. */
        b_wr32(0, 0x10000u | 3u | 0x800u);
        b_wr16(0x0e, 0x80);
        modelled = 1;
        (void)EngineBReferencedSignature(NitroPort_IoReg + 0x1000, NitroPort_Oam, NitroPort_Pltt, &modelled, &after);
        CHECK(!modelled);
    }

    /* Extended-palette bank bytes change: whole (conservative). */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 1);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    {
        u64 fb = sPartial.fallbacks;

        NitroPort_IoReg[0x248] = 0x82;
        d = frame();
        CHECK(d == 1);
        NitroPort_LcdcVram[sBankLcdc[7]] ^= 0xFF;
        d = frame();
        CHECK(d == 1);
        CHECK(sPartial.fallbacks - fb >= 1);
    }
    CHECK(sPartial.diagMismatch == 0);
    fflush(stdout);
    printf("extpal: whole fallback, exact\n");

    /* Attribution, diag off (the diagnostic's whole renders would pollute
     * the scanline count): the same icon animation plus one tile edit. */
    reset_menu();
    NitroPort_Render2DSetPartialB(1, 0);
    {
        NitroRender2DSplit s = make_split(over, alpha);
        NitroPort_Render2DTakeState(1);
        NitroPort_Render2DSplit(1, under, &s);
    }
    {
        u64 refBytes = sRefHashBytes;
        u64 takes0 = sPartial.objTakes + sPartial.bgTakes;
        u64 before, fast = 0;

        for (i = 0; i < 24; i++) {
            spr(0, 21 + i, 40, 0, 1, 0, 0, 1, 0);
            d = frame();
            CHECK(d == 1);
            fast += gFast;
        }
        before = lines1();
        {
            u16 tile = 7;

            memcpy(NitroPort_DbBgVram + 0x2000 + (10 * 32 + 5) * 2, &tile, 2);
            d = frame();
            CHECK(d == 1);
            CHECK(gFast == 8u);
            fast += gFast;
        }
        fflush(stdout);
    printf("attribution: 24 icon frames + 1 tile edit = %llu fast scanlines (whole would be %u), refHash +%llu bytes\n",
               (unsigned long long)fast, 25 * 192,
               (unsigned long long)(sRefHashBytes - refBytes));
        CHECK(fast < (24u * 192u + 192u) / 4u);
        CHECK(sPartial.objTakes + sPartial.bgTakes - takes0 == 25);
        CHECK(sPartial.diagMismatch == 0);
        (void)before;
    }

    fflush(stdout);
    printf("partialB: %d checks, %d failures (diag mismatches %u)\n", checks, failures, sPartial.diagMismatch);
    return failures != 0;
}
