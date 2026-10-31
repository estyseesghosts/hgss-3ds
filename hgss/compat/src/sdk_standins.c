/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * SDK stand-ins for the vendored port unit tests (3ds_port/nitro/tests/).
 *
 * HGSS's NitroSDK only ships the sources HGSS itself calls: matrix helpers,
 * compressors, backup getters and sync wrappers the game never used were
 * stripped, and a few small functions (CLZ, OS_WaitInterrupt, SND_StartSeq)
 * only exist in other SDK revisions. The tests exercise the ntrtwl API the
 * port was written against, so this file provides the missing C functions.
 * Every definition is weak: if the SDK patch stack later provides the real
 * function, the strong one wins silently at link time.
 *
 * Each function documents the ntrtwl source it matches
 * (https://github.com/ntrtwl/NitroSDK at eaae40f1, the revision
 * hgss/compat/include documents) or, where HGSS's own primitives suffice,
 * the HGSS code it mirrors. Tested by the port's own tests, which carry
 * independent vectors (CRC/MD5/SHA-1 check values, rotation identities,
 * compression round-trips).
 *
 * Compiled into libnitro.a by tools/hgss_nitro.py (hgss/compat/src), with
 * the port's flags (-m32 -malign-double, -fno-short-enums -fsigned-char
 * -fwrapv -fno-strict-aliasing).
 */

#include <nitro.h>
#include <nitro/spi/common/type.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* HGSS-internal entry points this file reuses (declared here, defined in
 * HGSS's SDK sources). */
extern u32 PMi_ReadRegister(u32 reg, u16 *buffer);
extern void TP_RequestAutoSamplingStartAsync(u16 vcount, u16 frequency, TPData *buffer, u16 count);
extern void TP_RequestAutoSamplingStopAsync(void);
extern u32 TP_WaitRawResult(TPData *result);
extern void TP_WaitBusy(u32 commandFlags);
extern u32 TP_CheckBusy(u32 commandFlags);
extern void OS_Halt(void);

/* ------------------------------------------------------------------ */
/* Fixed-point matrices: ntrtwl libraries/fx/src/fx_mtx22.c, fx_mtx33.c */
/* and fx_mtx43.c. The underscore assembly only stores, copies and      */
/* negates, so the C below stores the same values (negated sine as      */
/* 0u minus (u32)sin, as patch 0101 does for HGSS's own functions).     */
/* ------------------------------------------------------------------ */

__attribute__((weak)) void MTX_Identity22(MtxFx22 *pDst)
{
    pDst->_00 = FX32_ONE;
    pDst->_01 = 0;
    pDst->_10 = 0;
    pDst->_11 = FX32_ONE;
}

__attribute__((weak)) void MTX_Scale22(MtxFx22 *pDst, fx32 x, fx32 y)
{
    pDst->_00 = x;
    pDst->_01 = 0;
    pDst->_10 = 0;
    pDst->_11 = y;
}

__attribute__((weak)) void MTX_Rot22(MtxFx22 *pDst, fx32 sinVal, fx32 cosVal)
{
    pDst->_00 = cosVal;
    pDst->_01 = sinVal;
    pDst->_10 = (fx32)(0u - (u32)sinVal);
    pDst->_11 = cosVal;
}

__attribute__((weak)) void MTX_Transpose22(const MtxFx22 *pSrc, MtxFx22 *pDst)
{
    fx32 a00 = pSrc->_00;
    fx32 a01 = pSrc->_01;
    fx32 a10 = pSrc->_10;
    fx32 a11 = pSrc->_11;

    pDst->_00 = a00;
    pDst->_01 = a10;
    pDst->_10 = a01;
    pDst->_11 = a11;
}

__attribute__((weak)) void MTX_Copy33To43(const MtxFx33 *pSrc, MtxFx43 *pDst)
{
    pDst->_00 = pSrc->_00;
    pDst->_01 = pSrc->_01;
    pDst->_02 = pSrc->_02;
    pDst->_10 = pSrc->_10;
    pDst->_11 = pSrc->_11;
    pDst->_12 = pSrc->_12;
    pDst->_20 = pSrc->_20;
    pDst->_21 = pSrc->_21;
    pDst->_22 = pSrc->_22;
    pDst->_30 = 0;
    pDst->_31 = 0;
    pDst->_32 = 0;
}

__attribute__((weak)) void MTX_Copy33To44(const MtxFx33 *pSrc, MtxFx44 *pDst)
{
    pDst->_00 = pSrc->_00;
    pDst->_01 = pSrc->_01;
    pDst->_02 = pSrc->_02;
    pDst->_03 = 0;
    pDst->_10 = pSrc->_10;
    pDst->_11 = pSrc->_11;
    pDst->_12 = pSrc->_12;
    pDst->_13 = 0;
    pDst->_20 = pSrc->_20;
    pDst->_21 = pSrc->_21;
    pDst->_22 = pSrc->_22;
    pDst->_23 = 0;
    pDst->_30 = 0;
    pDst->_31 = 0;
    pDst->_32 = 0;
    pDst->_33 = FX32_ONE;
}

__attribute__((weak)) void MTX_Transpose33(const MtxFx33 *pSrc, MtxFx33 *pDst)
{
    MtxFx33 tmp;

    tmp._00 = pSrc->_00;
    tmp._01 = pSrc->_10;
    tmp._02 = pSrc->_20;
    tmp._10 = pSrc->_01;
    tmp._11 = pSrc->_11;
    tmp._12 = pSrc->_21;
    tmp._20 = pSrc->_02;
    tmp._21 = pSrc->_12;
    tmp._22 = pSrc->_22;
    *pDst = tmp;
}

__attribute__((weak)) void MTX_Scale33(MtxFx33 *pDst, fx32 x, fx32 y, fx32 z)
{
    pDst->_00 = x;
    pDst->_01 = 0;
    pDst->_02 = 0;
    pDst->_10 = 0;
    pDst->_11 = y;
    pDst->_12 = 0;
    pDst->_20 = 0;
    pDst->_21 = 0;
    pDst->_22 = z;
}

/* ntrtwl's MTX_RotAxis33 verbatim (asserts empty under SDK_FINALROM). */
__attribute__((weak)) void MTX_RotAxis33(MtxFx33 *pDst, const VecFx32 *vec, fx32 sinVal, fx32 cosVal)
{
    fx64 t, ss, cc, xx, yy, zz;
    fx32 t01;
    fx32 s2;

    t = (fx64)(FX32_ONE - (fx32)cosVal);
    ss = (fx64)sinVal;
    cc = (fx64)cosVal;
    xx = (fx64)vec->x;
    yy = (fx64)vec->y;
    zz = (fx64)vec->z;

    t01 = (fx32)((xx * xx * t) >> (FX64_SHIFT + FX64_SHIFT));
    pDst->_00 = t01 + cosVal;

    t01 = (fx32)((yy * yy * t) >> (FX64_SHIFT + FX64_SHIFT));
    pDst->_11 = t01 + cosVal;

    t01 = (fx32)((zz * zz * t) >> (FX64_SHIFT + FX64_SHIFT));
    pDst->_22 = t01 + cosVal;

    t01 = (fx32)((t * xx * yy) >> (FX64_SHIFT + FX64_SHIFT));
    s2 = (fx32)((ss * zz) >> FX64_SHIFT);
    pDst->_01 = t01 + s2;
    pDst->_10 = t01 - s2;

    t01 = (fx32)((t * xx * zz) >> (FX64_SHIFT + FX64_SHIFT));
    s2 = (fx32)((ss * yy) >> FX64_SHIFT);
    pDst->_02 = t01 - s2;
    pDst->_20 = t01 + s2;

    t01 = (fx32)((t * yy * zz) >> (FX64_SHIFT + FX64_SHIFT));
    s2 = (fx32)((ss * xx) >> FX64_SHIFT);
    pDst->_12 = t01 + s2;
    pDst->_21 = t01 - s2;
}

__attribute__((weak)) void MTX_Transpose43(const MtxFx43 *pSrc, MtxFx43 *pDst)
{
    MtxFx43 tmp;

    tmp._00 = pSrc->_00;
    tmp._01 = pSrc->_10;
    tmp._02 = pSrc->_20;
    tmp._10 = pSrc->_01;
    tmp._11 = pSrc->_11;
    tmp._12 = pSrc->_21;
    tmp._20 = pSrc->_02;
    tmp._21 = pSrc->_12;
    tmp._22 = pSrc->_22;
    tmp._30 = 0;
    tmp._31 = 0;
    tmp._32 = 0;
    *pDst = tmp;
}

__attribute__((weak)) void MTX_RotZ43(MtxFx43 *pDst, fx32 sinVal, fx32 cosVal)
{
    pDst->_00 = cosVal;
    pDst->_01 = sinVal;
    pDst->_02 = 0;
    pDst->_10 = (fx32)(0u - (u32)sinVal);
    pDst->_11 = cosVal;
    pDst->_12 = 0;
    pDst->_20 = 0;
    pDst->_21 = 0;
    pDst->_22 = FX32_ONE;
    pDst->_30 = 0;
    pDst->_31 = 0;
    pDst->_32 = 0;
}

/* The 4x4 underscore bodies have no HGSS source at all (only the inline
 * wrappers in fx_mtx44.h); ntrtwl libraries/fx/src/fx_mtx44.c stores the
 * same values. */
__attribute__((weak)) void MTX_Scale44_(MtxFx44 *pDst, fx32 x, fx32 y, fx32 z)
{
    pDst->_00 = x;
    pDst->_01 = 0;
    pDst->_02 = 0;
    pDst->_03 = 0;
    pDst->_10 = 0;
    pDst->_11 = y;
    pDst->_12 = 0;
    pDst->_13 = 0;
    pDst->_20 = 0;
    pDst->_21 = 0;
    pDst->_22 = z;
    pDst->_23 = 0;
    pDst->_30 = 0;
    pDst->_31 = 0;
    pDst->_32 = 0;
    pDst->_33 = FX32_ONE;
}

__attribute__((weak)) void MTX_Transpose44_(const MtxFx44 *pSrc, MtxFx44 *pDst)
{
    MtxFx44 tmp;
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            tmp.m[i][j] = pSrc->m[j][i];
        }
    }
    *pDst = tmp;
}

__attribute__((weak)) void MTX_Copy44To33_(const MtxFx44 *pSrc, MtxFx33 *pDst)
{
    pDst->_00 = pSrc->_00;
    pDst->_01 = pSrc->_01;
    pDst->_02 = pSrc->_02;
    pDst->_10 = pSrc->_10;
    pDst->_11 = pSrc->_11;
    pDst->_12 = pSrc->_12;
    pDst->_20 = pSrc->_20;
    pDst->_21 = pSrc->_21;
    pDst->_22 = pSrc->_22;
}

/* ------------------------------------------------------------------ */
/* MATH: ntrtwl libraries/math/src/math.c (portable C fallback).        */
/* ------------------------------------------------------------------ */

__attribute__((weak)) u32 MATH_CountLeadingZerosFunc(u32 x)
{
    u32 y;
    u32 n = 32;

    y = x >> 16;
    if (y != 0) {
        n -= 16;
        x = y;
    }
    y = x >> 8;
    if (y != 0) {
        n -= 8;
        x = y;
    }
    y = x >> 4;
    if (y != 0) {
        n -= 4;
        x = y;
    }
    y = x >> 2;
    if (y != 0) {
        n -= 2;
        x = y;
    }
    y = x >> 1;
    if (y != 0) {
        n -= 2;
    } else {
        n -= x;
    }

    return n;
}

__attribute__((weak)) u32 MATH_CountLeadingZerosInline(u32 x)
{
    return MATH_CountLeadingZerosFunc(x);
}

/* ------------------------------------------------------------------ */
/* MI software DMA: HGSS's own mi_dma.c pattern (wait, set params, wait) */
/* with the control words ntrtwl's MI_CNT macros produce.               */
/* ------------------------------------------------------------------ */

__attribute__((weak)) void MI_DmaFill16(u32 dmaNo, void *dest, u16 data, u32 size)
{
    vu32 *dmaCntp;

    if (!size) {
        return;
    }

    MIi_WAIT_BEFOREDMA(dmaCntp, dmaNo);

    MIi_DmaSetParams_wait_src32(dmaNo, data, (u32)dest,
                                (MI_DMA_ENABLE | MI_DMA_16BIT_BUS | MI_DMA_SRC_FIX |
                                 MI_DMA_DEST_INC | ((size) / 2)));

    do {
        while (*dmaCntp & REG_MI_DMA0CNT_E_MASK) {}
    } while (0);
}

__attribute__((weak)) void MI_DmaSend32(u32 dmaNo, const void *src, volatile void *dest, u32 size)
{
    vu32 *dmaCntp;

    MIi_CheckDma0SourceAddress(dmaNo, (u32)src, size, MI_DMA_SRC_INC);

    if (!size) {
        return;
    }

    MIi_WAIT_BEFOREDMA(dmaCntp, dmaNo);

    MIi_DmaSetParams_wait(dmaNo, (u32)src, (u32)dest,
                          (MI_DMA_ENABLE | MI_DMA_32BIT_BUS | MI_DMA_SRC_INC |
                           MI_DMA_DEST_FIX | ((size) / 4)));

    do {
        while (*dmaCntp & REG_MI_DMA0CNT_E_MASK) {}
    } while (0);
}

__attribute__((weak)) BOOL MI_IsDmaBusy(u32 dmaNo)
{
    vu32 *dmaCntp = &((vu32 *)REG_DMA0SAD_ADDR)[dmaNo * 3 + 2];

    return (BOOL)((*dmaCntp & REG_MI_DMA0CNT_E_MASK) != 0);
}

__attribute__((weak)) void MI_VBlankDmaCopy32Async(u32 dmaNo, const void *src, void *dest, u32 size,
                                                   MIDmaCallback callback, void *arg)
{
    MIi_CheckAnotherAutoDMA(dmaNo, MI_DMA_TIMING_V_BLANK);
    MIi_CheckDma0SourceAddress(dmaNo, (u32)src, size, MI_DMA_SRC_INC);

    if (size == 0) {
        MIi_CallCallback(callback, arg);
    } else {
        MI_WaitDma(dmaNo);
        if (callback) {
            OSi_EnterDmaCallback(dmaNo, callback, arg);
            MIi_DmaSetParams(dmaNo, (u32)src, (u32)dest,
                             (MI_DMA_ENABLE | MI_DMA_IF_ENABLE | MI_DMA_TIMING_V_BLANK |
                              MI_DMA_32BIT_BUS | MI_DMA_SRC_INC | MI_DMA_DEST_INC | ((size) / 4)));
        } else {
            MIi_DmaSetParams(dmaNo, (u32)src, (u32)dest,
                             (MI_DMA_ENABLE | MI_DMA_TIMING_V_BLANK | MI_DMA_32BIT_BUS |
                              MI_DMA_SRC_INC | MI_DMA_DEST_INC | ((size) / 4)));
        }
    }
}

/* ------------------------------------------------------------------ */
/* OS: ntrtwl libraries/os/src/os_system.c. OS_Halt is HGSS's own       */
/* (os_terminate_proc.c, C under SDK_PORT_3DS): on the port it advances */
/* one frame through NitroPort_Halt.                                    */
/* ------------------------------------------------------------------ */

__attribute__((weak)) void OS_WaitInterrupt(BOOL clear, OSIrqMask irqFlags)
{
    OSIntrMode cpsrIrq = OS_DisableInterrupts();
    BOOL ime = OS_EnableIrq();

    if (clear) {
        OS_ClearIrqCheckFlag(irqFlags);
    }

    while (!(OS_GetIrqCheckFlag() & irqFlags)) {
        OS_Halt();
        (void)OS_EnableInterrupts();
        (void)OS_DisableInterrupts();
    }

    OS_ClearIrqCheckFlag(irqFlags);
    (void)OS_RestoreIrq(ime);
    (void)OS_RestoreInterrupts(cpsrIrq);
}

/* ------------------------------------------------------------------ */
/* PM: ntrtwl libraries/spi/src/pm.c, over HGSS's PMi_ReadRegister. The */
/* port's virtual PM keeps the status register's VDET bit clear, so the */
/* battery reads high, as the test expects.                             */
/* ------------------------------------------------------------------ */

__attribute__((weak)) u32 PM_GetBattery(PMBattery *batteryBuf)
{
    u16 reg;
    u32 result = PMi_ReadRegister(REG_PMIC_STAT_ADDR, &reg);

    if (result == PM_SUCCESS && batteryBuf != NULL) {
        *batteryBuf = (PMBattery)((reg & PMIC_STAT_VDET) ? PM_BATTERY_LOW : PM_BATTERY_HIGH);
    }

    return result;
}

/* ------------------------------------------------------------------ */
/* TP: synchronous wrappers over HGSS's async primitives (tp.c).        */
/* TP_COMMAND_SAMPLING/AUTO_START/AUTO_STOP are 0/1/2 there.            */
/* ------------------------------------------------------------------ */

__attribute__((weak)) u32 TP_RequestCalibratedSampling(TPData *result)
{
    TPData raw;
    u32 error = TP_RequestRawSampling(&raw);

    if (error) {
        return error;
    }
    TP_GetCalibratedPoint(result, &raw);
    return 0;
}

__attribute__((weak)) u32 TP_RequestAutoSamplingStart(u16 vcount, u16 frequency, TPData *buffer,
                                                      u16 count)
{
    TP_RequestAutoSamplingStartAsync(vcount, frequency, buffer, count);
    TP_WaitBusy(1u << 1);
    return TP_CheckBusy(1u << 1);
}

__attribute__((weak)) u32 TP_RequestAutoSamplingStop(void)
{
    TP_RequestAutoSamplingStopAsync();
    TP_WaitBusy(1u << 2);
    return TP_CheckBusy(1u << 2);
}

__attribute__((weak)) void TP_GetLatestCalibratedPointInAuto(TPData *result)
{
    TPData raw;

    TP_GetLatestRawPointInAuto(&raw);
    TP_GetCalibratedPoint(result, &raw);
}

/* ------------------------------------------------------------------ */
/* CARD: ntrtwl's geometry getters (the spec fields HGSS's own          */
/* CARD_GetBackupSectorSize reads) and SND: the start-sequence command  */
/* HGSS's snd_interface.c left commented out.                           */
/* ------------------------------------------------------------------ */

__attribute__((weak)) u32 CARD_GetBackupTotalSize(void)
{
    extern CARDiCommon cardi_common;

    return cardi_common.cmd->spec.total_size;
}

__attribute__((weak)) u32 CARD_GetBackupPageSize(void)
{
    extern CARDiCommon cardi_common;
    u32 page = cardi_common.cmd->spec.page_size;

    /* HGSS's CARDi_IdentifyBackupCore fills a private Cmd struct whose fields
     * are shifted one slot against ntrtwl's public CARDiCommandArg.spec: the
     * flash page size (256) and the EEPROM/FRAM page sizes land in the slot the
     * public header calls addr_width, and spec.page_size (the slot ntrtwl uses)
     * is left zero. HGSS has no CARD_GetBackupPageSize, so nothing on the DS
     * ever read it. Return whichever slot this identification filled. */
    return page != 0 ? page : cardi_common.cmd->spec.addr_width;
}

__attribute__((weak)) void SND_StartSeq(s32 player, const void *seqBasePtr, u32 seqOffset,
                                       struct SNDBankData *bankData)
{
    extern void PushCommand_impl(s32 id, u32 par1, u32 par2, u32 par3, u32 par4);

    PushCommand_impl(0x00 /* SND_CMD_START_SEQ, flat SND_command.h */, (u32)player,
                     (u32)seqBasePtr, seqOffset, (u32)bankData);
}

/* ------------------------------------------------------------------ */
/* MSL: lib/include/assert.h (MSL's assert) expands to this. The port  */
/* force-includes that header for third-party code (stb_image.h in     */
/* port_wideart.c calls assert without including anything), and MSL_C  */
/* is not compiled into the port, so the handler lives here. It aborts */
/* like the DS's MSL does instead of silently doing nothing.           */
/* ------------------------------------------------------------------ */

__attribute__((weak, noreturn)) void __msl_assertion_failed(const char *expr, const char *file,
                                                            const char *func, int line)
{
    fprintf(stderr, "Assertion (%s) failed in \"%s\", function \"%s\", line %d\n",
            expr, file, func, line);
    abort();
}

/* ------------------------------------------------------------------ */
/* FS/STD: FS_End reverses HGSS's FS_Init; STD_TSNPrintf forwards to    */
/* HGSS's STD_TVSNPrintf (ntrtwl's is a real function too).             */
/* ------------------------------------------------------------------ */

__attribute__((weak)) void FS_End(void)
{
    extern BOOL is_init;

    is_init = FALSE;
}

__attribute__((weak)) int STD_TSNPrintf(char *dst, size_t len, const char *fmt, ...)
{
    va_list vlist;
    int result;

    va_start(vlist, fmt);
    result = STD_TVSNPrintf(dst, (u32)len, fmt, vlist);
    va_end(vlist);
    return result;
}

/* ------------------------------------------------------------------ */
/* MI compressors: ntrtwl libraries/mi/src/mi_compress.c. HGSS's SDK    */
/* ships only MI_CompressLZImpl (used by the LZ/LZEx inlines in         */
/* nitro/mi/compress.h); RL and Huffman are needed for the round-trip   */
/* tests against HGSS's own MI_Uncompress functions. SDK asserts are    */
/* dropped (empty under SDK_FINALROM).                                  */
/* ------------------------------------------------------------------ */

__attribute__((weak)) u32 MI_CompressRL(const u8 *srcp, u32 size, u8 *dstp)
{
    u32 RLDstCount;
    u32 RLSrcCount;
    u8 RLCompFlag;
    u8 runLength;
    u8 rawDataLength;
    u32 i;

    const u8 *startp;

    *(u32 *)dstp = size << 8 | MI_COMPRESSION_RL;
    RLDstCount = 4;
    RLSrcCount = 0;
    rawDataLength = 0;
    RLCompFlag = 0;

    while (RLSrcCount < size) {
        startp = &srcp[RLSrcCount];

        for (i = 0; i < 128; i++) {
            if (RLSrcCount + rawDataLength >= size) {
                rawDataLength = (u8)(size - RLSrcCount);
                break;
            }

            if (RLSrcCount + rawDataLength + 2 < size) {
                if (startp[i] == startp[i + 1] && startp[i] == startp[i + 2]) {
                    RLCompFlag = 1;
                    break;
                }
            }

            rawDataLength++;
        }

        if (rawDataLength) {
            if (RLDstCount + rawDataLength + 1 >= size) {
                return 0;
            }

            dstp[RLDstCount++] = (u8)(rawDataLength - 1);

            for (i = 0; i < rawDataLength; i++) {
                dstp[RLDstCount++] = srcp[RLSrcCount++];
            }

            rawDataLength = 0;
        }

        if (RLCompFlag) {
            runLength = 3;
            for (i = 3; i < 128 + 2; i++) {

                if (RLSrcCount + runLength >= size) {
                    runLength = (u8)(size - RLSrcCount);
                    break;
                }

                if (srcp[RLSrcCount] != srcp[RLSrcCount + runLength]) {
                    break;
                }

                runLength++;
            }

            if (RLDstCount + 2 >= size) {
                return 0;
            }

            dstp[RLDstCount++] = (u8)(0x80 | (runLength - 3));
            dstp[RLDstCount++] = srcp[RLSrcCount];

            RLSrcCount += runLength;
            RLCompFlag = 0;
        }
    }

    i = 0;
    while ((RLDstCount + i) & 0x3) {
        dstp[RLDstCount + i] = 0;
        i++;
    }

    return RLDstCount;
}

typedef struct {
    u32 Freq;
    u16 No;
    s16 PaNo;
    s16 ChNo[2];
    u16 PaDepth;
    u16 LeafDepth;
    u32 HuffCode;
    u8 Bit;
    u8 _padding;
    u16 HWord;
} HgssHuffData;

typedef struct {
    u8 leftOffsetNeed;
    u8 rightOffsetNeed;
    u16 leftNodeNo;
    u16 rightNodeNo;
} HgssHuffTreeCtrlData;

typedef struct {
    HgssHuffData HuffTable[512];
    u8 HuffTree[256 * 2];
    HgssHuffTreeCtrlData HuffTreeCtrl[256];
} HgssHuffWork;

static HgssHuffData *sHgssHuffTable;
static const HgssHuffData sHgssHuffTableInitData = { 0, 0, 0, {-1, -1}, 0, 0, 0, 0, 0 };
static u8 sHgssHuffTreeTop;
static u8 *sHgssHuffTree;
static HgssHuffTreeCtrlData *sHgssHuffTreeCtrl;
static const HgssHuffTreeCtrlData sHgssHuffTreeCtrlInitData = { 1, 1, 0, 0 };
static u16 sHgssHuffDataNum;

static void HgssHuffUpdateParentDepth(u16 leftNo, u16 rightNo)
{
    sHgssHuffTable[leftNo].PaDepth++;
    sHgssHuffTable[rightNo].PaDepth++;

    if (sHgssHuffTable[leftNo].LeafDepth != 0) {
        HgssHuffUpdateParentDepth((u16)sHgssHuffTable[leftNo].ChNo[0],
                                  (u16)sHgssHuffTable[leftNo].ChNo[1]);
    }

    if (sHgssHuffTable[rightNo].LeafDepth != 0) {
        HgssHuffUpdateParentDepth((u16)sHgssHuffTable[rightNo].ChNo[0],
                                  (u16)sHgssHuffTable[rightNo].ChNo[1]);
    }
}

static void HgssHuffMakeCode(u16 nodeNo, u32 paHuffCode)
{
    sHgssHuffTable[nodeNo].HuffCode = (paHuffCode << 1) | sHgssHuffTable[nodeNo].Bit;

    if (sHgssHuffTable[nodeNo].LeafDepth != 0) {
        HgssHuffMakeCode((u16)sHgssHuffTable[nodeNo].ChNo[0], sHgssHuffTable[nodeNo].HuffCode);
        HgssHuffMakeCode((u16)sHgssHuffTable[nodeNo].ChNo[1], sHgssHuffTable[nodeNo].HuffCode);
    }
}

static u8 HgssHuffCountHWord(u16 nodeNo)
{
    u8 leftHWord, rightHWord;

    switch (sHgssHuffTable[nodeNo].LeafDepth) {
    case 0:
        return 0;
    case 1:
        leftHWord = rightHWord = 0;
        break;
    default:
        leftHWord = HgssHuffCountHWord((u16)sHgssHuffTable[nodeNo].ChNo[0]);
        rightHWord = HgssHuffCountHWord((u16)sHgssHuffTable[nodeNo].ChNo[1]);
        break;
    }

    sHgssHuffTable[nodeNo].HWord = (u16)(leftHWord + rightHWord + 1);
    return (u8)(leftHWord + rightHWord + 1);
}

static u8 HgssHuffRemainingNodeCanSetOffset(u8 costHWord)
{
    u8 i;
    s16 capacity;

    capacity = (s16)(64 - costHWord);

    for (i = 0; i < sHgssHuffTreeTop; i++) {
        if (sHgssHuffTreeCtrl[i].leftOffsetNeed) {
            if ((sHgssHuffTreeTop - i) <= capacity) {
                capacity--;
            } else {
                return 0;
            }
        }

        if (sHgssHuffTreeCtrl[i].rightOffsetNeed) {
            if ((sHgssHuffTreeTop - i) <= capacity) {
                capacity--;
            } else {
                return 0;
            }
        }
    }

    return 1;
}

static void HgssHuffSetOneNodeOffset(u16 huffTreeNo, u8 rightNodeFlag)
{
    u16 nodeNo;
    u8 offsetData = 0;

    if (rightNodeFlag) {
        nodeNo = sHgssHuffTreeCtrl[huffTreeNo].rightNodeNo;
        sHgssHuffTreeCtrl[huffTreeNo].rightOffsetNeed = 0;
    } else {
        nodeNo = sHgssHuffTreeCtrl[huffTreeNo].leftNodeNo;
        sHgssHuffTreeCtrl[huffTreeNo].leftOffsetNeed = 0;
    }

    if (sHgssHuffTable[sHgssHuffTable[nodeNo].ChNo[0]].LeafDepth == 0) {
        offsetData |= 0x80;
        sHgssHuffTree[sHgssHuffTreeTop * 2 + 0] = (u8)sHgssHuffTable[nodeNo].ChNo[0];
        sHgssHuffTreeCtrl[sHgssHuffTreeTop].leftNodeNo = (u8)sHgssHuffTable[nodeNo].ChNo[0];
        sHgssHuffTreeCtrl[sHgssHuffTreeTop].leftOffsetNeed = 0;
    } else {
        sHgssHuffTreeCtrl[sHgssHuffTreeTop].leftNodeNo = (u16)sHgssHuffTable[nodeNo].ChNo[0];
    }

    if (sHgssHuffTable[sHgssHuffTable[nodeNo].ChNo[1]].LeafDepth == 0) {
        offsetData |= 0x40;
        sHgssHuffTree[sHgssHuffTreeTop * 2 + 1] = (u8)sHgssHuffTable[nodeNo].ChNo[1];
        sHgssHuffTreeCtrl[sHgssHuffTreeTop].rightNodeNo = (u8)sHgssHuffTable[nodeNo].ChNo[1];
        sHgssHuffTreeCtrl[sHgssHuffTreeTop].rightOffsetNeed = 0;
    } else {
        sHgssHuffTreeCtrl[sHgssHuffTreeTop].rightNodeNo = (u16)sHgssHuffTable[nodeNo].ChNo[1];
    }

    offsetData |= (u8)(sHgssHuffTreeTop - huffTreeNo - 1);

    sHgssHuffTree[huffTreeNo * 2 + rightNodeFlag] = offsetData;
    sHgssHuffTreeTop++;
}

static void HgssHuffMakeSubsetHuffTree(u16 huffTreeNo, u8 rightNodeFlag)
{
    u8 i;

    i = sHgssHuffTreeTop;
    HgssHuffSetOneNodeOffset(huffTreeNo, rightNodeFlag);

    if (rightNodeFlag) {
        sHgssHuffTreeCtrl[huffTreeNo].rightOffsetNeed = 0;
    } else {
        sHgssHuffTreeCtrl[huffTreeNo].leftOffsetNeed = 0;
    }

    while (i < sHgssHuffTreeTop) {
        if (sHgssHuffTreeCtrl[i].leftOffsetNeed) {
            HgssHuffSetOneNodeOffset(i, 0);
            sHgssHuffTreeCtrl[i].leftOffsetNeed = 0;
        }

        if (sHgssHuffTreeCtrl[i].rightOffsetNeed) {
            HgssHuffSetOneNodeOffset(i, 1);
            sHgssHuffTreeCtrl[i].rightOffsetNeed = 0;
        }

        i++;
    }
}

static void HgssHuffMakeHuffTree(u16 rootNo)
{
    s16 i;
    u16 tmp;
    s16 costHWord, tmpCostHWord;
    s16 costOffsetNeed, tmpCostOffsetNeed;
    s16 costMaxKey, costMaxRightFlag;
    u8 offsetNeedNum;
    u8 tmpKey, tmpRightFlag;

    for (i = 0; i < 256; i++) {
        sHgssHuffTree[i * 2] = sHgssHuffTree[i * 2 + 1] = 0;
    }

    for (i = 0; i < 256; i++) {
        sHgssHuffTreeCtrl[i] = sHgssHuffTreeCtrlInitData;
    }

    sHgssHuffTreeTop = 1;
    costOffsetNeed = 0;

    sHgssHuffTreeCtrl[0].leftOffsetNeed = 0;
    sHgssHuffTreeCtrl[0].rightNodeNo = rootNo;

    while (1) {
        offsetNeedNum = 0;
        for (i = 0; i < sHgssHuffTreeTop; i++) {
            if (sHgssHuffTreeCtrl[i].leftOffsetNeed) {
                offsetNeedNum++;
            }
            if (sHgssHuffTreeCtrl[i].rightOffsetNeed) {
                offsetNeedNum++;
            }
        }

        costHWord = -1;
        costMaxKey = -1;

        tmpKey = 0;
        tmpRightFlag = 0;

        for (i = 0; i < sHgssHuffTreeTop; i++) {
            tmpCostOffsetNeed = (u8)(sHgssHuffTreeTop - i);

            if (sHgssHuffTreeCtrl[i].leftOffsetNeed) {
                tmpCostHWord = (s16)sHgssHuffTable[sHgssHuffTreeCtrl[i].leftNodeNo].HWord;

                if ((tmpCostHWord + offsetNeedNum) > 64) {
                    goto leftCostEvaluationEnd;
                }

                if (!HgssHuffRemainingNodeCanSetOffset((u8)tmpCostHWord)) {
                    goto leftCostEvaluationEnd;
                }

                if (tmpCostHWord > costHWord) {
                    costMaxKey = i;
                    costMaxRightFlag = 0;
                } else if ((tmpCostHWord == costHWord) && (tmpCostOffsetNeed > costOffsetNeed)) {
                    costMaxKey = i;
                    costMaxRightFlag = 0;
                }
            }
leftCostEvaluationEnd: {
            }

            if (sHgssHuffTreeCtrl[i].rightOffsetNeed) {
                tmpCostHWord = (s16)sHgssHuffTable[sHgssHuffTreeCtrl[i].rightNodeNo].HWord;

                if ((tmpCostHWord + offsetNeedNum) > 64) {
                    goto rightCostEvaluationEnd;
                }

                if (!(HgssHuffRemainingNodeCanSetOffset((u8)tmpCostHWord))) {
                    goto rightCostEvaluationEnd;
                }

                if (tmpCostHWord > costHWord) {
                    costMaxKey = i;
                    costMaxRightFlag = 1;
                } else if ((tmpCostHWord == costHWord) && (tmpCostOffsetNeed > costOffsetNeed)) {
                    costMaxKey = i;
                    costMaxRightFlag = 1;
                }
            }
rightCostEvaluationEnd: {
            }
        }

        if (costMaxKey >= 0) {
            HgssHuffMakeSubsetHuffTree((u8)costMaxKey, (u8)costMaxRightFlag);
            goto nextTreeMaking;
        } else {
            for (i = 0; i < sHgssHuffTreeTop; i++) {
                tmp = 0;
                tmpRightFlag = 0;

                if (sHgssHuffTreeCtrl[i].leftOffsetNeed) {
                    tmp = sHgssHuffTable[sHgssHuffTreeCtrl[i].leftNodeNo].HWord;
                }

                if (sHgssHuffTreeCtrl[i].rightOffsetNeed) {
                    if (sHgssHuffTable[sHgssHuffTreeCtrl[i].rightNodeNo].HWord > tmp) {
                        tmpRightFlag = 1;
                    }
                }

                if ((tmp != 0) || (tmpRightFlag)) {
                    HgssHuffSetOneNodeOffset((u8)i, tmpRightFlag);
                    goto nextTreeMaking;
                }
            }
        }
        return;
nextTreeMaking: {
        }
    }
}

__attribute__((weak)) u32 MI_CompressHuffman(const u8 *srcp, u32 size, u8 *dstp, u8 huffBitSize,
                                            u8 *work)
{
    u32 HuffDstCount;
    u8 tmp;
    u16 tableTop;
    s32 leftNo, rightNo;
    s32 i, ii, iii;
    u8 srcTmp;
    u32 bitStream = 0;
    u32 streamLength = 0;
    u16 rootNo;

    sHgssHuffTable = ((HgssHuffWork *)work)->HuffTable;
    sHgssHuffTree = ((HgssHuffWork *)work)->HuffTree;
    sHgssHuffTreeCtrl = ((HgssHuffWork *)work)->HuffTreeCtrl;

    sHgssHuffDataNum = (u16)(1 << huffBitSize);
    tableTop = sHgssHuffDataNum;

    for (i = 0; i < (s32)(sHgssHuffDataNum * 2); i++) {
        sHgssHuffTable[i] = sHgssHuffTableInitData;
        sHgssHuffTable[i].No = (u16)i;
    }

    if (huffBitSize == 8) {
        for (i = 0; i < (s32)size; i++) {
            sHgssHuffTable[srcp[i]].Freq++;
        }
    } else {
        for (i = 0; i < (s32)size; i++) {
            tmp = (u8)((srcp[i] & 0xf0) >> 4);
            sHgssHuffTable[tmp].Freq++;
            tmp = (u8)(srcp[i] & 0x0f);
            sHgssHuffTable[tmp].Freq++;
        }
    }

    leftNo = rightNo = -1;
    while (1) {
        for (i = 0; i < tableTop; i++) {
            if ((sHgssHuffTable[i].Freq != 0) && (sHgssHuffTable[i].PaNo == 0)) {
                leftNo = i;
                break;
            }
        }

        for (i = (u16)leftNo; i < tableTop; i++) {
            if ((sHgssHuffTable[i].Freq != 0) &&
                (sHgssHuffTable[i].PaNo == 0) && (sHgssHuffTable[i].Freq < sHgssHuffTable[leftNo].Freq)) {
                leftNo = i;
            }
        }

        for (i = 0; i < tableTop; i++) {
            if ((sHgssHuffTable[i].Freq != 0) && (sHgssHuffTable[i].PaNo == 0) && (i != leftNo)) {
                rightNo = i;
                break;
            }
        }

        for (i = (u16)rightNo; i < tableTop; i++) {
            if ((sHgssHuffTable[i].Freq != 0) &&
                (sHgssHuffTable[i].PaNo == 0) &&
                (sHgssHuffTable[i].Freq < sHgssHuffTable[rightNo].Freq) && (i != leftNo)) {
                rightNo = i;
            }
        }

        if (rightNo < 0) {
            if (tableTop == sHgssHuffDataNum) {
                sHgssHuffTable[tableTop].Freq = sHgssHuffTable[leftNo].Freq;
                sHgssHuffTable[tableTop].ChNo[0] = (s16)leftNo;
                sHgssHuffTable[tableTop].ChNo[1] = (s16)leftNo;
                sHgssHuffTable[tableTop].LeafDepth = 1;
                sHgssHuffTable[leftNo].PaNo = (s16)tableTop;
                sHgssHuffTable[leftNo].Bit = 0;
                sHgssHuffTable[leftNo].PaDepth = 1;
            } else {
                tableTop--;
            }

            rootNo = tableTop;
            break;
        }

        sHgssHuffTable[tableTop].Freq = sHgssHuffTable[leftNo].Freq + sHgssHuffTable[rightNo].Freq;
        sHgssHuffTable[tableTop].ChNo[0] = (s16)leftNo;
        sHgssHuffTable[tableTop].ChNo[1] = (s16)rightNo;

        if (sHgssHuffTable[leftNo].LeafDepth > sHgssHuffTable[rightNo].LeafDepth) {
            sHgssHuffTable[tableTop].LeafDepth = (u16)(sHgssHuffTable[leftNo].LeafDepth + 1);
        } else {
            sHgssHuffTable[tableTop].LeafDepth = (u16)(sHgssHuffTable[rightNo].LeafDepth + 1);
        }

        sHgssHuffTable[leftNo].PaNo = sHgssHuffTable[rightNo].PaNo = (s16)tableTop;
        sHgssHuffTable[leftNo].Bit = 0;
        sHgssHuffTable[rightNo].Bit = 1;

        HgssHuffUpdateParentDepth((u16)leftNo, (u16)rightNo);

        tableTop++;
        leftNo = rightNo = -1;
    }

    HgssHuffMakeCode(rootNo, 0x00);
    (void)HgssHuffCountHWord(rootNo);
    HgssHuffMakeHuffTree(rootNo);

    sHgssHuffTree[0] = --sHgssHuffTreeTop;

    *(u32 *)dstp = size << 8 | MI_COMPRESSION_HUFFMAN | huffBitSize;
    HuffDstCount = 4;

    if (HuffDstCount + (sHgssHuffTreeTop + 1) * 2 >= size) {
        return 0;
    }

    for (i = 0; i < (s32)((sHgssHuffTreeTop + 1) * 2); i++) {
        dstp[HuffDstCount++] = ((u8 *)sHgssHuffTree)[i];
    }

    while (HuffDstCount & 0x3) {
        if (HuffDstCount & 0x1) {
            sHgssHuffTreeTop++;
            dstp[4]++;
        }

        dstp[HuffDstCount++] = 0;
    }

    for (i = 0; i < (s32)size; i++) {
        u8 val = srcp[i];
        if (huffBitSize == 8) {
            bitStream = (bitStream << sHgssHuffTable[val].PaDepth) | sHgssHuffTable[val].HuffCode;
            streamLength += sHgssHuffTable[val].PaDepth;

            if (HuffDstCount + (streamLength / 8) >= size) {
                return 0;
            }

            for (ii = 0; ii < (s32)(streamLength / 8); ii++) {
                dstp[HuffDstCount++] = (u8)(bitStream >> (streamLength - (ii + 1) * 8));
            }
            streamLength %= 8;
        } else {
            for (ii = 0; ii < 2; ii++) {
                if (ii) {
                    srcTmp = (u8)(val >> 4);
                } else {
                    srcTmp = (u8)(val & 0x0F);
                }

                bitStream = (bitStream << sHgssHuffTable[srcTmp].PaDepth) | sHgssHuffTable[srcTmp].HuffCode;
                streamLength += sHgssHuffTable[srcTmp].PaDepth;

                if (HuffDstCount + (streamLength / 8) >= size) {
                    return 0;
                }

                for (iii = 0; iii < (s32)(streamLength / 8); iii++) {
                    dstp[HuffDstCount++] = (u8)(bitStream >> (streamLength - (iii + 1) * 8));
                }
                streamLength %= 8;
            }
        }
    }

    if (streamLength != 0) {
        if (HuffDstCount + 1 >= size) {
            return 0;
        }

        dstp[HuffDstCount++] = (u8)(bitStream << (8 - streamLength));
    }

    while (HuffDstCount & 0x3) {
        dstp[HuffDstCount++] = 0;
    }

    for (i = 1 + (sHgssHuffTreeTop + 1) * 2 / 4; i < (s32)(HuffDstCount / 4) + 1; i++) {
        tmp = dstp[i * 4 + 0];

        dstp[i * 4 + 0] = dstp[i * 4 + 3];
        dstp[i * 4 + 3] = tmp;

        tmp = dstp[i * 4 + 1];

        dstp[i * 4 + 1] = dstp[i * 4 + 2];
        dstp[i * 4 + 2] = tmp;
    }

    return HuffDstCount;
}
