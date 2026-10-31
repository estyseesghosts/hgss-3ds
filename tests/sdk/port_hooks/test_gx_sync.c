/* Host test for the SDK_PORT_3DS matrix result sync in lib/NitroSDK/src/gx/g3x.c. The port works out
 * the clip and vector matrix result registers only when the SDK reads them (NitroPort_Gx3dSyncResults),
 * so G3X_GetClipMtx and G3X_GetVectorMtx must sync before they look at GXSTAT's busy bit and copy the
 * registers. The stub sync models the port: it finishes the geometry engine (clears busy) and writes
 * fresh results. A read that happened before the sync sees busy and -1, or stale results. */
#include "hw_ram.h"
#include <nitro/mi/memory.h>

/* Defined in mi_memory.c (assembly in HGSS) and mi_dma.c; g3x.c references them. The tests never reach them. */
void MI_Copy32B(const void *src, void *dst);
void GXi_NopClearFifo128_(void *reg);
void MIi_CpuCopy16(const void *src, void *dst, u32 size);
void MIi_CpuClear32(u32 value, void *dst, u32 size);
void MI_DmaFill32(u32 dmaNo, void *dest, u32 data, u32 size);
void MI_DmaFill32Async(u32 dmaNo, void *dest, u32 data, u32 size, MIDmaCallback callback, void *arg);
u32 GXi_DmaId = (u32)-1;

static int g_unexpected;
void MI_Copy32B(const void *src, void *dst) {
    (void)src;
    (void)dst;
    g_unexpected++;
}
void GXi_NopClearFifo128_(void *reg) {
    (void)reg;
    g_unexpected++;
}
void MIi_CpuCopy16(const void *src, void *dst, u32 size) {
    (void)src;
    (void)dst;
    (void)size;
    g_unexpected++;
}
void MIi_CpuClear32(u32 value, void *dst, u32 size) {
    (void)value;
    (void)dst;
    (void)size;
    g_unexpected++;
}
void MI_DmaFill32(u32 dmaNo, void *dest, u32 data, u32 size) {
    (void)dmaNo;
    (void)dest;
    (void)data;
    (void)size;
    g_unexpected++;
}
void MI_DmaFill32Async(u32 dmaNo, void *dest, u32 data, u32 size, MIDmaCallback callback, void *arg) {
    (void)dmaNo;
    (void)dest;
    (void)data;
    (void)size;
    (void)callback;
    (void)arg;
    g_unexpected++;
}

/* The SDK's register copies, as g3x.c uses them: 64 and 36 bytes from a result register block. */
static void copy_words(const void *src, void *dst, unsigned words) {
    const volatile u32 *s = (const volatile u32 *)src;
    u32 *d = (u32 *)dst;
    unsigned i;
    for (i = 0; i < words; i++) {
        d[i] = s[i];
    }
}
void MI_Copy64B(const void *pSrc, void *pDst) {
    copy_words(pSrc, pDst, 16);
}
void MI_Copy36B(const void *pSrc, void *pDst) {
    copy_words(pSrc, pDst, 9);
}

u32 G3X_GetClipMtx(void *dst);
u32 G3X_GetVectorMtx(void *dst);

#define CLIP_FRESH(i)  (0x1000u + (u32)(i))
#define VEC_FRESH(i)   (0x2000u + (u32)(i))
#define STALE          0x5A5A5A5Au

static int g_sync_calls;

/* The port's geometry engine is done when this runs: busy clears, and the results are current. */
void NitroPort_Gx3dSyncResults(void) {
    unsigned i;
    g_sync_calls++;
    reg_G3X_GXSTAT &= ~REG_G3X_GXSTAT_B_MASK;
    for (i = 0; i < 16; i++) {
        ((vu32 *)&reg_G3X_CLIPMTX_RESULT_0)[i] = CLIP_FRESH(i);
    }
    for (i = 0; i < 9; i++) {
        ((vu32 *)&reg_G3X_VECMTX_RESULT_0)[i] = VEC_FRESH(i);
    }
}

static void set_stale_busy(void) {
    unsigned i;
    reg_G3X_GXSTAT |= REG_G3X_GXSTAT_B_MASK;
    for (i = 0; i < 16; i++) {
        ((vu32 *)&reg_G3X_CLIPMTX_RESULT_0)[i] = STALE;
    }
    for (i = 0; i < 9; i++) {
        ((vu32 *)&reg_G3X_VECMTX_RESULT_0)[i] = STALE;
    }
    g_sync_calls = 0;
}

static int all_equal(const u32 *got, u32 (*want)(unsigned), unsigned n) {
    unsigned i;
    for (i = 0; i < n; i++) {
        if (got[i] != want(i)) {
            return 0;
        }
    }
    return 1;
}
static u32 clip_want(unsigned i) {
    return CLIP_FRESH(i);
}
static u32 vec_want(unsigned i) {
    return VEC_FRESH(i);
}

int main(void) {
    u32 clip[16];
    u32 vec[9];
    if (!hw_test_map_io()) {
        printf("cannot map the DS I/O page at 0x04000000\n");
        return 1;
    }

    /* Busy until the port syncs: the sync must come before the busy check, or the read returns -1. */
    set_stale_busy();
    CHECK_EQ(G3X_GetClipMtx(clip), 0);
    CHECK_EQ(g_sync_calls, 1);
    CHECK(all_equal(clip, clip_want, 16));

    set_stale_busy();
    CHECK_EQ(G3X_GetVectorMtx(vec), 0);
    CHECK_EQ(g_sync_calls, 1);
    CHECK(all_equal(vec, vec_want, 9));

    /* Not busy: the sync still runs first, and the copy shows the fresh results. */
    set_stale_busy();
    reg_G3X_GXSTAT &= ~REG_G3X_GXSTAT_B_MASK;
    CHECK_EQ(G3X_GetClipMtx(clip), 0);
    CHECK_EQ(g_sync_calls, 1);
    CHECK(all_equal(clip, clip_want, 16));

    CHECK_EQ(g_unexpected, 0);
    return hw_test_finish("test_gx_sync");
}
