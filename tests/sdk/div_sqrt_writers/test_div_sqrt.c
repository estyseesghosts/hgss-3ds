/*
 * Host test for the remaining DIV/SQRT writer hooks (patch 0502): the raw divider and
 * square-root register writes in lib/NitroSDK/src/fx/{fx_vec.c,fx_vec_mult_add.c,
 * fx_vec_normalize.c,fx_mtx44_ortho.c,fx_mtx44_perspective.c} and lib/NitroSDK/src/tp.c.
 * Built and run by run.sh. Synthetic values only.
 *
 * CPi_PortUpdateDiv/Sqrt are stubs that emulate the divider and square root the way
 * 3ds_port/nitro/src/port_cp.c does (GBATEK "DS Maths": C division truncates toward zero
 * like the hardware; the error flag looks at the whole 64-bit denominator). Each counts its
 * calls, so a dropped hook shows up both as a wrong value (result registers are preset to a
 * sentinel first) and as a wrong call count. References are plain integer arithmetic with no
 * register traffic: exact equality is expected everywhere.
 */
/* No <stdio.h>: the project include path (lib/include) shadows stdarg.h, which breaks the
 * system stdio.h. Output goes through write(1); printf is declared by hand. */
#include <stdint.h>
#include <unistd.h>

#include <nitro.h>
#include <sys/mman.h>

int printf(const char *, ...);
void exit(int);

#define IO_BASE 0x04000000u
#define SENTINEL64 0x7EADBEEF5A5A5A5AuLL
#define SENTINEL32 0x7EADBEEFu

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EQ32(name, got, want) do { checks++; if ((s32)(got) != (s32)(want)) { fails++; \
    printf("FAIL line %d: %s = %d (0x%08x), want %d (0x%08x)\n", __LINE__, name, \
           (int)(s32)(got), (unsigned)(u32)(got), (int)(s32)(want), (unsigned)(u32)(want)); } } while (0)

/* ---- the port's hooks (stubs) ---- */
static int divCalls, sqrtCalls;

void CPi_PortUpdateDiv(void)
{
    u16 cnt = reg_CP_DIVCNT;
    u32 mode = cnt & REG_CP_DIVCNT_MODE_MASK;
    u64 numerRaw = *(volatile u64 *)REG_DIV_NUMER_ADDR;
    u64 denomRaw = *(volatile u64 *)REG_DIV_DENOM_ADDR;
    s64 numer, denom, quot, rem;

    divCalls++;
    switch (mode) {
    case 0:
        numer = (s32)(u32)numerRaw;
        denom = (s32)(u32)denomRaw;
        break;
    case 2:
        numer = (s64)numerRaw;
        denom = (s64)denomRaw;
        break;
    default:
        numer = (s64)numerRaw;
        denom = (s32)(u32)denomRaw;
        break;
    }
    if (denomRaw == 0) {
        cnt |= REG_CP_DIVCNT_DIV0_MASK;
    } else {
        cnt &= (u16)~REG_CP_DIVCNT_DIV0_MASK;
    }
    if (denom == 0) {
        rem = numer;
        quot = (numer < 0) ? 1 : -1;
        if (mode == 0) {
            quot = (s64)((u64)quot ^ 0xFFFFFFFF00000000ULL);
        }
    } else if (denom == -1 && numer == (s64)0x8000000000000000ULL) {
        quot = (s64)0x8000000000000000ULL;
        rem = 0;
    } else {
        quot = numer / denom;
        rem = numer % denom;
    }
    *(volatile u64 *)REG_DIV_RESULT_ADDR = (u64)quot;
    *(volatile u64 *)REG_DIVREM_RESULT_ADDR = (u64)rem;
    reg_CP_DIVCNT = (u16)(cnt & ~REG_CP_DIVCNT_BUSY_MASK);
}

/* floor(sqrt(x)) for any 64-bit x, exactly (no floating point). */
static u32 isqrt64(u64 x)
{
    u64 result = 0;
    u64 bit = 1ULL << 62;

    while (bit > x) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (x >= result + bit) {
            x -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (u32)result;
}

void CPi_PortUpdateSqrt(void)
{
    u16 cnt = reg_CP_SQRTCNT;
    u64 param = *(volatile u64 *)REG_SQRT_PARAM_ADDR;

    sqrtCalls++;
    if ((cnt & REG_CP_SQRTCNT_MODE_MASK) == 0) {
        param = (u32)param;
    }
    *(volatile u32 *)REG_SQRT_RESULT_ADDR = isqrt64(param);
    reg_CP_SQRTCNT = (u16)(cnt & ~REG_CP_SQRTCNT_BUSY_MASK);
}

/* ---- stubs for the rest of tp.o's imports (never called by the checks below) ---- */
OSIntrMode OS_DisableInterrupts(void)
{
    return 0;
}

OSIntrMode OS_RestoreInterrupts(OSIntrMode state)
{
    (void)state;
    return 0;
}

void OS_Terminate(void)
{
}

void PXI_Init(void)
{
}

void PXI_SetFifoRecvCallback(int fifotag, PXIFifoCallback callback)
{
    (void)fifotag;
    (void)callback;
}

BOOL PXI_IsCallbackReady(int fifotag, PXIProc proc)
{
    (void)fifotag;
    (void)proc;
    return FALSE;
}

int PXI_SendWordByFifo(int fifotag, u32 data, BOOL err)
{
    (void)fifotag;
    (void)data;
    (void)err;
    return 0;
}

/* tp.c leaves these two to the game. The layouts repeat tp.c's TpWork/TpCalibration so the
 * TP_SetCalibrateParam checks below read the fields the SDK writes; a layout drift fails the
 * value checks rather than silently passing. */
typedef struct {
    s32 x0;
    s32 xDotSize;
    s32 xScale;
    s32 y0;
    s32 yDotSize;
    s32 yScale;
} TestTpCalibration;

typedef struct {
    u16 init;
    u16 unk_02;
    void *callback;
    TPData last;
    volatile u16 index;
    u16 frequency;
    TPData *buffer;
    u16 count;
    u16 unk_1A;
    TestTpCalibration calibration;
    u16 calibrated;
    u16 state;
    volatile u16 error;
    volatile u16 busy;
} TestTpWork;

TestTpWork gTpWork;
TPCalibrateParam gTpCalibration;

/* ---- the functions under test (signatures copied from the SDK sources) ---- */
fx32 VEC_Mag(VecFx32 *a);
fx32 VEC_Distance(const VecFx32 *a, const VecFx32 *b);
void VEC_Normalize(const VecFx32 *src, VecFx32 *dst);
void VEC_Fx16Normalize(const VecFx16 *src, VecFx16 *dst);
void MTX_OrthoW(fx32 top, fx32 bottom, fx32 left, fx32 right, fx32 near, fx32 far, fx32 scale,
                MtxFx44 *m);
void MTX_PerspectiveW(fx32 fovySin, fx32 fovyCos, fx32 aspect, fx32 n, fx32 f, fx32 scaleW,
                      MtxFx44 *mtx);
void TP_SetCalibrateParam(const TPCalibrateParam *param);
int TP_CalcCalibrateParam(TPCalibrateParam *calibrate, u16 raw_x1, u16 raw_y1, u16 dx1, u16 dy1,
                          u16 raw_x2, u16 raw_y2, u16 dx2, u16 dy2);

static void MapIoPage(void)
{
    void *p = mmap((void *)(unsigned long)IO_BASE, 0x1000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    if (p != (void *)(unsigned long)IO_BASE) {
        printf("cannot map the I/O page at 0x%08x (got %lx)\n", IO_BASE, (unsigned long)p);
        exit(2);
    }
}

/* Sentinel result registers: a read before the hook sees these, not a stale value. */
static void PresetResults(void)
{
    *(volatile u64 *)REG_DIV_RESULT_ADDR = (u64)SENTINEL64;
    *(volatile u64 *)REG_DIVREM_RESULT_ADDR = (u64)SENTINEL64;
    *(volatile u32 *)REG_SQRT_RESULT_ADDR = SENTINEL32;
}

static void ResetHooks(void)
{
    divCalls = sqrtCalls = 0;
}

static void CheckVecMag(void)
{
    VecFx32 a;
    s64 l2;
    fx32 want;

    /* (3, 4, 0): |v| = 5. */
    a.x = 3 * 4096;
    a.y = 4 * 4096;
    a.z = 0;
    l2 = (s64)a.x * a.x + (s64)a.y * a.y + (s64)a.z * a.z;
    want = (fx32)((isqrt64((u64)(l2 << 2)) + 1) >> 1);
    PresetResults();
    ResetHooks();
    EQ32("mag(3,4,0)", VEC_Mag(&a), want);
    EQ32("mag(3,4,0) want", want, 5 * 4096);
    CHECK(divCalls == 0, "mag div calls %d, want 0", divCalls);
    CHECK(sqrtCalls == 1, "mag sqrt calls %d, want 1", sqrtCalls);

    /* Zero vector. */
    a.x = a.y = a.z = 0;
    PresetResults();
    ResetHooks();
    EQ32("mag(0,0,0)", VEC_Mag(&a), 0);
    CHECK(sqrtCalls == 1, "mag0 sqrt calls %d, want 1", sqrtCalls);

    /* Unit x. */
    a.x = 4096;
    a.y = 0;
    a.z = 0;
    PresetResults();
    ResetHooks();
    EQ32("mag(1,0,0)", VEC_Mag(&a), 4096);
    CHECK(sqrtCalls == 1, "mag1 sqrt calls %d, want 1", sqrtCalls);
}

static void CheckVecDistance(void)
{
    VecFx32 a, b;
    s64 dx, dy, dz, l2;
    fx32 want;

    a.x = 3 * 4096;
    a.y = 4 * 4096;
    a.z = 4096;
    b.x = 0;
    b.y = 0;
    b.z = 4096;
    dx = (s64)(a.x - b.x);
    dy = (s64)(a.y - b.y);
    dz = (s64)(a.z - b.z);
    l2 = dx * dx + dy * dy + dz * dz;
    want = (fx32)((isqrt64((u64)(l2 * 4)) + 1) >> 1);
    PresetResults();
    ResetHooks();
    EQ32("dist", VEC_Distance(&a, &b), want);
    EQ32("dist want", want, 5 * 4096);
    CHECK(divCalls == 0, "dist div calls %d, want 0", divCalls);
    CHECK(sqrtCalls == 1, "dist sqrt calls %d, want 1", sqrtCalls);
}

static void CheckVecNormalize(void)
{
    VecFx32 src, dst;
    VecFx16 src16, dst16;
    s64 len, quot, factor, root;

    src.x = 3 * 4096;
    src.y = 4 * 4096;
    src.z = 0;
    len = (s64)src.x * src.x + (s64)src.y * src.y + (s64)src.z * src.z;
    quot = 0x0100000000000000LL / len;
    root = isqrt64((u64)(len << 2));
    factor = quot * root;
    PresetResults();
    ResetHooks();
    VEC_Normalize(&src, &dst);
    EQ32("norm.x", dst.x, (fx32)((factor * src.x + (1LL << 44)) >> 45));
    EQ32("norm.y", dst.y, (fx32)((factor * src.y + (1LL << 44)) >> 45));
    EQ32("norm.z", dst.z, (fx32)((factor * src.z + (1LL << 44)) >> 45));
    EQ32("norm.x sane", dst.x, 2458); /* 0.6 in fx32, rounded */
    EQ32("norm.y sane", dst.y, 3277); /* 0.8 in fx32, rounded */
    CHECK(divCalls == 1, "norm div calls %d, want 1", divCalls);
    CHECK(sqrtCalls == 1, "norm sqrt calls %d, want 1", sqrtCalls);

    src16.x = 3 * 256;
    src16.y = 4 * 256;
    src16.z = 0;
    len = (s64)src16.x * src16.x + (s64)src16.y * src16.y + (s64)src16.z * src16.z;
    quot = 0x0100000000000000LL / len;
    root = isqrt64((u64)(len << 2));
    factor = quot * root;
    PresetResults();
    ResetHooks();
    VEC_Fx16Normalize(&src16, &dst16);
    EQ32("norm16.x", dst16.x, (fx16)((factor * src16.x + (1LL << 44)) >> 45));
    EQ32("norm16.y", dst16.y, (fx16)((factor * src16.y + (1LL << 44)) >> 45));
    EQ32("norm16.z", dst16.z, (fx16)((factor * src16.z + (1LL << 44)) >> 45));
    CHECK(divCalls == 1, "norm16 div calls %d, want 1", divCalls);
    CHECK(sqrtCalls == 1, "norm16 sqrt calls %d, want 1", sqrtCalls);
}

static void CheckOrtho(void)
{
    /* Symmetric frustum, unit scale, then a scaled one. References mirror the SDK formula
     * with plain s64 arithmetic (no divider): exact equality is expected. */
    MtxFx44 m;
    fx32 top = 96 * 4096, bottom = -96 * 4096, left = -128 * 4096, right = 128 * 4096;
    fx32 near = 4096, far = 100 * 4096, scale = 4096;
    s64 w, h, d, x, y, z;

    w = (s64)right - left;
    h = (s64)top - bottom;
    d = (s64)near - far;
    x = ((s64)4096 << 32) / w;
    y = ((s64)4096 << 32) / h;
    z = ((s64)4096 << 32) / d;
    PresetResults();
    ResetHooks();
    MTX_OrthoW(top, bottom, left, right, near, far, scale, &m);
    EQ32("ortho._00", m._00, (fx32)(((x << 13) + 0x80000000LL) >> 32));
    EQ32("ortho._11", m._11, (fx32)(((y << 13) + 0x80000000LL) >> 32));
    EQ32("ortho._22", m._22, (fx32)(((z << 13) + 0x80000000LL) >> 32));
    EQ32("ortho._30", m._30, (fx32)((x * -(right + left) + 0x80000000LL) >> 32));
    EQ32("ortho._31", m._31, (fx32)((y * -(top + bottom) + 0x80000000LL) >> 32));
    EQ32("ortho._32", m._32, (fx32)((z * (far + near) + 0x80000000LL) >> 32));
    EQ32("ortho._33", m._33, scale);
    EQ32("ortho._00 sane", m._00, 32); /* 2/256 in fx32 */
    EQ32("ortho._30 sane", m._30, 0);
    EQ32("ortho._31 sane", m._31, 0);
    CHECK(divCalls == 3, "ortho div calls %d, want 3", divCalls);
    CHECK(sqrtCalls == 0, "ortho sqrt calls %d, want 0", sqrtCalls);

    /* Non-unit scale exercises the x*scale/4096 lines (plain arithmetic, same hooks). */
    scale = 2048;
    x = x * scale / 4096;
    y = y * scale / 4096;
    z = z * scale / 4096;
    PresetResults();
    ResetHooks();
    MTX_OrthoW(top, bottom, left, right, near, far, scale, &m);
    EQ32("ortho2._00", m._00, (fx32)(((x << 13) + 0x80000000LL) >> 32));
    EQ32("ortho2._11", m._11, (fx32)(((y << 13) + 0x80000000LL) >> 32));
    EQ32("ortho2._22", m._22, (fx32)(((z << 13) + 0x80000000LL) >> 32));
    EQ32("ortho2._33", m._33, scale);
    CHECK(divCalls == 3, "ortho2 div calls %d, want 3", divCalls);
}

static void CheckPerspective(void)
{
    MtxFx44 m;
    fx32 fovySin = 2048, fovyCos = 3547; /* 30 degrees: cot = 1.732 */
    fx32 aspect = 4096, n = 4096, f = 100 * 4096, scaleW = 4096;
    s64 cot64, t64, t2;
    s32 fovyCot, m00;
    s64 t;

    /* FX_GetDivResult scales the 64-bit quotient down by 2^20 (see fx_cp.c): the raw
     * 64/32 quotient of (cos<<32)/sin is cot*2^20, so the fx32 cotangent is (q+2^19)>>20. */
    cot64 = ((s64)fovyCos << 32) / (s64)fovySin;
    fovyCot = (s32)(((u64)cot64 + (1 << 19)) >> 20);
    t64 = ((s64)4096 << 32) / ((s64)n - f);
    t = (s64)t64;
    t2 = ((s64)fovyCot << 32) / (s64)aspect;
    m00 = (s32)(((u64)t2 + (1 << 19)) >> 20);
    PresetResults();
    ResetHooks();
    MTX_PerspectiveW(fovySin, fovyCos, aspect, n, f, scaleW, &m);
    EQ32("persp._11", m._11, fovyCot);
    EQ32("persp._22", m._22,
         (fx32)((t * ((s64)f + n) + 0x80000000LL) >> 32));
    EQ32("persp._32", m._32,
         (fx32)((t * (fx32)(((((s64)(n << 1) * f) + 0x800) >> 12)) + 0x80000000LL) >> 32));
    EQ32("persp._00", m._00, m00);
    EQ32("persp._23", m._23, -scaleW);
    EQ32("persp._01", m._01, 0);
    EQ32("persp._33", m._33, 0);
    CHECK(divCalls == 3, "persp div calls %d, want 3", divCalls);
    CHECK(sqrtCalls == 0, "persp sqrt calls %d, want 0", sqrtCalls);
}

static void CheckTpSetCalibrate(void)
{
    TPCalibrateParam param;

    param.x0 = 100;
    param.xDotSize = 256;
    param.y0 = 50;
    param.yDotSize = 200;
    PresetResults();
    ResetHooks();
    TP_SetCalibrateParam(&param);
    EQ32("tp.xScale", gTpWork.calibration.xScale, 0x10000000 / 256);
    EQ32("tp.yScale", gTpWork.calibration.yScale, 0x10000000 / 200);
    EQ32("tp.x0", gTpWork.calibration.x0, 100);
    EQ32("tp.xDotSize", gTpWork.calibration.xDotSize, 256);
    EQ32("tp.y0", gTpWork.calibration.y0, 50);
    EQ32("tp.yDotSize", gTpWork.calibration.yDotSize, 200);
    CHECK(gTpWork.calibrated == 1, "tp calibrated %d, want 1", gTpWork.calibrated);
    CHECK(divCalls == 2, "tp div calls %d, want 2", divCalls);

    /* Zero dot sizes take the else branches: no division, zeroed scales. */
    param.x0 = 0;
    param.xDotSize = 0;
    param.y0 = 0;
    param.yDotSize = 0;
    PresetResults();
    ResetHooks();
    TP_SetCalibrateParam(&param);
    EQ32("tp0.xScale", gTpWork.calibration.xScale, 0);
    EQ32("tp0.yScale", gTpWork.calibration.yScale, 0);
    CHECK(divCalls == 0, "tp0 div calls %d, want 0", divCalls);
}

static void CheckTpCalcCalibrate(void)
{
    TPCalibrateParam cal;
    int rc;

    PresetResults();
    ResetHooks();
    rc = TP_CalcCalibrateParam(&cal, 1000, 800, 40, 30, 3000, 2400, 200, 150);
    EQ32("tpcalc.rc", rc, 0);
    EQ32("tpcalc.xDotSize", cal.xDotSize, 3200); /* (-2000<<8)/-160 */
    EQ32("tpcalc.x0", cal.x0, 2000);
    EQ32("tpcalc.yDotSize", cal.yDotSize, 3413); /* (-1600<<8)/-120, truncated */
    EQ32("tpcalc.y0", cal.y0, 1600);
    CHECK(divCalls == 2, "tpcalc div calls %d, want 2", divCalls);

    /* Degenerate points are rejected without touching the divider. */
    PresetResults();
    ResetHooks();
    rc = TP_CalcCalibrateParam(&cal, 1000, 800, 40, 30, 1000, 2400, 200, 150);
    EQ32("tpcalc.degenerate", rc, 1);
    CHECK(divCalls == 0, "tpcalc degenerate div calls %d, want 0", divCalls);
}

int main(void)
{
    MapIoPage();
    CheckVecMag();
    CheckVecDistance();
    CheckVecNormalize();
    CheckOrtho();
    CheckPerspective();
    CheckTpSetCalibrate();
    CheckTpCalcCalibrate();
    printf("%s: %d checks, %d failures\n", fails == 0 ? "PASS" : "FAIL", checks, fails);
    return fails != 0;
}
