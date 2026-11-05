/* Exercise actual fixed-point geometry math without the host32 game ABI. */
#include <stdio.h>
#include <time.h>
#include GX3D_SOURCE

unsigned char NitroPort_IoReg[0x2000] __attribute__((aligned(8)));
int NitroPort_Gx3dTriangleTranslucent(const NitroGx3dTriangle *tri)
{
    return ((tri->polygon_attr >> 16) & 31u) < 31u;
}
static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "%d: %s\n", __LINE__, #x); } } while (0)
static uint32_t rng = 0x31415926;
static int32_t value(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (int32_t)(rng & 0x1FFFFFu) - 0x100000;
}
static int32_t reference_saturate(int64_t n)
{
    return n > INT32_MAX ? INT32_MAX : n < INT32_MIN ? INT32_MIN : (int32_t)n;
}
static void reference(Matrix *out, const Matrix *a, const Matrix *b)
{
    Matrix r;
    for (int row = 0; row < 4; row++) for (int col = 0; col < 4; col++) {
        int64_t n = 0;
        for (int k = 0; k < 4; k++) n += (int64_t)a->a[row*4+k] * b->a[k*4+col];
        r.a[row*4+col] = reference_saturate(n >> 12);
    }
    *out = r;
}
static void invalidation_cases(void)
{
    static const unsigned ops[] = {0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c};
    uint32_t p[16] = {0};
    p[0] = p[5] = p[10] = p[15] = 4096;
    for (int mode = 0; mode < 4; mode++) for (unsigned n = 0; n < sizeof(ops)/sizeof(ops[0]); n++) {
        unsigned op = ops[n];
        GeoState g = {0};
        identity(&g.pos); identity(&g.proj); identity(&g.vec); identity(&g.tex);
        identity(&g.pjStack); identity(&g.texStack);
        identity(&g.posStack[0]); identity(&g.vecStack[0]);
        g.mode = mode; g.wideNum = g.wideDen = g.tallNum = g.tallDen = 1;
        g.clipDirty = 1;
        ensure_clip(&g);
        Matrix before = g.clip, expected;
        /* Use valid stack operations; pop to level zero. */
        g.pjLevel = g.texLevel = g.pvLevel = (op == 0x12);
        uint32_t old = p[0];
        if (op >= 0x11 && op <= 0x14) p[0] = (mode == 1 || mode == 2) && op == 0x12 ? 1 : 0;
        matrix_command(&g, op, p);
        int mutates = mode != 3 && op != 0x11 && op != 0x13;
        CHECK(g.clipDirty == mutates);
        CHECK(memcmp(&g.clip, &before, sizeof(before)) == 0); /* still lazy */
        reference(&expected, &g.pos, &g.proj);
        ensure_clip(&g);
        CHECK(memcmp(&g.clip, &expected, sizeof(expected)) == 0);
        /* Non-mutating commands must never clear a previous real mutation. */
        g.clipDirty = 1;
        matrix_command(&g, op, p);
        CHECK(g.clipDirty == 1);
        p[0] = old;
    }
}

/* A caller's previous facing value must not affect a stage. This includes
 * early camera/outside exits, clipping, both winding directions, all cull
 * masks and shadow capture. Only the unobserved prior-facing global itself
 * may remain different on an early exit. */
static void stage_prior_facing_cases(void)
{
    struct Result {
        GeoVertex v[3];
        NitroGx3dTriangle staged[16];
        NitroGx3dTriangle shadow[16];
        __typeof__(sDiag) diag;
        unsigned count, vertices, clipped, shadows;
    } first, second;
    unsigned shadowCases = 0;
    for (int n = 0; n < 10000; n++) {
        GeoVertex input[3] = {{0}};
        for (int v = 0; v < 3; v++) {
            for (int k = 0; k < 3; k++) input[v].clip[k] = value() / 128;
            input[v].clip[3] = (n % 11 == 0 && v == 0) ? 0 : 4096;
            if (n % 53 == 0 && v == 0) memset(input[v].clip, 0, sizeof(input[v].clip));
            input[v].outside = (uint8_t)outcode(input[v].clip);
            input[v].lit = 1;
            input[v].color = (uint16_t)(value() & 0x7fff);
            input[v].s = value(); input[v].t = value();
        }
        memset(&s, 0, sizeof(s));
        s.attr = (uint32_t)((n & 3) << 6) | (31u << 16);
        s.viewport[2] = 255; s.viewport[3] = 191;
        sShadowCapture = (n >> 2) & 1;
        sDest = &sScenes[0];
        struct Result *result[2] = {&first, &second};
        for (int trial = 0; trial < 2; trial++) {
            struct Result *r = result[trial];
            memset(r, 0, sizeof(*r)); memcpy(r->v, input, sizeof(input));
            sDest->shadow_count = 0; sDest->shadow_triangles = r->shadow;
            memset(&sDiag, 0, sizeof(sDiag)); memset(sStaged, 0, sizeof(sStaged));
            sStagedCount = sStagedVertices = sStagedClipped = 0;
            sPolygonFront = trial;
            stage_triangle(&r->v[0], &r->v[1], &r->v[2]);
            memcpy(r->staged, sStaged, sizeof(sStaged));
            r->diag = sDiag; r->count = sStagedCount; r->vertices = sStagedVertices;
            r->clipped = sStagedClipped; r->shadows = sDest->shadow_count;
        }
        shadowCases += first.shadows != 0;
        CHECK(memcmp(&first, &second, sizeof(first)) == 0);
    }
    CHECK(shadowCases > 0);
    sDest->shadow_triangles = NULL; sDest = NULL; sShadowCapture = 0;
}

static uint64_t trace_bytes(uint64_t h, const void *data, size_t size)
{
    const uint8_t *p = data;
    while (size--) h = (h ^ *p++) * UINT64_C(1099511628211);
    return h;
}

/* Actual submit path, all primitive kinds, clipping and interpolation records.
 * Its digest is compared with the pre-optimization source by the Python test. */
static uint64_t submit_trace(void)
{
    static PolyRecord records[MAX_RECORDS];
    static NitroGx3dTriangle shadow[NITRO_GX3D_MAX_SHADOW_TRIANGLES];
    uint64_t h = UINT64_C(1469598103934665603);
    rng = 0x87543121;
    for (int primitive = 0; primitive < 4; primitive++) for (int mode = 0; mode < 4; mode++) {
        memset(&s, 0, sizeof(s)); memset(&sDiag, 0, sizeof(sDiag));
        memset(records, 0, sizeof(records)); memset(shadow, 0, sizeof(shadow));
        memset(&sScenes[0], 0, sizeof(sScenes[0]));
        sDest = &sScenes[0]; sDest->shadow_triangles = shadow;
        sShadowCapture = 1; sInterp = 1; sReplaying = 0;
        sRec[0] = records; sRecBuild = 0; sRecCount[0] = 0;
        identity(&s.pos); identity(&s.proj); identity(&s.vec);
        s.wideNum = s.wideDen = s.tallNum = s.tallDen = 1; s.clipDirty = 1;
        s.viewport[2] = 255; s.viewport[3] = 191;
        s.inBegin = 1; s.primitive = primitive;
        s.attr = (uint32_t)(mode << 6) | (31u << 16); s.rgb = 0x739c;
        for (int i = 0; i < 96; i++) {
            s.coord[0] = value() / 128; s.coord[1] = value() / 128; s.coord[2] = value() / 256;
            submit_vertex();
        }
        h = trace_bytes(h, &sDest->count, sizeof(sDest->count));
        h = trace_bytes(h, sDest->triangles, sDest->count * sizeof(sDest->triangles[0]));
        h = trace_bytes(h, &sDest->shadow_count, sizeof(sDest->shadow_count));
        h = trace_bytes(h, shadow, sDest->shadow_count * sizeof(shadow[0]));
        h = trace_bytes(h, &sRecCount[0], sizeof(sRecCount[0]));
        h = trace_bytes(h, records, sRecCount[0] * sizeof(records[0]));
        h = trace_bytes(h, &sDiag, sizeof(sDiag));
    }
    sDest = NULL; sShadowCapture = 0; sInterp = 0; sRec[0] = NULL;
    return h;
}

static void hidden_list_profile_cases(void)
{
    uint32_t words[4] = {8192u, 0, 0, 0}; /* one vertex followed by END */
    StreamCursor c;
    memset(&s, 0, sizeof(s)); memset(&sHiddenDiag, 0, sizeof(sHiddenDiag));
    identity(&s.xf); s.xfTrans[3] = (int64_t)4096 * 4096;
    s.primitive = 0;
    memset(&c, 0, sizeof(c)); c.c.w = words; c.c.count = 2; c.c.word = 0x4123;
    CHECK(skip_hidden_vertices(&c) == 1);
    CHECK(sHiddenDiag.accepted == 1 && sHiddenDiag.vertices == 1 && sHiddenDiag.polygons == 0);

    words[0] = 0; memset(&c, 0, sizeof(c)); c.c.w = words; c.c.count = 2; c.c.word = 0x4123;
    CHECK(skip_hidden_vertices(&c) == 0 && sHiddenDiag.intersects == 1);
    s.texparam = 2u << 30;
    CHECK(skip_hidden_vertices(&c) == 0 && sHiddenDiag.mode == 1);
    s.texparam = 0;
    memset(&c, 0, sizeof(c)); c.c.w = words; c.c.count = 2; c.c.word = 0x29;
    CHECK(skip_hidden_vertices(&c) == 0 && sHiddenDiag.unsupported == 1);
    memset(&c, 0, sizeof(c)); c.c.w = words; c.c.count = 2; c.c.word = 0x41;
    CHECK(skip_hidden_vertices(&c) == 0 && sHiddenDiag.empty == 1);
    memset(&c, 0, sizeof(c)); c.c.w = words; c.end = 1; sRing[0] = 0;
    CHECK(skip_hidden_vertices(&c) == 0 && sHiddenDiag.incomplete == 1);
    s.xfTrans[0] = (int64_t)1 << 42;
    memset(&c, 0, sizeof(c)); c.c.w = words; c.c.count = 2; c.c.word = 0x4123;
    CHECK(skip_hidden_vertices(&c) == 0 && sHiddenDiag.saturated == 1);
    /* Both vertices are beyond +X but lie on opposite sides of the camera. */
    s.xfTrans[0] = s.xfTrans[3] = 0; s.xf.a[7] = 4096;
    words[0] = 8192u | ((uint32_t)(uint16_t)-4096 << 16); words[1] = 0;
    words[2] = 8192u | (4096u << 16); words[3] = 0;
    memset(&c, 0, sizeof(c)); c.c.w = words; c.c.count = 4; c.c.word = 0x412323;
    CHECK(skip_hidden_vertices(&c) == 0 && sHiddenDiag.mixed == 1);
    CHECK(sHiddenDiag.attempts == sHiddenDiag.accepted + sHiddenDiag.mode + sHiddenDiag.incomplete +
          sHiddenDiag.unsupported + sHiddenDiag.empty + sHiddenDiag.saturated + sHiddenDiag.intersects +
          sHiddenDiag.mixed + sHiddenDiag.shadow);
}

static void hidden_list_shadow_case(void)
{
    static NitroGx3dTriangle shadow[16];
    const int xy[3][2] = {{5000, -1000}, {7000, -1000}, {6000, 1000}};
    uint32_t words[6];
    GeoVertex v[3] = {{0}};
    StreamCursor c = {0};
    memset(&s, 0, sizeof(s)); memset(&sDiag, 0, sizeof(sDiag));
    memset(shadow, 0, sizeof(shadow));
    identity(&s.xf); s.xfTrans[3] = (int64_t)4096 * 4096;
    s.attr = (3u << 6) | (31u << 16); s.viewport[2] = 255; s.viewport[3] = 191;
    sDest = &sScenes[0]; sDest->shadow_triangles = shadow; sDest->shadow_count = 0;
    sShadowCapture = 1;
    for (int i = 0; i < 3; i++) {
        words[i * 2] = (uint16_t)xy[i][0] | ((uint32_t)(uint16_t)xy[i][1] << 16);
        words[i * 2 + 1] = 0;
        v[i].clip[0] = xy[i][0]; v[i].clip[1] = xy[i][1]; v[i].clip[3] = 4096;
        v[i].outside = (uint8_t)outcode(v[i].clip); v[i].lit = 1;
    }
    CHECK(!polygon_hidden(v[0].outside | v[1].outside | v[2].outside,
                          v[0].outside & v[1].outside & v[2].outside));
    stage_triangle(&v[0], &v[1], &v[2]);
    CHECK(sDest->shadow_count == 1); /* visible to the existing shadow path */
    c.c.w = words; c.c.count = 6; c.c.word = 0x41232323;
    CHECK(skip_hidden_vertices(&c) == 0); /* must retain those commands */
    CHECK(sHiddenDiag.shadow == 1);
    for (int sign = -1; sign <= 1; sign += 2) {
        for (int i = 0; i < 3; i++) {
            words[i * 2] = 0; words[i * 2 + 1] = (uint16_t)(sign * 8192);
        }
        memset(&c, 0, sizeof(c)); c.c.w = words; c.c.count = 6; c.c.word = 0x41232323;
        CHECK(skip_hidden_vertices(&c) == 1); /* wholly beyond near/far stays rejectable */
    }
    sShadowCapture = 0; sDest->shadow_triangles = NULL; sDest = NULL;
}

static double benchmark(void (*fn)(Matrix *, const Matrix *, const Matrix *),
                        Matrix *a, const Matrix *p, uint32_t *digest)
{
    void (*volatile call)(Matrix *, const Matrix *, const Matrix *) = fn;
    Matrix out;
    struct timespec start, end;
    uint32_t h = 0;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (unsigned i = 0; i < 500000; i++) {
        a->a[12] = (int32_t)(i & 0xFFFF);
        call(&out, a, p);
        h += (uint32_t)out.a[i & 15];
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    *digest = h;
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}
int main(int argc, char **argv)
{
    unsigned saturated = 0;
    invalidation_cases();
    stage_prior_facing_cases();
    uint64_t trace = submit_trace();
    printf("gx3d submit trace: %016llx\n", (unsigned long long)trace);
    CHECK(trace == UINT64_C(0x7679d7b60433e5d0)); /* pre-change translator */
    hidden_list_profile_cases();
    hidden_list_shadow_case();
    for (int n = 0; n < 100006; n++) {
        static const int32_t edge[] = {INT32_MIN, INT32_MAX, -1, 0, 1, -4096};
        (void)value();
        int32_t v = n < 6 ? edge[n] : (int32_t)rng;
        static const int ratios[][2] = {{256,320},{192,240},{256,400},{1,1}};
        for (unsigned k = 0; k < sizeof(ratios)/sizeof(ratios[0]); k++) {
            int num = ratios[k][0], den = ratios[k][1];
            int64_t scaled = (int64_t)v * num;
            scaled = (scaled >= 0 ? scaled + den/2 : scaled - den/2) / den;
            CHECK(scale_view(v, num, den) == reference_saturate(scaled));
        }
    }
    static const int offdiag[] = {1,2,3,4,6,7,8,9,12,13};
    for (int n = 0; n < 20000; n++) {
        Matrix a, p = {{0}}, expected, actual;
        for (int k = 0; k < 16; k++) a.a[k] = value();
        /* Force genuine 32-bit output saturation while every 64-bit sum
         * remains in range; ordinary random matrices alone are too small. */
        if (n % 13 == 0) for (int k = 0; k < 16; k++) a.a[k] *= 256;
        p.a[0] = value(); p.a[5] = value(); p.a[10] = value();
        p.a[11] = value(); p.a[14] = value(); p.a[15] = value();
        /* Both signs, zeros, saturation and a one-word violation of EACH
         * sparse-shape guard. Every general matrix must retain the old path. */
        if (n % 3 == 0) p.a[offdiag[(n / 3) % 10]] = value() | 1;
        if (n % 7 == 0) { p.a[11] = 0; p.a[15] = 4096; }
        if (n % 11 == 0) { p.a[11] = -4096; p.a[15] = 0; }
        reference(&expected, &a, &p);
        for (int k = 0; k < 16; k++) saturated += expected.a[k] == INT32_MIN || expected.a[k] == INT32_MAX;
        multiply_clip(&actual, &a, &p);
        CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
        /* Aliasing contracts of the original multiply are preserved. */
        actual = a; multiply_clip(&actual, &actual, &p);
        CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
        actual = p; multiply_clip(&actual, &a, &actual);
        CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
        GeoState g = {0};
        g.pos = a; g.proj = p; g.clipDirty = 1;
        g.wideNum = g.wideDen = g.tallNum = g.tallDen = 1;
        ensure_clip(&g);
        CHECK(memcmp(&g.clip, &expected, sizeof(expected)) == 0);
        CHECK(memcmp(&g.xf, &expected, sizeof(expected)) == 0);
        for (int k = 0; k < 4; k++) CHECK(g.xfTrans[k] == ((int64_t)expected.a[12+k] << 12));
        /* Exercise the actual wide/tall ensure_clip path as well. */
        g.clipDirty = 1; g.wideNum = 256; g.wideDen = 320;
        g.tallNum = 192; g.tallDen = 240;
        g.viewport[2] = 255; g.viewport[3] = 191;
        ensure_clip(&g);
        for (int row = 0; row < 4; row++) {
            int64_t x = (int64_t)expected.a[row*4] * 256;
            int64_t y = (int64_t)expected.a[row*4+1] * 192;
            CHECK(g.xf.a[row*4] == reference_saturate((x >= 0 ? x+160 : x-160)/320));
            CHECK(g.xf.a[row*4+1] == reference_saturate((y >= 0 ? y+120 : y-120)/240));
        }
    }
    CHECK(saturated > 0);
    if (argc > 1) {
        /* Native microbenchmark only: NOT Old 3DS scene timing. */
        Matrix a, p = {{21628,0,0,0, 0,28836,0,0, 0,0,-5266,-4096, 0,0,-1404343,0}};
        for (int k = 0; k < 16; k++) a.a[k] = value();
        for (int trial = 0; trial < 3; trial++) {
            uint32_t slow, fast;
            double before = benchmark(multiply, &a, &p, &slow);
            double after = benchmark(multiply_clip, &a, &p, &fast);
            CHECK(slow == fast);
            printf("host-only matrix benchmark: old %.3f ms, candidate %.3f ms, checksum %08x\n", before, after, fast);
        }
    }
    printf("gx3d-clip-matrix: %u checks, %u failed\n", checks, failures);
    return failures != 0;
}
