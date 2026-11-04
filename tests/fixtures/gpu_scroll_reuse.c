/* Synthetic registers only. Include the real translation/renderer implementation
 * so existing line counters can prove CPU pixel work was skipped without a
 * production test API. External services below have no game/art/3D image. */
#include <stdio.h>
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
    /* OBJ display enabled, as on the field, but all 128 OAM entries disabled.
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
        CHECK(NitroPort_Render2DSplitIfChanged(0, under, &candidate) == 0);
        CHECK(sProfile[0].lines == lines);
    }
    /* A fresh full render must produce the exact same canonical final UVs. */
    NitroRender2DSplit ref = layout(refOver, refAlpha); ref.snapshot = snapshot;
    NitroPort_Render2DSplit(0, refUnder, &ref);
    CHECK(memcmp(under, refUnder, sizeof(under)) == 0);
    CHECK(memcmp(over, refOver, sizeof(over)) == 0);
    CHECK(memcmp(alpha, refAlpha, sizeof(alpha)) == 0);
}

enum Change { PALETTE, TILE, OAM, WINDOW, HBLANK, NOMINATION, BUFFER,
              BLANK_RETURN, LAYOUT, AFFINE, ENGINE_B, FORCED_RENDER };
static void fallback_case(enum Change change)
{
    reset(1);
    NitroRender2DSplit initial = layout(over, alpha);
    NitroPort_Render2DSplit(0, under, &initial);
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
    case AFFINE: wr32(0, Rd32(NitroPort_IoReg, 0) | 1); break;
    case ENGINE_B:
        engine = 1;
        wr32(0x1000, 0x10000 | 0x200);
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
    CHECK(sProfile[engine].lines > before);
    if (engine == 0) CHECK(sProfile[0].lines > lines);
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

int main(void)
{
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
    printf("gpu-scroll: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
