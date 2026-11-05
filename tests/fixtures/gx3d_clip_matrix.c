/* Exercise actual fixed-point geometry math without the host32 game ABI. */
#include <stdio.h>
#include <time.h>
#include GX3D_SOURCE

unsigned char NitroPort_IoReg[0x2000] __attribute__((aligned(8)));
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
    invalidation_cases();
    static const int offdiag[] = {1,2,3,4,6,7,8,9,12,13};
    for (int n = 0; n < 20000; n++) {
        Matrix a, p = {{0}}, expected, actual;
        for (int k = 0; k < 16; k++) a.a[k] = value();
        p.a[0] = value(); p.a[5] = value(); p.a[10] = value();
        p.a[11] = value(); p.a[14] = value(); p.a[15] = value();
        /* Both signs, zeros, saturation and a one-word violation of EACH
         * sparse-shape guard. Every general matrix must retain the old path. */
        if (n % 3 == 0) p.a[offdiag[(n / 3) % 10]] = value() | 1;
        if (n % 7 == 0) { p.a[11] = 0; p.a[15] = 4096; }
        if (n % 11 == 0) { p.a[11] = -4096; p.a[15] = 0; }
        reference(&expected, &a, &p);
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
    }
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
