/* Host test for the SDK_PORT_3DS C versions of OS_InitContext, OS_SaveContext and OS_LoadContext
 * (lib/NitroSDK/src/os/os_context.c), and of CP_SaveContext and CPi_RestoreContext
 * (lib/NitroSDK/src/cp/CP_context.c). Built and run by run.sh. Synthetic values only.
 *
 * Under SDK_PORT_3DS the registers live in the port's arrays (patch 1301); run.sh links
 * tests/sdk/port_memory.c for them. The port's thread switch (NitroPort_SaveContext,
 * NitroPort_LoadContext) and the divider/sqrt hooks (CPi_PortUpdateDiv, CPi_PortUpdateSqrt)
 * are stubs that count calls and record what they see.
 */
#include <nitro.h>

int printf(const char *, ...); /* lib/include shadows the system stdio.h; see tests/sdk/os_system */
void exit(int);

#define FIELD_OFF(T, f) ((u32)(unsigned long)&((T *)0)->f)

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EQ32(name, got, want) do { checks++; if ((u32)(got) != (u32)(want)) { fails++; \
    printf("FAIL line %d: %s = 0x%08x, want 0x%08x\n", __LINE__, name, (unsigned)(u32)(got), (unsigned)(u32)(want)); } } while (0)
#define EQ64(name, got, want) do { checks++; if ((u64)(got) != (u64)(want)) { fails++; \
    printf("FAIL line %d: %s = 0x%08x%08x, want 0x%08x%08x\n", __LINE__, name, \
           (unsigned)((u64)(got) >> 32), (unsigned)(u32)(u64)(got), \
           (unsigned)((u64)(want) >> 32), (unsigned)(u32)(u64)(want)); } } while (0)

/* ---- stubs for the port ---- */
static int saveCalls, loadCalls, saveRet;
static void *saveArg, *loadArg;

BOOL NitroPort_SaveContext(void *context)
{
    saveCalls++;
    saveArg = context;
    return (BOOL)saveRet;
}

void NitroPort_LoadContext(void *context)
{
    loadCalls++;
    loadArg = context;
}

typedef struct {
    u16 divCnt, sqrtCnt;
    u64 numer, denom, sqrtParam;
} RegSnap;

static int divHooks, sqrtHooks;
static RegSnap divSeen, sqrtSeen;

static void Snap(RegSnap *s)
{
    s->divCnt = reg_CP_DIVCNT;
    s->sqrtCnt = reg_CP_SQRTCNT;
    s->numer = reg_CP_DIV_NUMER;
    s->denom = reg_CP_DIV_DENOM;
    s->sqrtParam = reg_CP_SQRT_PARAM;
}

void CPi_PortUpdateDiv(void)
{
    divHooks++;
    Snap(&divSeen);
}

void CPi_PortUpdateSqrt(void)
{
    sqrtHooks++;
    Snap(&sqrtSeen);
}

/* ---- helpers ---- */
static void SetCp(u16 divCnt, u64 numer, u64 denom, u16 sqrtCnt, u64 sqrtParam)
{
    reg_CP_DIVCNT = divCnt;
    reg_CP_DIV_NUMER = numer;
    reg_CP_DIV_DENOM = denom;
    reg_CP_SQRTCNT = sqrtCnt;
    reg_CP_SQRT_PARAM = sqrtParam;
}

static void SetHooks(void)
{
    divHooks = sqrtHooks = 0;
}

static int SameRegs(const RegSnap *s, u16 divCnt, u64 numer, u64 denom, u16 sqrtCnt, u64 sqrtParam)
{
    return s->divCnt == divCnt && s->numer == numer && s->denom == denom
        && s->sqrtCnt == sqrtCnt && s->sqrtParam == sqrtParam;
}

/* Fill every word of a context with a pattern; returns nothing. */
static void FillCtx(OSContext *c, u32 pattern)
{
    u32 *w = (u32 *)c;
    unsigned i;
    for (i = 0; i < sizeof(OSContext) / sizeof(u32); i++) {
        w[i] = pattern;
    }
}

static int CpPartUnchanged(const OSContext *c, u32 pattern)
{
    const u32 *w = (const u32 *)c;
    unsigned i;
    for (i = OS_CONTEXT_CP_CONTEXT / sizeof(u32); i < sizeof(OSContext) / sizeof(u32); i++) {
        if (w[i] != pattern) {
            return 0;
        }
    }
    return 1;
}

/* ---- OS_InitContext ---- */
typedef struct {
    u32 entry, sp, pcPlus4, expSp, expSpSvc, expCpsr;
} InitCase;

static const InitCase kInitCases[] = {
    /* entry       sp          pc+4        sp          sp_svc      cpsr */
    { 0x02100000u, 0x023FF000u, 0x02100004u, 0x023FEFC0u, 0x023FF000u, 0x1Fu }, /* ARM, aligned */
    { 0x02100001u, 0x023FF000u, 0x02100005u, 0x023FEFC0u, 0x023FF000u, 0x3Fu }, /* Thumb, aligned */
    { 0x02100000u, 0x023FF004u, 0x02100004u, 0x023FEFC0u, 0x023FF004u, 0x1Fu }, /* ARM, bit 2 set: -4 */
    { 0x02100001u, 0x023FF004u, 0x02100005u, 0x023FEFC0u, 0x023FF004u, 0x3Fu }, /* Thumb, bit 2 set */
    { 0x02100002u, 0x023FF008u, 0x02100006u, 0x023FEFC8u, 0x023FF008u, 0x1Fu }, /* bit 1 of pc: ARM */
    { 0x02100000u, 0x023FF002u, 0x02100004u, 0x023FEFC2u, 0x023FF002u, 0x1Fu }, /* only bit 2 counts */
};

static void TestInitContext(void)
{
    unsigned k, i;

    for (k = 0; k < sizeof(kInitCases) / sizeof(kInitCases[0]); k++) {
        const InitCase *t = &kInitCases[k];
        OSContext c;
        int regsZero = 1;

        FillCtx(&c, 0xA5A5A5A5u);
        OS_InitContext(&c, t->entry, t->sp);

        EQ32("pc_plus4", c.pc_plus4, t->pcPlus4);
        EQ32("sp", c.sp, t->expSp);
        EQ32("sp_svc", c.sp_svc, t->expSpSvc);
        EQ32("cpsr", c.cpsr, t->expCpsr);
        for (i = 0; i < 13; i++) {
            if (c.r[i] != 0) {
                regsZero = 0;
            }
        }
        CHECK(regsZero, "case %u: r0-r12 zeroed", k);
        EQ32("lr", c.lr, 0);
        CHECK(CpPartUnchanged(&c, 0xA5A5A5A5u), "case %u: coprocessor context untouched", k);
    }
}

static void TestContextLayout(void)
{
    /* The asm uses the OS_CONTEXT_* offsets; the struct must agree with them. */
    EQ32("cpsr offset", FIELD_OFF(OSContext, cpsr), OS_CONTEXT_CPSR);
    EQ32("r0 offset", FIELD_OFF(OSContext, r), OS_CONTEXT_R0);
    EQ32("sp offset", FIELD_OFF(OSContext, sp), OS_CONTEXT_SP);
    EQ32("lr offset", FIELD_OFF(OSContext, lr), OS_CONTEXT_LR);
    EQ32("pc_plus4 offset", FIELD_OFF(OSContext, pc_plus4), OS_CONTEXT_PC_PLUS4);
    EQ32("sp_svc offset", FIELD_OFF(OSContext, sp_svc), OS_CONTEXT_SP_SVC);
    EQ32("cp_context offset", FIELD_OFF(OSContext, cp_context), OS_CONTEXT_CP_CONTEXT);
    EQ32("OSContext size", sizeof(OSContext), 100); /* DS layout: u64 4-aligned (patch 1001) */
    /* CPContext offsets used by the asm (sqrt at +16, div_mode +24, sqrt_mode +26). */
    EQ32("CPContext div_denom", FIELD_OFF(CPContext, div_denom), 8);
    EQ32("CPContext sqrt", FIELD_OFF(CPContext, sqrt), 16);
    EQ32("CPContext div_mode", FIELD_OFF(CPContext, div_mode), 24);
    EQ32("CPContext sqrt_mode", FIELD_OFF(CPContext, sqrt_mode), 26);
    EQ32("CPContext size", sizeof(CPContext), 28); /* the asm writes 28 bytes: div_numer..sqrt = 24, div_mode +24, sqrt_mode +26 */
}

/* ---- OS_SaveContext / OS_LoadContext: thread switch is the port's; no coprocessor work here ---- */
static void TestSaveLoad(void)
{
    OSContext c;
    u32 pattern = 0x5A5A5A5Au;
    BOOL r;

    FillCtx(&c, pattern);
    SetCp(0x0002, 0x1111222233334444ULL, 0x5555666677778888ULL, 0x0001, 0x9999AAAABBBBCCCCULL);
    SetHooks();

    saveRet = 0;
    saveCalls = 0;
    r = OS_SaveContext(&c);
    EQ32("OS_SaveContext returns the port's FALSE", r, 0);
    EQ32("save calls the port once", saveCalls, 1);
    CHECK(saveArg == (void *)&c, "save passes the context");
    saveRet = 1;
    r = OS_SaveContext(&c);
    EQ32("OS_SaveContext passes the port's TRUE through", r, 1);
    EQ32("save calls the port again", saveCalls, 2);

    CHECK(CpPartUnchanged(&c, pattern), "save leaves the coprocessor context to the port");
    EQ32("save: no divider hook", divHooks, 0);
    EQ32("save: no sqrt hook", sqrtHooks, 0);
    {
        RegSnap now;
        Snap(&now);
        CHECK(SameRegs(&now, 0x0002, 0x1111222233334444ULL, 0x5555666677778888ULL, 0x0001,
                       0x9999AAAABBBBCCCCULL), "save leaves the divider registers alone");
    }

    loadCalls = 0;
    OS_LoadContext(&c);
    EQ32("OS_LoadContext calls the port once", loadCalls, 1);
    CHECK(loadArg == (void *)&c, "load passes the context");
    CHECK(CpPartUnchanged(&c, pattern), "load leaves the coprocessor context to the port");
    EQ32("load: no divider hook (no restore before the switch)", divHooks, 0);
    EQ32("load: no sqrt hook", sqrtHooks, 0);
    {
        RegSnap now;
        Snap(&now);
        CHECK(SameRegs(&now, 0x0002, 0x1111222233334444ULL, 0x5555666677778888ULL, 0x0001,
                       0x9999AAAABBBBCCCCULL), "load leaves the divider registers alone");
    }
}

/* ---- CP_SaveContext ---- */
static void TestCpSave(void)
{
    typedef struct {
        u16 divCnt, sqrtCnt, wantDivMode, wantSqrtMode;
        u64 numer, denom, sqrtParam;
    } SaveCase;
    static const SaveCase cases[] = {
        /* busy and error flags are not part of the mode */
        { 0xC002, 0x8001, 2, 1, 0x1122334455667788ULL, 0x0102030405060708ULL, 0x0123456789ABCDEFULL },
        { 0x4003, 0x0000, 3, 0, 0x8000000000000001ULL, 0xFFFFFFFF00000003ULL, 0x0000000200000001ULL },
        { 0x8000, 0x8000, 0, 0, 0x0000000000000000ULL, 0x0000000000000007ULL, 0xFEDCBA9876543210ULL },
    };
    unsigned k;

    for (k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        const SaveCase *t = &cases[k];
        struct { CPContext c; unsigned char guard[8]; } w; /* guard bytes catch a write past the struct */
        CPContext *const ctxp = &w.c;
        unsigned char *b = (unsigned char *)&w;
        int pad;
        unsigned i;

        for (i = 0; i < sizeof(w); i++) {
            b[i] = 0xEE;
        }
        SetCp(t->divCnt, t->numer, t->denom, t->sqrtCnt, t->sqrtParam);
        SetHooks();
        CP_SaveContext(ctxp);

        EQ64("save div_numer", ctxp->div_numer, t->numer);
        EQ64("save div_denom", ctxp->div_denom, t->denom);
        EQ64("save sqrt", ctxp->sqrt, t->sqrtParam);
        EQ32("save div_mode", ctxp->div_mode, t->wantDivMode);
        EQ32("save sqrt_mode", ctxp->sqrt_mode, t->wantSqrtMode);
        pad = 1;
        for (i = sizeof(CPContext); i < sizeof(w); i++) {
            if (b[i] != 0xEE) {
                pad = 0;
            }
        }
        CHECK(pad, "case %u: nothing written past the end of CPContext", k);
        EQ32("save runs no hook", divHooks + sqrtHooks, 0);
    }
}

/* ---- CPi_RestoreContext and the round trip ---- */
static void TestCpRestore(void)
{
    CPContext ctx;
    CPContext saved;

    ctx.div_numer = 0x0FEDCBA987654321ULL;
    ctx.div_denom = 0x1000000000000007ULL;
    ctx.sqrt = 0x0000000200000001ULL;
    ctx.div_mode = 2;
    ctx.sqrt_mode = 1;

    SetCp(0xC000, 0, 0, 0x8000, 0); /* junk to be overwritten */
    SetHooks();
    CPi_RestoreContext(&ctx);

    EQ64("restore numer", reg_CP_DIV_NUMER, ctx.div_numer);
    EQ64("restore denom", reg_CP_DIV_DENOM, ctx.div_denom);
    EQ64("restore sqrt param", reg_CP_SQRT_PARAM, ctx.sqrt);
    EQ32("restore DIVCNT", reg_CP_DIVCNT, 2);
    EQ32("restore SQRTCNT", reg_CP_SQRTCNT, 1);
    EQ32("restore: divider hook runs once", divHooks, 1);
    EQ32("restore: sqrt hook runs once", sqrtHooks, 1);
    /* The hooks run after the whole write sequence: they see every restored register. */
    CHECK(SameRegs(&divSeen, 2, ctx.div_numer, ctx.div_denom, 1, ctx.sqrt),
          "divider hook sees all restored registers");
    CHECK(SameRegs(&sqrtSeen, 2, ctx.div_numer, ctx.div_denom, 1, ctx.sqrt),
          "sqrt hook sees all restored registers");

    /* Round trip: save what the registers hold, clear them, restore, and compare. */
    SetCp(0x0001, 0x7FFF0000FFFF0001ULL, 0x0000000100000003ULL, 0x0000, 0xFEDCBA9876543210ULL);
    CP_SaveContext(&saved);
    SetCp(0, 0, 0, 0, 0);
    SetHooks();
    CPi_RestoreContext(&saved);
    CHECK(SameRegs(&divSeen, 0x0001, 0x7FFF0000FFFF0001ULL, 0x0000000100000003ULL, 0x0000,
                   0xFEDCBA9876543210ULL), "round trip restores every register");
    EQ64("round trip numer", reg_CP_DIV_NUMER, 0x7FFF0000FFFF0001ULL);
    EQ64("round trip denom", reg_CP_DIV_DENOM, 0x0000000100000003ULL);
    EQ64("round trip sqrt", reg_CP_SQRT_PARAM, 0xFEDCBA9876543210ULL);
    EQ32("round trip DIVCNT", reg_CP_DIVCNT, 0x0001);
    EQ32("round trip SQRTCNT", reg_CP_SQRTCNT, 0x0000);
}

int main(void)
{
    TestContextLayout();
    TestInitContext();
    TestSaveLoad();
    TestCpSave();
    TestCpRestore();

    printf("cp_context (os/cp): %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
