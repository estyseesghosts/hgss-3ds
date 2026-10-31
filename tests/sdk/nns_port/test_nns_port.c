/*
 * Host test for the SDK_PORT_3DS NitroSystem hooks (patch 0302): the GXFIFO word in
 * NNS_G3dGeBufferOP_N, the early 3D transfers (NNSi_GfdDoVramTransfer3D) and their skip
 * in NNS_GfdDoVramTransfer, and the hidden park entries in
 * NNS_G2dResetOamManagerBuffer. Built and run by run.sh. Synthetic values only.
 *
 * Data the SDK leaves to the linker (gGeBufferState, gOamBuffers and friends,
 * gVramTransferFuncTable) is defined here with matching types; the real layout puts the
 * named OAM buffers inside gOamBuffers, but the checks below only read what the tested
 * functions write, so independent arrays are equivalent. The g3d_sbc.c stores use the
 * same one-line pattern as the gecom one tested here and are covered by the
 * preprocessor identity proof and the survey compile.
 */
/* No <stdio.h>: the project include path (lib/include) shadows stdarg.h, which breaks the
 * system stdio.h. Output goes through write(1); printf is declared by hand. */
#include <stdint.h>
#include <unistd.h>

#include <nitro.h>
#include <nnsys/g3d_util_internal.h>
#include <nnsys/g2d_internal.h>
#include <nnsys/gfd/VramTransferMan/gfd_VramTransferManager.h>

int printf(const char *, ...);

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EQU32(name, got, want) do { checks++; if ((u32)(got) != (u32)(want)) { fails++; \
    printf("FAIL line %d: %s = 0x%08x, want 0x%08x\n", __LINE__, name, \
           (unsigned)(u32)(got), (unsigned)(u32)(want)); } } while (0)

/* ---- geometry: the port FIFO (stub) and the MI bulk path ---- */
#define MAXWORDS 16
static int fifoCalls;
static u32 fifoWords[MAXWORDS];
static int sendCalls;
static const void *sendSrc;
static u32 sendSize;

void NitroPort_GxFifoWord(unsigned int value)
{
    if (fifoCalls < MAXWORDS) {
        fifoWords[fifoCalls] = value;
    }
    fifoCalls++;
}

void MIi_CpuSend32(const void *srcp, volatile void *destp, u32 size)
{
    sendCalls++;
    sendSrc = srcp;
    (void)destp;
    sendSize = size;
}

/* The geometry command buffer state (BSS: no buffer, unlocked). */
G3dGeBufferState gGeBufferState;

void NNS_G3dGeBufferOP_N(u32 op, const u32 *args, u32 num);

/* ---- VRAM transfers: recording table functions ---- */
static int texCalls, plttCalls, clrCalls, bgCalls;
static void *texSrc, *plttSrc, *clrSrc, *bgSrc;

static void TexTransfer(void *src, u32 dstAddr, u32 szByte)
{
    texCalls++;
    texSrc = src;
    (void)dstAddr;
    (void)szByte;
}

static void PlttTransfer(void *src, u32 dstAddr, u32 szByte)
{
    plttCalls++;
    plttSrc = src;
    (void)dstAddr;
    (void)szByte;
}

static void ClrTransfer(void *src, u32 dstAddr, u32 szByte)
{
    clrCalls++;
    clrSrc = src;
    (void)dstAddr;
    (void)szByte;
}

static void BgTransfer(void *src, u32 dstAddr, u32 szByte)
{
    bgCalls++;
    bgSrc = src;
    (void)dstAddr;
    (void)szByte;
}

void (*const gVramTransferFuncTable[NNS_GFD_DST_MAX])(void *src, u32 dstAddr, u32 szByte) = {
    TexTransfer, PlttTransfer, ClrTransfer, ClrTransfer, BgTransfer,
};

void DC_FlushRange(const void *data, u32 size)
{
    (void)data;
    (void)size;
}

void NNS_GfdInitVramTransferManager(NNSGfdVramTransferTask *pTaskArray, u32 lengthOfArray);
BOOL NNS_GfdRegisterNewVramTransferTask(NNS_GFD_DST_TYPE type, u32 dstAddr, void *pSrc,
                                        u32 szByte);
void NNS_GfdDoVramTransfer(void);
void NNSi_GfdDoVramTransfer3D(void);

/* ---- OAM: shadow buffers and MI stubs ---- */
OamManagerBuffer gOamBuffers[3];
GXOamAttr gOamBufferMainOam[128];
u16 gOamBufferMainAffineOwners[32];
u16 gOamBufferSubOamOwners[128];
GXOamAttr gOamBufferSubOam[128];
u16 gOamBufferSubAffineOwners[32];
u16 gOamBufferSoftOamOwners[128];
GXOamAttr gOamBufferSoftOam[128];
u16 gOamBufferSoftAffineOwners[32];
u16 gOamManagerNextId;
u32 GXi_DmaId;
OamLoadFunc gOamLoadFuncs[3];

static int clear16Calls, clear32Calls, dmaFillCalls;
static u32 lastFillValue;

void MIi_CpuClear16(u16 data, void *destp, u32 size)
{
    u16 n = (u16)(size / sizeof(u16));
    u16 *dst = (u16 *)destp;
    u16 i;

    clear16Calls++;
    for (i = 0; i < n; i++) {
        dst[i] = data;
    }
}

void MIi_CpuClear32(u32 data, void *destp, u32 size)
{
    u32 n = size / sizeof(u32);
    u32 *dst = (u32 *)destp;
    u32 i;

    clear32Calls++;
    for (i = 0; i < n; i++) {
        dst[i] = data;
    }
}

void MI_DmaFill32(u32 dmaNo, void *dest, u32 data, u32 size)
{
    (void)dmaNo;
    (void)dest;
    (void)size;
    dmaFillCalls++;
    lastFillValue = data;
}

void DC_InvalidateRange(void *startAddr, u32 nBytes)
{
    (void)startAddr;
    (void)nBytes;
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

void MIi_CpuCopyFast(const void *srcp, void *destp, u32 size)
{
    const u8 *src = (const u8 *)srcp;
    u8 *dst = (u8 *)destp;
    u32 i;

    for (i = 0; i < size; i++) {
        dst[i] = src[i];
    }
}

void MI_SendGXCommandAsync(u32 dmaNo, const void *src, u32 commandLength, MIDmaCallback callback,
                           void *arg)
{
    (void)dmaNo;
    (void)src;
    (void)commandLength;
    (void)callback;
    (void)arg;
}

void MI_SendGXCommandAsyncFast(u32 dmaNo, const void *src, u32 commandLength,
                               MIDmaCallback callback, void *arg)
{
    (void)dmaNo;
    (void)src;
    (void)commandLength;
    (void)callback;
    (void)arg;
}

void MTX_Copy44To43_(const MtxFx44 *pSrc, MtxFx43 *pDst)
{
    (void)pSrc;
    (void)pDst;
}

u32 G3X_GetClipMtx(void *dst)
{
    (void)dst;
    return 0;
}

u32 G3X_GetVectorMtx(void *dst)
{
    (void)dst;
    return 0;
}

volatile u32 gGeBufferStateIsLocked;

/* GX/GXS load functions used only by untested transfer paths. */
#define LOAD3(name) void name(const void *src, u32 dstAddr, u32 szByte) \
    { (void)src; (void)dstAddr; (void)szByte; }
#define LOAD0(name) void name(void) \
    {}

LOAD0(GX_BeginLoadTex)
LOAD3(GX_LoadTex)
LOAD0(GX_EndLoadTex)
LOAD0(GX_BeginLoadTexPltt)
LOAD3(GX_LoadTexPltt)
LOAD0(GX_EndLoadTexPltt)
LOAD0(GX_BeginLoadClearImage)
LOAD3(GX_LoadClearImageColor)
LOAD3(GX_LoadClearImageDepth)
LOAD0(GX_EndLoadClearImage)
LOAD3(GX_LoadBG0Char)
LOAD3(GX_LoadBG1Char)
LOAD3(GX_LoadBG2Char)
LOAD3(GX_LoadBG3Char)
LOAD3(GX_LoadBG0Scr)
LOAD3(GX_LoadBG1Scr)
LOAD3(GX_LoadBG2Scr)
LOAD3(GX_LoadBG3Scr)
LOAD3(GX_LoadOBJPltt)
LOAD3(GX_LoadBGPltt)
LOAD0(GX_BeginLoadOBJExtPltt)
LOAD3(GX_LoadOBJExtPltt)
LOAD0(GX_EndLoadOBJExtPltt)
LOAD0(GX_BeginLoadBGExtPltt)
LOAD3(GX_LoadBGExtPltt)
LOAD0(GX_EndLoadBGExtPltt)
LOAD3(GX_LoadOAM)
LOAD3(GX_LoadOBJ)
LOAD3(GXS_LoadBG0Char)
LOAD3(GXS_LoadBG1Char)
LOAD3(GXS_LoadBG2Char)
LOAD3(GXS_LoadBG3Char)
LOAD3(GXS_LoadBG0Scr)
LOAD3(GXS_LoadBG1Scr)
LOAD3(GXS_LoadBG2Scr)
LOAD3(GXS_LoadBG3Scr)
LOAD3(GXS_LoadOBJPltt)
LOAD3(GXS_LoadBGPltt)
LOAD0(GXS_BeginLoadOBJExtPltt)
LOAD3(GXS_LoadOBJExtPltt)
LOAD0(GXS_EndLoadOBJExtPltt)
LOAD0(GXS_BeginLoadBGExtPltt)
LOAD3(GXS_LoadBGExtPltt)
LOAD0(GXS_EndLoadBGExtPltt)
LOAD3(GXS_LoadOAM)
LOAD3(GXS_LoadOBJ)

BOOL NNS_G2dGetNewOamManagerInstance(OamManagerInstance *pMan, u16 oamFirst, u16 numOam,
                                    u16 affineFirst, u16 numAffine, int type);
void NNS_G2dResetOamManagerBuffer(OamManagerInstance *pMan);

static void CheckGeBufferOP(void)
{
    u32 args[3] = {0x11111111, 0x22222222, 0x33333333};

    fifoCalls = 0;
    sendCalls = 0;
    NNS_G3dGeBufferOP_N(0x1D001D00, args, 3);
    CHECK(fifoCalls == 1, "fifo calls %d, want 1", fifoCalls);
    EQU32("fifo word", fifoWords[0], 0x1D001D00);
    CHECK(sendCalls == 1, "send calls %d, want 1", sendCalls);
    CHECK(sendSrc == args, "send src mismatch");
    EQU32("send size", sendSize, 3 * sizeof(u32));
}

static NNSGfdVramTransferTask tasks[8];
static u32 texData[16], plttData[8], clrData[4], bgData[16];

static void CheckVram3D(void)
{
    NNS_GfdInitVramTransferManager(tasks, 8);
    CHECK(NNS_GfdRegisterNewVramTransferTask(NNS_GFD_DST_2D_BG0_CHAR_MAIN, 0x40, bgData,
                                             sizeof(bgData)) == TRUE, "reg bg failed");
    CHECK(NNS_GfdRegisterNewVramTransferTask(NNS_GFD_DST_3D_TEX_VRAM, 0x100, texData,
                                             sizeof(texData)) == TRUE, "reg tex failed");
    CHECK(NNS_GfdRegisterNewVramTransferTask(NNS_GFD_DST_3D_TEX_PLTT, 0x20, plttData,
                                             sizeof(plttData)) == TRUE, "reg pltt failed");
    CHECK(NNS_GfdRegisterNewVramTransferTask(NNS_GFD_DST_3D_CLRIMG_COLOR, 0x0, clrData,
                                             sizeof(clrData)) == TRUE, "reg clr failed");

    /* The halt: 3D transfers run, marked done; the 2D one waits. */
    texCalls = plttCalls = clrCalls = bgCalls = 0;
    NNSi_GfdDoVramTransfer3D();
    CHECK(texCalls == 1 && texSrc == texData, "tex not done at halt");
    CHECK(plttCalls == 1 && plttSrc == plttData, "pltt not done at halt");
    CHECK(clrCalls == 1 && clrSrc == clrData, "clr not done at halt");
    CHECK(bgCalls == 0, "bg done at halt, want 0");

    /* The V-blank: the 2D one runs; the 3D ones are not done again. */
    NNS_GfdDoVramTransfer();
    CHECK(bgCalls == 1 && bgSrc == bgData, "bg not done at vblank");
    CHECK(texCalls == 1, "tex redone at vblank");
    CHECK(plttCalls == 1, "pltt redone at vblank");
    CHECK(clrCalls == 1, "clr redone at vblank");

    /* Drained: the queue takes new tasks. */
    CHECK(NNS_GfdRegisterNewVramTransferTask(NNS_GFD_DST_3D_TEX_VRAM, 0x180, texData,
                                             sizeof(texData)) == TRUE, "re-reg failed");
    NNS_GfdDoVramTransfer();
    CHECK(texCalls == 2, "tex not done without halt");
}

static void CheckOamReset(void)
{
    OamManagerInstance man;
    GXOamAttr *buf;
    int i;

    for (i = 0; i < 128; i++) {
        gOamBuffers[0].oamOwners[i] = OAM_MANAGER_NO_OWNER;
    }
    gOamManagerNextId = 0;
    GXi_DmaId = (u32)-1;
    CHECK(NNS_G2dGetNewOamManagerInstance(&man, 0, 4, 0, 0, 0) == TRUE, "oam manager failed");

    /* Slow path writes attr0 of each managed entry. */
    buf = gOamBuffers[0].oamBuffer;
    for (i = 0; i < 4; i++) {
        buf[i].attr0 = 0x1111;
    }
    man.useFastTransfer = FALSE;
    NNS_G2dResetOamManagerBuffer(&man);
    for (i = 0; i < 4; i++) {
        EQU32("oam attr0", buf[i].attr0, 192 | 0x0200);
    }

    /* Fast CPU path fills with the same value. */
    for (i = 0; i < 4; i++) {
        buf[i].attr0 = 0x2222;
    }
    clear32Calls = 0;
    man.useFastTransfer = TRUE;
    NNS_G2dResetOamManagerBuffer(&man);
    CHECK(clear32Calls == 1, "clear32 calls %d, want 1", clear32Calls);
    for (i = 0; i < 4; i++) {
        EQU32("oam fast attr0", buf[i].attr0, 192 | 0x0200);
    }

    /* Fast DMA path passes the same value to the filler. */
    GXi_DmaId = 0;
    dmaFillCalls = 0;
    NNS_G2dResetOamManagerBuffer(&man);
    CHECK(dmaFillCalls == 1, "dma fill calls %d, want 1", dmaFillCalls);
    EQU32("dma fill value", lastFillValue, 192 | 0x0200);
}

int main(void)
{
    CheckGeBufferOP();
    CheckVram3D();
    CheckOamReset();
    printf("%s: %d checks, %d failures\n", fails == 0 ? "PASS" : "FAIL", checks, fails);
    return fails != 0;
}
