/*
 * Host test for the SDK_PORT_3DS 3D command hooks (patch 0604): the G3_* immediate writes
 * in lib/include/nitro/gx/g3imm.h, the matrix loads in lib/NitroSDK/src/gx/g3imm.c, the
 * GXSTAT stack-error clears in lib/NitroSDK/src/gx/g3x.c, the G3X_ResetMtxStackOverflow
 * inline in lib/include/nitro/gx/g3x.h, and the static MI_GetUncompressedSize in
 * lib/include/nitro/mi/uncompress.h. Built and run by run.sh. Synthetic values only.
 *
 * The port geometry engine (3ds_port/nitro/src/port_gx3d.c) is stubs that record the
 * opcode/value stream. Linking this test with g3imm.o proves the uncompress.h fix too:
 * both TUs include <nitro.h>, so a bare `inline` would fail the link with multiple
 * definitions of MI_GetUncompressedSize.
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
#define MAXWORDS 40

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EQU32(name, got, want) do { checks++; if ((u32)(got) != (u32)(want)) { fails++; \
    printf("FAIL line %d: %s = 0x%08x, want 0x%08x\n", __LINE__, name, \
           (unsigned)(u32)(got), (unsigned)(u32)(want)); } } while (0)

/* ---- the port geometry engine (stubs) ---- */
static int dwCalls, dwOpcodes[MAXWORDS];
static u32 dwValues[MAXWORDS];
static int clearCalls, syncCalls;

void NitroPort_GxDirectWrite(int opcode, unsigned int parameter)
{
    if (dwCalls < MAXWORDS) {
        dwOpcodes[dwCalls] = opcode;
        dwValues[dwCalls] = parameter;
    }
    dwCalls++;
}

void NitroPort_Gx3dClearStackError(void)
{
    clearCalls++;
}

void NitroPort_Gx3dSyncResults(void)
{
    syncCalls++;
}

/* ---- stubs for g3x.o imports that the checks never exercise ---- */
u32 GXi_DmaId;

void GXi_NopClearFifo128_(void *reg)
{
    (void)reg;
}

void MI_Copy32B(const void *src, void *dst)
{
    (void)src;
    (void)dst;
}

void MI_Copy36B(const void *pSrc, void *pDst)
{
    (void)pSrc;
    (void)pDst;
}

void MI_Copy64B(const void *pSrc, void *pDst)
{
    (void)pSrc;
    (void)pDst;
}

void MI_DmaFill32(u32 dmaNo, void *dest, u32 data, u32 size)
{
    (void)dmaNo;
    (void)dest;
    (void)data;
    (void)size;
}

void MI_DmaFill32Async(u32 dmaNo, void *dest, u32 data, u32 size, MIDmaCallback callback,
                       void *arg)
{
    (void)dmaNo;
    (void)dest;
    (void)data;
    (void)size;
    (void)callback;
    (void)arg;
}

void MIi_CpuClear32(u32 data, void *destp, u32 size)
{
    u32 n = size / sizeof(u32);
    u32 *dst = (u32 *)destp;
    u32 i;

    for (i = 0; i < n; i++) {
        dst[i] = data;
    }
}

void MIi_CpuCopy16(const void *srcp, void *destp, u32 size)
{
    const u8 *src = (const u8 *)srcp;
    u8 *dst = (u8 *)destp;
    u32 i;

    for (i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

/* ---- the functions under test ---- */
void G3_LoadMtx44(const MtxFx44 *mtx);
void G3_LoadMtx43(const MtxFx43 *mtx);
void G3_MultMtx43(const MtxFx43 *mtx);
void G3_MultMtx33(const MtxFx33 *mtx);
void G3X_Init(void);
void G3X_Reset(void);

static void MapIoPage(void)
{
    void *p = mmap((void *)(unsigned long)IO_BASE, 0x1000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    if (p != (void *)(unsigned long)IO_BASE) {
        printf("cannot map the I/O page at 0x%08x (got %lx)\n", IO_BASE, (unsigned long)p);
        exit(2);
    }
}

static void ResetStream(void)
{
    dwCalls = 0;
}

static void CheckStream(int wantCalls, int wantOpcode, const u32 *wantWords)
{
    int i;

    CHECK(dwCalls == wantCalls, "stream calls %d, want %d", dwCalls, wantCalls);
    for (i = 0; i < wantCalls && i < MAXWORDS; i++) {
        if (dwOpcodes[i] != wantOpcode) {
            fails++;
            checks++;
            printf("FAIL line %d: word %d opcode 0x%x, want 0x%x\n", __LINE__, i,
                   (unsigned)dwOpcodes[i], (unsigned)wantOpcode);
        }
        if (dwValues[i] != wantWords[i]) {
            fails++;
            checks++;
            printf("FAIL line %d: word %d value 0x%08x, want 0x%08x\n", __LINE__, i,
                   (unsigned)dwValues[i], (unsigned)wantWords[i]);
        }
        checks += 2;
    }
}

static void CheckMatrixLoads(void)
{
    MtxFx44 m44;
    MtxFx43 m43;
    MtxFx33 m33;
    u32 *w44 = (u32 *)&m44._00;
    u32 *w43 = (u32 *)&m43._00;
    u32 *w33 = (u32 *)&m33._00;
    int i;

    for (i = 0; i < 16; i++) {
        w44[i] = 0x1000 + (u32)i;
    }
    for (i = 0; i < 12; i++) {
        w43[i] = 0x2000 + (u32)i;
    }
    for (i = 0; i < 9; i++) {
        w33[i] = 0x3000 + (u32)i;
    }
    ResetStream();
    G3_LoadMtx44(&m44);
    CheckStream(16, G3OP_MTX_LOAD_4x4, w44);
    ResetStream();
    G3_LoadMtx43(&m43);
    CheckStream(12, G3OP_MTX_LOAD_4x3, w43);
    ResetStream();
    G3_MultMtx43(&m43);
    CheckStream(12, G3OP_MTX_MULT_4x3, w43);
    ResetStream();
    G3_MultMtx33(&m33);
    CheckStream(9, G3OP_MTX_MULT_3x3, w33);
}

static void CheckImmInlines(void)
{
    ResetStream();
    G3_MtxMode(GX_MTXMODE_PROJECTION);
    CHECK(dwCalls == 1, "mtxmode calls %d, want 1", dwCalls);
    CHECK(dwOpcodes[0] == G3OP_MTX_MODE, "mtxmode opcode 0x%x", (unsigned)dwOpcodes[0]);
    EQU32("mtxmode param", dwValues[0], GX_PACK_MTXMODE_PARAM(GX_MTXMODE_PROJECTION));

    ResetStream();
    G3_PushMtx();
    CHECK(dwCalls == 1, "push calls %d, want 1", dwCalls);
    CHECK(dwOpcodes[0] == G3OP_MTX_PUSH, "push opcode 0x%x", (unsigned)dwOpcodes[0]);
    EQU32("push param", dwValues[0], 0);

    ResetStream();
    G3_Translate(0x1000, 0x2000, 0x3000);
    CHECK(dwCalls == 3, "translate calls %d, want 3", dwCalls);
    CHECK(dwOpcodes[0] == G3OP_MTX_TRANS && dwOpcodes[1] == G3OP_MTX_TRANS &&
          dwOpcodes[2] == G3OP_MTX_TRANS, "translate opcodes");
    EQU32("translate x", dwValues[0], 0x1000);
    EQU32("translate y", dwValues[1], 0x2000);
    EQU32("translate z", dwValues[2], 0x3000);

    ResetStream();
    G3_Identity();
    CHECK(dwCalls == 1, "identity calls %d, want 1", dwCalls);
    CHECK(dwOpcodes[0] == G3OP_MTX_IDENTITY, "identity opcode 0x%x", (unsigned)dwOpcodes[0]);
}

static void CheckStackErrorClears(void)
{
    /* Each of G3X_Init / G3X_Reset clears the engine-side stack error twice: once
     * directly and once via the matrix-stack init/reset they call. */
    clearCalls = 0;
    G3X_Init();
    CHECK(clearCalls == 2, "init clears %d, want 2", clearCalls);
    G3X_Reset();
    CHECK(clearCalls == 4, "reset clears %d total, want 4", clearCalls);

    /* The DS-named inline from g3x.h clears it too. */
    G3X_ResetMtxStackOverflow();
    CHECK(clearCalls == 5, "inline clears %d total, want 5", clearCalls);
}

static void CheckUncompressedSize(void)
{
    u8 data[8] = {0x78, 0x56, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00};

    EQU32("uncompressed size", MI_GetUncompressedSize(data), 0x123456);
}

int main(void)
{
    MapIoPage();
    CheckMatrixLoads();
    CheckImmInlines();
    CheckStackErrorClears();
    CheckUncompressedSize();
    printf("%s: %d checks, %d failures\n", fails == 0 ? "PASS" : "FAIL", checks, fails);
    return fails != 0;
}
