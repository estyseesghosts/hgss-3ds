/*
 * Host test for the divider and square-root hooks in lib/NitroSDK/src/fx/fx_cp.c (SDK_PORT_3DS).
 * Built and run by run.sh. Synthetic values only.
 *
 * The port's CPi_PortUpdateDiv and CPi_PortUpdateSqrt are stubs. Each counts its calls, records
 * the registers it is called with, and writes a canned result into the result register, as the
 * port does. Result registers are preset to a sentinel first, so a result read that happens
 * before the hook shows up as the sentinel, not the canned value.
 */
#include <nitro.h>
#include <sys/mman.h>

int printf(const char *, ...); /* lib/include shadows the system stdio.h; see tests/sdk/os_system */
void exit(int);

#define IO_BASE 0x04000000u
#define SENTINEL 0x7EADBEEF00000000ULL

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EQ32(name, got, want) do { checks++; if ((u32)(got) != (u32)(want)) { fails++; \
    printf("FAIL line %d: %s = 0x%08x, want 0x%08x\n", __LINE__, name, (unsigned)(u32)(got), (unsigned)(u32)(want)); } } while (0)
#define EQ64(name, got, want) do { checks++; if ((u64)(got) != (u64)(want)) { fails++; \
    printf("FAIL line %d: %s = 0x%08x%08x, want 0x%08x%08x\n", __LINE__, name, \
           (unsigned)((u64)(got) >> 32), (unsigned)(u32)(u64)(got), \
           (unsigned)((u64)(want) >> 32), (unsigned)(u32)(u64)(want)); } } while (0)

/* ---- the port's hooks (stubs) ---- */
static int divHooks, sqrtHooks;
static u16 divSeenCnt, sqrtSeenCnt;
static u64 divSeenNumer, divSeenDenom, sqrtSeenParam;
static u64 stubDivResult, stubDivRem;
static u32 stubSqrtResult;

void CPi_PortUpdateDiv(void)
{
    divHooks++;
    divSeenCnt = reg_CP_DIVCNT;
    divSeenNumer = reg_CP_DIV_NUMER;
    divSeenDenom = reg_CP_DIV_DENOM;
    reg_CP_DIV_RESULT = stubDivResult;
    reg_CP_DIVREM_RESULT = stubDivRem;
}

void CPi_PortUpdateSqrt(void)
{
    sqrtHooks++;
    sqrtSeenCnt = reg_CP_SQRTCNT;
    sqrtSeenParam = reg_CP_SQRT_PARAM;
    reg_CP_SQRT_RESULT = stubSqrtResult;
}

/* ---- the functions under test ---- */
void FX_InvAsync(fx32 x);
void FX_DivAsync(fx32 numerator, fx32 denominator);
fx32 FX_Div(fx32 numerator, fx32 denominator);
fx32 FX_Inv(fx32 x);
fx64c FX_InvFx64c(fx32 x);
fx32 FX_DivS32(fx32 numerator, fx32 denominator);
fx32 FX_ModS32(fx32 num, fx32 mod);
fx32 FX_Sqrt(fx32 x);
fx32 FX_GetDivResult(void);
fx32 FX_GetSqrtResult(void);

static void MapIoPage(void)
{
    void *p = mmap((void *)(unsigned long)IO_BASE, 0x1000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != (void *)(unsigned long)IO_BASE) {
        printf("cannot map the I/O page at 0x%08x (got %lx)\n", IO_BASE, (unsigned long)p);
        exit(2);
    }
}

static void ResetHooks(void)
{
    divHooks = sqrtHooks = 0;
}

static void PresetResults(void)
{
    reg_CP_DIV_RESULT = SENTINEL;
    reg_CP_DIVREM_RESULT = SENTINEL;
    reg_CP_SQRT_RESULT = 0x7EADBEEFu;
}

int main(void)
{
    MapIoPage();

    /* Divider writes: DIVCNT mode 1 (64/32) for FX_InvAsync and FX_DivAsync, and the hook runs after
     * the denominator write (so it sees it). Exactly one divider hook per operation. */
    ResetHooks();
    FX_InvAsync(0x2000);
    EQ32("InvAsync: divider hook once", divHooks, 1);
    EQ32("InvAsync: no sqrt hook", sqrtHooks, 0);
    EQ32("InvAsync: DIVCNT seen", divSeenCnt, 1);
    EQ64("InvAsync: numer seen", divSeenNumer, 0x0000100000000000ULL);
    EQ64("InvAsync: denom seen", divSeenDenom, 0x2000);

    ResetHooks();
    FX_DivAsync(0x3000, 0x1000);
    EQ32("DivAsync: divider hook once", divHooks, 1);
    EQ32("DivAsync: DIVCNT seen", divSeenCnt, 1);
    EQ64("DivAsync: numer seen", divSeenNumer, 0x0000300000000000ULL);
    EQ64("DivAsync: denom seen", divSeenDenom, 0x1000);

    /* The results are read after the hook: a sentinel left in the register would show up. */
    ResetHooks();
    PresetResults();
    stubDivResult = (u64)0x1234 << 20;
    EQ32("FX_Div returns the hook's quotient", FX_Div(0x3000, 0x1000), 0x1234);
    EQ32("FX_Div: divider hook once", divHooks, 1);

    ResetHooks();
    PresetResults();
    stubDivResult = (u64)0x0BCD << 20;
    EQ32("FX_Inv returns the hook's result", FX_Inv(0x2000), 0x0BCD);
    EQ32("FX_Inv: divider hook once", divHooks, 1);
    EQ64("FX_Inv: denom seen", divSeenDenom, 0x2000);

    ResetHooks();
    PresetResults();
    stubDivResult = 0x0000123456789ABCULL;
    EQ64("FX_InvFx64c returns the 64-bit result", FX_InvFx64c(0x2000), 0x0000123456789ABCULL);
    EQ32("FX_InvFx64c: divider hook once", divHooks, 1);

    /* 32-bit divide and modulo: DIVCNT is set to mode 0, the hook runs before the busy wait. */
    ResetHooks();
    PresetResults();
    reg_CP_DIV_NUMER = 0xCAFEBABE00000000ULL; /* only the low word is written by the code */
    stubDivResult = 0xDEAD000000001234ULL;     /* the low word is the 32-bit result */
    EQ32("FX_DivS32 returns the low word of the hook's result", FX_DivS32((fx32)-4096, 0x1000), 0x1234);
    EQ32("DivS32: divider hook once", divHooks, 1);
    EQ32("DivS32: no sqrt hook", sqrtHooks, 0);
    EQ32("DivS32: mode 0 seen", divSeenCnt, 0);
    EQ64("DivS32: numer low word seen", divSeenNumer, 0xCAFEBABE00000000ULL | (u32)(fx32)-4096);
    EQ64("DivS32: denom seen", divSeenDenom, 0x1000);

    ResetHooks();
    PresetResults();
    stubDivRem = 0xABCD000000000042ULL;
    EQ32("FX_ModS32 returns the low word of the remainder", FX_ModS32(0x5000, 0x0700), 0x42);
    EQ32("ModS32: divider hook once", divHooks, 1);

    /* Square root: one hook, after the parameter write. */
    ResetHooks();
    PresetResults();
    stubSqrtResult = 0x1234u << 10;
    EQ32("FX_Sqrt returns the hook's result", FX_Sqrt(0x2000), 0x1234);
    EQ32("Sqrt: sqrt hook once", sqrtHooks, 1);
    EQ32("Sqrt: no divider hook", divHooks, 0);
    EQ32("Sqrt: SQRTCNT seen", sqrtSeenCnt, 1);
    EQ64("Sqrt: param seen", sqrtSeenParam, 0x0000200000000000ULL);

    /* Non-positive input: no register writes and no hook; result 0. */
    ResetHooks();
    reg_CP_SQRTCNT = 0x0000;
    reg_CP_SQRT_PARAM = 0x5555555500000000ULL;
    EQ32("FX_Sqrt(0) is 0", FX_Sqrt(0), 0);
    EQ32("FX_Sqrt(-5) is 0", FX_Sqrt(-5), 0);
    EQ32("non-positive sqrt: no hook", sqrtHooks, 0);
    EQ32("non-positive sqrt: SQRTCNT untouched", reg_CP_SQRTCNT, 0);
    EQ64("non-positive sqrt: param untouched", reg_CP_SQRT_PARAM, 0x5555555500000000ULL);

    /* Result readers do not start anything. */
    ResetHooks();
    (void)FX_GetDivResult();
    (void)FX_GetSqrtResult();
    EQ32("result readers: no divider hook", divHooks, 0);
    EQ32("result readers: no sqrt hook", sqrtHooks, 0);

    printf("fx_cp hooks: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
