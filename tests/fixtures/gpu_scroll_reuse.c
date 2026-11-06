/* Synthetic registers only. Include the real translation/renderer implementation
 * so existing line counters can prove CPU pixel work was skipped without a
 * production test API. External services below have no game/art/3D image. */
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
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
void NitroPort_Log(const char *fmt, ...) {}
int NitroPort_Unimplemented(const char *what) { fprintf(stderr, "unexpected: %s\n", what); abort(); }
const uint16_t *NitroPort_Gx3dAcquireImage(void) { return NULL; }
void NitroPort_Gx3dReleaseImage(const uint16_t *p) {}
const uint8_t *NitroPort_Gx3dImageAlpha(const uint16_t *p) { return NULL; }
int NitroPort_Gx3dImageTranslucent(const uint16_t *p) { return 0; }
uint32_t NitroPort_Gx3dImageCount(void) { return 0; }
int NitroPort_WideWindows(int e, int bg, NitroWideWindow *out, int max) { return 0; }
int NitroPort_WidePictures(void) { return 0; }
uint32_t NitroPort_WideWindowsSerial(void) { return 0; }
int NitroPort_WideSplits(int e, NitroWideSplit *out) { return 0; }
int NitroPort_WideParts(int e, NitroWidePart *out) { return 0; }
void NitroPort_WideLayout(int e, NitroRender2DSplit *split) {}
int NitroPort_WideArtCount(void) { return 0; }
uint32_t NitroPort_WideArtSerial(void) { return 0; }
const uint16_t *NitroPort_WideArtPicture(int p, int *w, int *h, int *dx, int *dy) { return NULL; }
const uint16_t *NitroPort_WideArtFindNear(uint64_t key, int ch, int cv, int ph, int pv,
                                       int mh, int mv, int *h, int *v, int *full) { return NULL; }
int NitroPort_WideArtExtent(const uint16_t *p, int *l, int *r) { return 0; }
const uint8_t *NitroPort_WideArtRows(const uint16_t *p) { return NULL; }
int NitroPort_WideArtMovedOffset(int m, int l, int r) { return m; }
int NitroPort_WideArtUnshared(void) { return 0; }
int NitroPort_WideArtToShare(uint64_t key, int i, const uint16_t **p, int *h, int *v,
                            int *full, char *name, size_t n) { return 0; }
int NitroPort_WideArtShareWrite(const char *name, const uint16_t *p) { return 0; }

static int failures, checks;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "%d: %s\n", __LINE__, #x); } } while (0)
static void wr16(unsigned off, u16 v) { memcpy(NitroPort_IoReg + off, &v, 2); }
static void wr32(unsigned off, u32 v) { memcpy(NitroPort_IoReg + off, &v, 4); }
static u16 under[320 * 192], over[320 * 192], refUnder[320 * 192], refOver[320 * 192];
static u8 alpha[320 * 192], refAlpha[320 * 192];
static NitroRender2DSplit layout(u16 *o, u8 *a)
{
    NitroRender2DSplit s = {0};
    s.width = 320; s.origin = 32; s.over = o; s.overAlpha = a;
    s.gpuBgs = 6; s.windowsToEdge = 1;
    return s;
}
static void reset(int two)
{
    memset(NitroPort_IoReg, 0, sizeof(NitroPort_IoReg));
    memset(NitroPort_Pltt, 0, sizeof(NitroPort_Pltt));
    memset(NitroPort_BgVram, 0, sizeof(NitroPort_BgVram));
    memset(NitroPort_Oam, 0, sizeof(NitroPort_Oam));
    memset(&NitroPort_LineRegs, 0, sizeof(NitroPort_LineRegs));
    wr16(0x304, 0x202); /* both engines powered */
    /* OBJ display enabled for the initial synthetic scene, with all OAM disabled.
     * SortObjs itself must establish that no sprites affect the CPU picture. */
    wr32(0, 0x10000 | 0x1000 | 0x108 | 0x200 | (two ? 0x400 : 0));
    wr16(8, 1); /* 3D priority */
    wr16(10, (1 << 8) | 2); /* BG1 map at 0x800, behind 3D */
    wr16(12, (2 << 8) | 3); /* BG2 map at 0x1000, behind 3D */
    for (unsigned i = 0; i < 128; i++) {
        u16 disabled = 0x200; memcpy(NitroPort_Oam + i * 8, &disabled, 2);
    }
    for (unsigned i = 0; i < 32; i++) NitroPort_BgVram[32 + i] = (i & 1) ? 0x21 : 0x12;
    for (unsigned i = 0; i < 2048; i += 2) {
        u16 tile = 1; memcpy(NitroPort_BgVram + 0x800 + i, &tile, 2);
        memcpy(NitroPort_BgVram + 0x1000 + i, &tile, 2);
    }
    ((u16 *)NitroPort_Pltt)[0] = 0x421;
    ((u16 *)NitroPort_Pltt)[1] = 31;
    ((u16 *)NitroPort_Pltt)[2] = 31 << 5;
    memset(under, 0x55, sizeof(under)); memset(over, 0x55, sizeof(over));
    memset(alpha, 0x55, sizeof(alpha));
    memset(refUnder, 0x55, sizeof(refUnder)); memset(refOver, 0x55, sizeof(refOver));
    memset(refAlpha, 0x55, sizeof(refAlpha));
}

static void scroll_case(int two, int h1, int v1, int h2, int v2, int bright)
{
    reset(two);
    NitroRender2DSplit initial = layout(over, alpha);
    NitroPort_Render2DSplit(0, under, &initial);
    CHECK(initial.nGpuQuads == (two ? 2 : 1));
    unsigned lines = sProfile[0].lines;
    wr16(0x14, h1); wr16(0x16, v1); wr16(0x18, h2); wr16(0x1A, v2);
    wr16(0x6C, bright);
    NitroRender2DSplit reused = layout(over, alpha);
    CHECK(NitroPort_Render2DSplitIfChanged(0, under, &reused) != 0);
    CHECK(reused.flatCpuPlanes == 1);
    CHECK(sProfile[0].lines == lines);
    /* Reference AFTER candidate, so it cannot change candidate cache history. */
    NitroRender2DSplit ref = layout(refOver, refAlpha);
    NitroPort_Render2DSplit(0, refUnder, &ref);
    CHECK(memcmp(under, refUnder, sizeof(under)) == 0);
    CHECK(memcmp(over, refOver, sizeof(over)) == 0);
    CHECK(memcmp(alpha, refAlpha, sizeof(alpha)) == 0);
    CHECK(reused.nGpuQuads == ref.nGpuQuads);
    CHECK(reused.gpuQuadsBehind == ref.gpuQuadsBehind);
    CHECK(memcmp(reused.gpuQuads, ref.gpuQuads, ref.nGpuQuads * sizeof(NitroGpuQuad)) == 0);
    CHECK(memcmp(reused.gpuBgKey, ref.gpuBgKey, sizeof(ref.gpuBgKey)) == 0);
    CHECK(reused.masterBright == ref.masterBright);
}

static void sequence_case(int snapshot, int mapSize)
{
    reset(1);
    wr16(10, (1 << 8) | 2 | mapSize);
    wr16(12, (2 << 8) | 3 | mapSize);
    wr16(0x14, 511); wr16(0x16, 511);
    wr16(0x18, 511); wr16(0x1A, 511);
    NitroPort_Render2DTakeState(0);
    NitroRender2DSplit initial = layout(over, alpha);
    initial.snapshot = snapshot;
    NitroPort_Render2DSplit(0, under, &initial);
    CHECK(initial.nGpuQuads == 2);
    for (int n = 0; n < 6; n++) {
        unsigned lines = sProfile[0].lines;
        wr16(0x14, n & 1 ? 511 : 0); wr16(0x16, n & 1 ? 511 : 0);
        wr16(0x18, n & 1 ? 511 : 0); wr16(0x1A, n & 1 ? 511 : 0);
        NitroPort_Render2DTakeState(0);
        NitroRender2DSplit candidate = layout(over, alpha);
        candidate.snapshot = snapshot;
        CHECK(NitroPort_Render2DSplitIfChanged(0, under, &candidate) == 1);
        CHECK(sProfile[0].lines == lines);
        float texel = mapSize == 0 ? 2.0f : 1.0f;
        for (int q = 0; q < candidate.nGpuQuads; q++) {
            const NitroGpuQuad *quad = &candidate.gpuQuads[q];
            float u = ((float)(n & 1 ? 511 : 0) - 32.0f) * texel / 512.0f;
            float v = (float)(n & 1 ? 511 : 0) * texel / 512.0f;
            CHECK(quad->u0 == u && quad->v0 == v);
            CHECK(quad->u1 == u + 320.0f * texel / 512.0f);
            CHECK(quad->v1 == v + 192.0f * texel / 512.0f);
            CHECK(quad->u2 == quad->u1 && quad->v2 == v);
        }
        candidate = layout(over, alpha); candidate.snapshot = snapshot;
        CHECK(NitroPort_Render2DSplitIfChanged(0, under, &candidate) == 1);
        CHECK(sProfile[0].lines == lines);
    }
    /* A fresh full render must produce the exact same canonical final UVs. */
    NitroRender2DSplit ref = layout(refOver, refAlpha); ref.snapshot = snapshot;
    NitroPort_Render2DSplit(0, refUnder, &ref);
    CHECK(memcmp(under, refUnder, sizeof(under)) == 0);
    CHECK(memcmp(over, refOver, sizeof(over)) == 0);
    CHECK(memcmp(alpha, refAlpha, sizeof(alpha)) == 0);
}

/* Scroll is placement, not texture identity. Direct writes must still change
 * the data key: these exercise real key construction with no writer hooks. */
static void data_scroll_key_case(int snapshot)
{
    reset(1);
    NitroPort_Render2DTakeState(0);
    NitroRender2DSplit first = layout(over, alpha);
    first.snapshot = snapshot;
    NitroPort_Render2DSplit(0, under, &first);
    CHECK(first.nGpuQuads == 2);
    wr16(0x14, 17); wr16(0x16, 19);
    wr16(0x18, 23); wr16(0x1a, 29);
    NitroPort_Render2DTakeState(0);
    NitroRender2DSplit moved = layout(over, alpha);
    moved.snapshot = snapshot;
    CHECK(NitroPort_Render2DSplitIfChanged(0, under, &moved) == 1);
    CHECK(moved.nGpuQuads == 2);
    CHECK(memcmp(first.gpuBgKey, moved.gpuBgKey, sizeof(first.gpuBgKey)) == 0);
    CHECK(memcmp(first.gpuQuads, moved.gpuQuads, 2 * sizeof(NitroGpuQuad)) != 0);

    NitroPort_BgVram[32] ^= 1;
    NitroPort_Render2DTakeState(0);
    NitroRender2DSplit tile = layout(over, alpha);
    tile.snapshot = snapshot;
    CHECK(NitroPort_Render2DSplitIfChanged(0, under, &tile) == 1);
    CHECK(tile.nGpuQuads == 2);
    CHECK(tile.gpuBgKey[0] != moved.gpuBgKey[0]);
    CHECK(tile.gpuBgKey[1] != moved.gpuBgKey[1]);
    CHECK(memcmp(tile.gpuQuads, moved.gpuQuads, 2 * sizeof(NitroGpuQuad)) == 0);

    ((u16 *)NitroPort_Pltt)[1] ^= 1;
    NitroPort_Render2DTakeState(0);
    NitroRender2DSplit palette = layout(over, alpha);
    palette.snapshot = snapshot;
    CHECK(NitroPort_Render2DSplitIfChanged(0, under, &palette) == 1);
    CHECK(palette.nGpuQuads == 2);
    CHECK(palette.gpuBgKey[0] != tile.gpuBgKey[0]);
    CHECK(palette.gpuBgKey[1] != tile.gpuBgKey[1]);
    CHECK(memcmp(palette.gpuQuads, tile.gpuQuads, 2 * sizeof(NitroGpuQuad)) == 0);
}

/* Conservative fallback must see writes that bypass any hypothetical hook.
 * These are signature coverage tests, not proof of complete writer inventory. */
static void vram_signature_case(void)
{
    static const u32 zero[2] = {0, 0};
    /* The old identical-lane seeding erased an incoming bit-31 difference
     * for EVERY eight-byte region, independent of its contents. */
    for (u32 seed = 0; seed < 32; seed++) {
        u32 h = seed * 0x9e3779b9u;
        CHECK(HashWords((const u8 *)zero, sizeof(zero), h) !=
              HashWords((const u8 *)zero, sizeof(zero), h ^ 0x80000000u));
    }
    struct Region { int engine; u8 *bytes; unsigned length; } regions[] = {
        {0, NitroPort_BgVram, 0x80000}, {0, NitroPort_ObjVram, 0x40000},
        {0, NitroPort_LcdcVram + 0x80000, 0x18000},
        {1, NitroPort_DbBgVram, 0x20000}, {1, NitroPort_DbObjVram, 0x20000},
        {1, NitroPort_LcdcVram + 0x98000, 0xc000},
    };
    reset(1);
    for (unsigned i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
        struct Region *r = &regions[i];
        const u8 *io = NitroPort_IoReg + (r->engine ? 0x1000 : 0);
        for (unsigned end = 0; end < 2; end++) {
            unsigned offset = end ? r->length - 1 : 0;
            u32 before = EngineVramSignature(r->engine, io, NitroPort_Oam);
            r->bytes[offset] ^= 1;
            if (EngineVramSignature(r->engine, io, NitroPort_Oam) == before)
                fprintf(stderr, "signature collision region=%u offset=%u\n", i, offset);
            CHECK(EngineVramSignature(r->engine, io, NitroPort_Oam) != before);
            r->bytes[offset] ^= 1;
            CHECK(EngineVramSignature(r->engine, io, NitroPort_Oam) == before);
        }
    }
    /* LCDC display mode consumes the selected 128-KiB bank as well. */
    for (unsigned bank = 0; bank < 4; bank++) {
        wr32(0, (2u << 16) | (bank << 18));
        u8 *p = NitroPort_LcdcVram + bank * 0x20000 + 0x1ffff;
        u32 before = EngineVramSignature(0, NitroPort_IoReg, NitroPort_Oam);
        *p ^= 1;
        CHECK(EngineVramSignature(0, NitroPort_IoReg, NitroPort_Oam) != before);
        *p ^= 1;
    }
}

/* A real lower-screen text pixel can depend on the last VRAM byte. The
 * fallback must request a redraw after a raw write, not just change a hash. */
static void lower_vram_end_pixel_case(void)
{
    reset(0);
    memset(NitroPort_DbBgVram, 0, sizeof(NitroPort_DbBgVram));
    wr32(0x1000, 0x10000 | 0x100); /* graphics mode, BG0 */
    wr16(0x1008, 7 << 2); /* character base 0x1c000, screen base 0 */
    wr16(0x1010, 6); wr16(0x1012, 7);
    u16 tile = 511;
    memcpy(NitroPort_DbBgVram, &tile, sizeof(tile));
    ((u16 *)(NitroPort_Pltt + 0x400))[1] = 0x7fff;
    NitroRender2DSplit initial = layout(over, alpha);
    initial.gpuBgs = 0;
    NitroPort_Render2DSplit(1, under, &initial);
    for (int i = 0; i < 3; i++) {
        NitroRender2DSplit stable = layout(over, alpha);
        stable.gpuBgs = 0;
        NitroPort_Render2DSplitIfChanged(1, under, &stable);
    }
    u16 before = under[32];
    NitroPort_DbBgVram[0x1ffff] ^= 1;
    NitroRender2DSplit changed = layout(over, alpha);
    changed.gpuBgs = 0;
    CHECK(NitroPort_Render2DSplitIfChanged(1, under, &changed) == 1);
    NitroRender2DSplit reference = layout(refOver, refAlpha);
    reference.gpuBgs = 0;
    NitroPort_Render2DSplit(1, refUnder, &reference);
    CHECK(refUnder[32] != before);
    CHECK(memcmp(under, refUnder, sizeof(under)) == 0);
}

enum Change { PALETTE, TILE, OAM, WINDOW, HBLANK, NOMINATION, BUFFER,
              BLANK_RETURN, LAYOUT, AFFINE, ENGINE_B, BACKDROP_EFFECT, FORCED_RENDER };
static void fallback_case(enum Change change)
{
    reset(1);
    NitroRender2DSplit initial = layout(over, alpha);
    /* Establish the freshly resolved flat path before every invalidation:
     * each change must be revalidated, never trusted from the previous frame. */
    CHECK(NitroPort_Render2DSplitIfChanged(0, under, &initial) == 1);
    unsigned lines = sProfile[0].lines;
    NitroRender2DSplit candidate = layout(over, alpha);
    int engine = 0;
    wr16(0x14, 7); wr16(0x18, 11);
    switch (change) {
    case PALETTE: ((u16 *)NitroPort_Pltt)[0] ^= 31; break;
    case TILE: NitroPort_BgVram[32] ^= 1; break;
    case OAM: wr32(0, Rd32(NitroPort_IoReg, 0) | 0x1000); ((u16 *)NitroPort_Oam)[0] = 0; break;
    case WINDOW: wr32(0, Rd32(NitroPort_IoReg, 0) | 0x2000); break;
    case HBLANK:
        NitroPort_LineRegs.active = 1;
        for (int y = 0; y < 192; y++) memcpy(NitroPort_LineRegs.regs[0][y], NitroPort_IoReg, NITRO_LINE_REGS);
        break;
    case NOMINATION: candidate.gpuBgs = 2; break;
    case BUFFER: candidate.over = refOver; candidate.overAlpha = refAlpha; break;
    case BLANK_RETURN:
        wr32(0, Rd32(NitroPort_IoReg, 0) | 0x80);
        NitroPort_Render2DSplit(0, under, &candidate);
        wr32(0, Rd32(NitroPort_IoReg, 0) & ~0x80u);
        candidate = layout(over, alpha);
        break;
    case LAYOUT: candidate.stretch = 1; break;
    case AFFINE: {
        /* DS mode 1 keeps BG2 text; mode 2 makes BG2 affine. */
        wr32(0, (Rd32(NitroPort_IoReg, 0) & ~7u) | 2);
        Engine e; SetUpEngine(&e, 0, &candidate);
        CHECK(BgKind(&e, 2, Rd32(NitroPort_IoReg, 0)) == BG_AFFINE);
        break;
    }
    case ENGINE_B:
        engine = 1;
        wr32(0x1000, 0x10000 | 0x200);
        break;
    case BACKDROP_EFFECT:
        wr16(0x50, 0x20 | 0x80); /* backdrop as brightness first target */
        wr16(0x54, 9);
        break;
    case FORCED_RENDER:
        candidate.gpuBgs = 0;
        NitroPort_Render2DSplit(0, under, &candidate);
        candidate = layout(over, alpha);
        break;
    }
    unsigned before = sProfile[engine].lines;
    u16 *pixels = change == BUFFER ? refUnder : under;
    CHECK(NitroPort_Render2DSplitIfChanged(engine, pixels, &candidate) == 1);
    /* R3 can resolve a GPU-only picture directly even after content/buffer
     * changes. These cases still require fresh output, not old cached pixels. */
    int flat = change == PALETTE || change == TILE || change == BUFFER ||
               change == BLANK_RETURN || change == FORCED_RENDER;
    if (!flat && sProfile[engine].lines == before) {
        fprintf(stderr, "fallback case %d unexpectedly omitted scanlines\n", change);
    }
    if (!flat) CHECK(candidate.flatCpuPlanes == 0);
    CHECK(flat ? sProfile[engine].lines == before : sProfile[engine].lines > before);
    if (engine == 0 && !flat) CHECK(sProfile[0].lines > lines);
    /* For all fallbacks, compare the defined pixels and metadata with a full
     * draw of exactly the same input, not merely the "did draw" counter. */
    static u16 expectedUnder[320 * 192], expectedOver[320 * 192];
    static u8 expectedAlpha[320 * 192];
    memset(expectedUnder, 0x55, sizeof(expectedUnder));
    memset(expectedOver, 0x55, sizeof(expectedOver));
    memset(expectedAlpha, 0x55, sizeof(expectedAlpha));
    NitroRender2DSplit ref = layout(expectedOver, expectedAlpha);
    ref.gpuBgs = candidate.gpuBgs; ref.stretch = candidate.stretch;
    NitroPort_Render2DSplit(engine, expectedUnder, &ref);
    CHECK(memcmp(pixels, expectedUnder, sizeof(expectedUnder)) == 0);
    CHECK(candidate.has3d == ref.has3d);
    if (candidate.has3d) {
        CHECK(memcmp(candidate.over, expectedOver, sizeof(expectedOver)) == 0);
    } /* Header contract: without 3D, over is not touched or consumed. */
    CHECK(candidate.nGpuQuads == ref.nGpuQuads);
    CHECK(memcmp(candidate.gpuQuads, ref.gpuQuads, ref.nGpuQuads * sizeof(NitroGpuQuad)) == 0);
}

static void flat_content_case(int change)
{
    reset(1);
    NitroRender2DSplit initial = layout(over, alpha);
    NitroPort_Render2DSplit(0, under, &initial);
    CHECK(initial.nGpuQuads == 2);
    unsigned lines = sProfile[0].lines;
    switch (change) {
    case 0: NitroPort_BgVram[0x70000] ^= 1; break; /* unrelated VRAM */
    case 1: NitroPort_BgVram[32] ^= 1; break; /* actual taken tile */
    case 2: ((u16 *)NitroPort_Pltt)[0] = 0x1234; break; /* CPU backdrop */
    case 3: ((u16 *)NitroPort_Pltt)[1] = 0x4321; break; /* GPU palette */
    case 4: ((u16 *)NitroPort_Oam)[2] = 17; break; /* still-disabled OBJ */
    }
    wr16(0x14, 43); wr16(0x18, 63);
    NitroRender2DSplit candidate = layout(over, alpha);
    CHECK(NitroPort_Render2DSplitIfChanged(0, under, &candidate) == 1);
    CHECK(candidate.flatCpuPlanes == 1);
    CHECK(sProfile[0].lines == lines);
    NitroRender2DSplit ref = layout(refOver, refAlpha);
    NitroPort_Render2DSplit(0, refUnder, &ref);
    CHECK(memcmp(under, refUnder, sizeof(under)) == 0);
    CHECK(memcmp(over, refOver, sizeof(over)) == 0);
    CHECK(memcmp(alpha, refAlpha, sizeof(alpha)) == 0);
    CHECK(candidate.nGpuQuads == ref.nGpuQuads);
    CHECK(memcmp(candidate.gpuQuads, ref.gpuQuads, ref.nGpuQuads * sizeof(NitroGpuQuad)) == 0);
    CHECK(memcmp(candidate.gpuBgKey, ref.gpuBgKey, sizeof(ref.gpuBgKey)) == 0);
    CHECK(candidate.backdrop == ref.backdrop);
}

static void disabled_obj_transition_case(int mode, int snapshot)
{
    reset(1);
    /* A real preceding OBJ-enabled frame populates the persistent ObjFrame.
     * Use both an ordinary sprite and an OBJ-window sprite: the latter isn't
     * a GPU quad, but must not block the next scene's zero-sprite admission. */
    ((u16 *)NitroPort_Oam)[0] = (u16)(mode << 10);
    memset(NitroPort_ObjVram, 0x11, 32);
    ((u16 *)NitroPort_Pltt)[0x100 + 1] = 0x7C00;
    NitroPort_Render2DTakeState(0);
    NitroRender2DSplit prior = layout(over, alpha);
    prior.snapshot = snapshot;
    prior.gpuBgs = 0;
    NitroPort_Render2DSplit(0, under, &prior);
    CHECK(sWork[0].objFrame.count == 1);

    /* Field mode disables OBJ display. OAM deliberately remains populated:
     * the display-enable bit, not clearing OAM, controls sprite visibility. */
    wr32(0, Rd32(NitroPort_IoReg, 0) & ~0x1000u);
    NitroPort_Render2DTakeState(0);
    NitroRender2DSplit field = layout(over, alpha);
    field.snapshot = snapshot;
    NitroPort_Render2DSplit(0, under, &field);
    CHECK(sWork[0].objFrame.count == 0);
    CHECK(field.nGpuQuads == 2);
    CHECK(field.flatCpuPlanes == 0); /* forced draw never claims the proof */
    CHECK(field.gpuObjAtlasKey == 0);
    for (int q = 0; q < field.nGpuQuads; q++) CHECK(field.gpuQuads[q].kind == 0);

    unsigned lines = sProfile[0].lines;
    NitroPort_BgVram[0x70000] ^= 1;
    wr16(0x14, 47); wr16(0x18, 71);
    NitroPort_Render2DTakeState(0);
    field = layout(over, alpha);
    field.snapshot = snapshot;
    CHECK(NitroPort_Render2DSplitIfChanged(0, under, &field) == 1);
    CHECK(sProfile[0].lines == lines);
    CHECK(field.nGpuQuads == 2);
    CHECK(field.gpuObjAtlasKey == 0);
    /* Empty over pixels leave alpha untouched; give both calls the same
     * history, including alpha left by the preceding visible sprite. */
    memcpy(refAlpha, alpha, sizeof(alpha));
    NitroRender2DSplit ref = layout(refOver, refAlpha);
    ref.snapshot = snapshot;
    NitroPort_Render2DSplit(0, refUnder, &ref);
    CHECK(memcmp(under, refUnder, sizeof(under)) == 0);
    CHECK(memcmp(over, refOver, sizeof(over)) == 0);
    CHECK(memcmp(alpha, refAlpha, sizeof(alpha)) == 0);
    CHECK(field.nGpuQuads == ref.nGpuQuads);
    CHECK(memcmp(field.gpuBgKey, ref.gpuBgKey, sizeof(ref.gpuBgKey)) == 0);
    CHECK(memcmp(field.gpuQuads, ref.gpuQuads, ref.nGpuQuads * sizeof(NitroGpuQuad)) == 0);

    /* Enabling OBJ again must sort current OAM, not keep the empty frame. */
    wr32(0, Rd32(NitroPort_IoReg, 0) | 0x1000u);
    NitroPort_Render2DTakeState(0);
    field = layout(over, alpha);
    field.snapshot = snapshot;
    lines = sProfile[0].lines;
    CHECK(NitroPort_Render2DSplitIfChanged(0, under, &field) == 1);
    CHECK(sWork[0].objFrame.count == 1);
    CHECK(sProfile[0].lines > lines);
    /* Compare re-enabled sprites with a full render as well. */
    memcpy(refAlpha, alpha, sizeof(alpha));
    ref = layout(refOver, refAlpha);
    ref.snapshot = snapshot;
    NitroPort_Render2DSplit(0, refUnder, &ref);
    CHECK(memcmp(under, refUnder, sizeof(under)) == 0);
    CHECK(memcmp(over, refOver, sizeof(over)) == 0);
    CHECK(memcmp(alpha, refAlpha, sizeof(alpha)) == 0);
    CHECK(field.nGpuQuads == ref.nGpuQuads);
    CHECK(memcmp(field.gpuQuads, ref.gpuQuads, ref.nGpuQuads * sizeof(NitroGpuQuad)) == 0);
}

static void empty_front_bg_case(int snapshot, int bpp8, int size)
{
    reset(1);
    wr32(0, Rd32(NitroPort_IoReg, 0) | 0x800u);
    /* Goldenrod's topology: BG1/BG2 behind 3D, BG3 in front, only two
     * GPU picture slots. Its separate font map/tiles initially draw nothing. */
    wr16(14, (3 << 8) | (2 << 2) | size | (bpp8 ? 0x80 : 0));
    /* Larger maps must not overlap the two nonempty field maps. */
    memset(NitroPort_BgVram + 0x1800, 0, 0x2000);
    for (int phase = 0; phase < 7; phase++) {
        if (phase == 1) NitroPort_BgVram[0x1800] = 1; /* still a zero tile */
        if (phase == 2) NitroPort_BgVram[0x8000 + (bpp8 ? 64 : 32)] = 0x11;
        if (phase == 3) NitroPort_BgVram[0x8000 + (bpp8 ? 64 : 32)] = 0;
        if (phase == 4) NitroPort_BgVram[0x1800] = 0;
        if (phase == 5) NitroPort_BgVram[0x8000] = 0x11; /* tile zero is NOT inherently empty */
        if (phase == 6) NitroPort_BgVram[0x8000] = 0;
        wr16(0x14, phase * 7); wr16(0x18, phase * 11);
        NitroPort_Render2DTakeState(0);
        NitroRender2DSplit field = layout(over, alpha);
        field.snapshot = snapshot;
        field.anchorWindows = 1;
        for (int b = 0; b < 4; b++) field.clip[b] = 1;
        memcpy(refAlpha, alpha, sizeof(alpha));
        unsigned lines = sProfile[0].lines;
        CHECK(NitroPort_Render2DSplitIfChanged(0, under, &field) == 1);
        CHECK(field.nGpuQuads == 2);
        CHECK(phase == 2 || phase == 5 ? sProfile[0].lines > lines : sProfile[0].lines == lines);
        NitroRender2DSplit ref = layout(refOver, refAlpha);
        ref.snapshot = snapshot;
        ref.anchorWindows = 1;
        for (int b = 0; b < 4; b++) ref.clip[b] = 1;
        NitroPort_Render2DSplit(0, refUnder, &ref);
        CHECK(memcmp(under, refUnder, sizeof(under)) == 0);
        CHECK(memcmp(over, refOver, sizeof(over)) == 0);
        CHECK(memcmp(alpha, refAlpha, sizeof(alpha)) == 0);
        CHECK(field.nGpuQuads == ref.nGpuQuads);
        CHECK(memcmp(field.gpuQuads, ref.gpuQuads, ref.nGpuQuads * sizeof(NitroGpuQuad)) == 0);
    }
}

static void flat_no_full_vram_hash_case(void)
{
    reset(1);
    NitroRender2DSplit field = layout(over, alpha);
    CHECK(NitroPort_Render2DSplitIfChanged(0, under, &field) == 1);
    unsigned lines = sProfile[0].lines;
    u64 key = field.gpuBgKey[0];
    /* Active maps/tiles remain readable. A full EngineVramSignature or
     * RememberSplit hash would touch this unused page and fault. This tests
     * actual reads, not a source-string assertion or a production counter. */
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uintptr_t address = ((uintptr_t)(NitroPort_BgVram + 0x60000) + page - 1) & ~(uintptr_t)(page - 1);
    CHECK(address + page <= (uintptr_t)(NitroPort_BgVram + sizeof(NitroPort_BgVram)));
    CHECK(mprotect((void *)address, page, PROT_NONE) == 0);
    NitroPort_BgVram[32] ^= 1; /* GPU picture content must still refresh */
    wr16(0x14, 13); wr16(0x18, 29);
    field = layout(over, alpha);
    CHECK(NitroPort_Render2DSplitIfChanged(0, under, &field) == 1);
    CHECK(sProfile[0].lines == lines);
    CHECK(field.gpuBgKey[0] != key);
    CHECK(mprotect((void *)address, page, PROT_READ | PROT_WRITE) == 0);
    NitroRender2DSplit ref = layout(refOver, refAlpha);
    NitroPort_Render2DSplit(0, refUnder, &ref);
    CHECK(memcmp(under, refUnder, sizeof(under)) == 0);
    CHECK(memcmp(over, refOver, sizeof(over)) == 0);
    CHECK(field.nGpuQuads == ref.nGpuQuads);
    CHECK(memcmp(field.gpuQuads, ref.gpuQuads, ref.nGpuQuads * sizeof(NitroGpuQuad)) == 0);
    CHECK(memcmp(field.gpuBgKey, ref.gpuBgKey, sizeof(ref.gpuBgKey)) == 0);
}

static void load_capture(const char *dir, const char *name, void *out, size_t n)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s.bin", dir, name);
    FILE *f = fopen(path, "rb");
    if (f == NULL || fread(out, 1, n, f) != n) {
        fprintf(stderr, "cannot read %s\n", path); exit(2);
    }
    fclose(f);
}

static void captured_field_case(const char *dir)
{
    reset(1);
    load_capture(dir, "io", NitroPort_IoReg, sizeof(NitroPort_IoReg));
    load_capture(dir, "bg", NitroPort_BgVram, sizeof(NitroPort_BgVram));
    load_capture(dir, "obj", NitroPort_ObjVram, sizeof(NitroPort_ObjVram));
    load_capture(dir, "oam", NitroPort_Oam, sizeof(NitroPort_Oam));
    load_capture(dir, "pltt", NitroPort_Pltt, sizeof(NitroPort_Pltt));
    load_capture(dir, "lcdc", NitroPort_LcdcVram, sizeof(NitroPort_LcdcVram));
    NitroPort_Render2DTakeState(0);
    NitroRender2DSplit field = layout(over, alpha);
    field.snapshot = 1; field.gpuBgs = 14; field.anchorWindows = 1;
    for (int b = 0; b < 4; b++) field.clip[b] = 1;
    unsigned lines = sProfile[0].lines;
    CHECK(NitroPort_Render2DSplitIfChanged(0, under, &field) == 1);
    CHECK(field.nGpuQuads == 2);
    CHECK(sProfile[0].lines == lines);
    NitroRender2DSplit ref = layout(refOver, refAlpha);
    ref.snapshot = 1; ref.gpuBgs = 14; ref.anchorWindows = 1;
    for (int b = 0; b < 4; b++) ref.clip[b] = 1;
    NitroPort_Render2DSplit(0, refUnder, &ref);
    CHECK(sProfile[0].lines == lines + 192);
    CHECK(memcmp(under, refUnder, sizeof(under)) == 0);
    CHECK(memcmp(over, refOver, sizeof(over)) == 0);
    CHECK(memcmp(alpha, refAlpha, sizeof(alpha)) == 0);
    CHECK(field.nGpuQuads == ref.nGpuQuads);
    CHECK(memcmp(field.gpuQuads, ref.gpuQuads, ref.nGpuQuads * sizeof(NitroGpuQuad)) == 0);
    CHECK(memcmp(field.gpuBgKey, ref.gpuBgKey, sizeof(ref.gpuBgKey)) == 0);
}

static void front_band_case(int patterned)
{
    static u16 picture[256 * 256], reconstructed[320 * 192];
    reset(0);
    wr16(10, (1 << 8)); /* BG1 in front of priority-1 3D */
    memset(NitroPort_BgVram + 32, 0x11, 32);
    if (patterned) {
        memset(NitroPort_BgVram + 64, 0x22, 32);
        for (int ty = 0; ty < 32; ty++) for (int tx = 0; tx < 32; tx++) {
            u16 tile = (ty / 4) % 3; /* transparent, red, green bands */
            memcpy(NitroPort_BgVram + 0x800 + (ty * 32 + tx) * 2, &tile, 2);
        }
        wr16(0x14, 7); wr16(0x16, 13);
    }
    NitroPort_Render2DTakeState(0);
    NitroRender2DSplit ref = layout(refOver, refAlpha);
    ref.snapshot = 1; ref.gpuBgs = 0; ref.clip[1] = 1;
    NitroPort_Render2DSplit(0, refUnder, &ref);
    NitroRender2DSplit taken = layout(over, alpha);
    taken.snapshot = 1; taken.clip[1] = 1;
    NitroPort_Render2DSplit(0, under, &taken);
    CHECK(taken.nGpuQuads >= 3);
    CHECK(taken.gpuQuadsBehind == 0);
    CHECK(NitroPort_Render2DBgPicture(1, 1, picture));
    memset(reconstructed, 0, sizeof(reconstructed));
    for (int i = 0; i < taken.nGpuQuads; i++) {
        NitroGpuQuad *q = &taken.gpuQuads[i];
        for (int y = q->y0; y < q->y1; y++) for (int x = q->x0; x < q->x1; x++) {
            u16 c;
            if (q->kind == 2) {
                c = (u16)(q->pad[0] | ((u16)q->pad[1] << 8));
            } else {
                int sx = (int)(q->u0 * 256.0f) + x - q->x0;
                int sy = (int)(q->v0 * 256.0f) + y - q->y0;
                u16 p = picture[(sy & 255) * 256 + (sx & 255)];
                c = p & 1 ? (u16)(0x8000 | ((p >> 11) & 31) | (((p >> 6) & 31) << 5) |
                                  (((p >> 1) & 31) << 10)) : 0;
            }
            reconstructed[y * 320 + x] = c;
        }
    }
    CHECK(memcmp(reconstructed, refOver, sizeof(reconstructed)) == 0);
}

static void front_band_capacity_case(void)
{
    reset(0);
    wr16(10, (1 << 8));
    /* Alternating solid rows require more runs than fit in the bounded quad
     * list; the entire front region must remain on the software path. */
    for (int y = 0; y < 8; y++) {
        memset(NitroPort_BgVram + 32 + y * 4, y & 1 ? 0x22 : 0x11, 4);
    }
    NitroRender2DSplit candidate = layout(over, alpha);
    candidate.clip[1] = 1;
    NitroPort_Render2DSplit(0, under, &candidate);
    NitroRender2DSplit ref = layout(refOver, refAlpha);
    ref.clip[1] = 1; ref.gpuBgs = 0;
    NitroPort_Render2DSplit(0, refUnder, &ref);
    CHECK(candidate.nGpuQuads == 0);
    CHECK(memcmp(over, refOver, sizeof(over)) == 0);
}

static void text_row_width_case(void)
{
    static const int rows[] = {0, 1, 7, 8, 127, 255, 256, 511};
    reset(0);
    /* Deterministic nontrivial maps, tile flips and both palette formats. */
    for (unsigned i = 0; i < sizeof(NitroPort_DbBgVram); i++)
        NitroPort_DbBgVram[i] = (u8)((i * 37u + (i >> 3) * 11u + 19u) & 255u);
    for (unsigned i = 0; i < 0x400; i++)
        NitroPort_Pltt[0x400 + i] = (u8)((i * 29u + 7u) & 255u);
    wr32(0x1000, 0x10000 | 0x100);
    for (int snapshot = 0; snapshot < 2; snapshot++) {
        NitroPort_Render2DTakeState(1);
        NitroRender2DSplit s = layout(over, alpha);
        s.snapshot = snapshot;
        Engine e;
        SetUpEngine(&e, 1, &s);
        u32 dispcnt = Rd32(e.io, 0);
        for (int size = 0; size < 4; size++) {
            for (int bpp = 0; bpp < 2; bpp++) {
                u16 cnt = (u16)((size << 14) | (bpp << 7) | (7 << 8) | (2 << 2));
                for (unsigned r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
                    int ty = rows[r] & (sTextSizes[size][1] - 1);
                    u64 key = 0x98760000u + (u64)(snapshot * 0x100 + size * 0x20 + bpp * 0x10 + r);
                    const u16 *cached = TextRowCached(&e, 1, 0, key, ty, cnt, dispcnt);
                    CHECK(cached != NULL);
                    if (!cached) continue;
                    for (int x = 0; x < 512; x++)
                        CHECK(cached[x] == TextBgPixel(&e, 0, cnt, dispcnt, x, ty));
                    if (sTextSizes[size][0] == 256)
                        CHECK(memcmp(cached, cached + 256, 256 * sizeof(*cached)) == 0);
                    CHECK(TextRowCached(&e, 1, 0, key, ty, cnt, dispcnt) == cached);
                }
            }
        }
    }
}

int main(int argc, char **argv)
{
    vram_signature_case();
    lower_vram_end_pixel_case();
    data_scroll_key_case(0);
    data_scroll_key_case(1);
    scroll_case(0, 1, 3, 0, 0, 0);
    scroll_case(1, 0, 0, 7, 11, 0);
    scroll_case(1, 13, 21, 31, 41, 0);
    scroll_case(1, 511, 511, 511, 511, 0);
    scroll_case(1, 4, 9, 3, 6, 0x4008);
    for (int snapshot = 0; snapshot < 2; snapshot++) {
        sequence_case(snapshot, 0);
        sequence_case(snapshot, 0x4000);
        sequence_case(snapshot, 0x8000);
        sequence_case(snapshot, 0xC000);
    }
    for (int c = PALETTE; c <= FORCED_RENDER; c++) fallback_case((enum Change)c);
    for (int c = 0; c < 5; c++) flat_content_case(c);
    for (int snapshot = 0; snapshot < 2; snapshot++) {
        disabled_obj_transition_case(0, snapshot);
        disabled_obj_transition_case(1, snapshot);
        disabled_obj_transition_case(2, snapshot);
        for (int bpp8 = 0; bpp8 < 2; bpp8++) {
            empty_front_bg_case(snapshot, bpp8, 0);
            empty_front_bg_case(snapshot, bpp8, 0xC000);
        }
    }
    flat_no_full_vram_hash_case();
    front_band_case(0);
    front_band_case(1);
    front_band_capacity_case();
    text_row_width_case();
    if (argc == 2) captured_field_case(argv[1]);
    printf("gpu-scroll: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
