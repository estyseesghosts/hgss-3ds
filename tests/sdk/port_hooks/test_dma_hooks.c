/* Host test for the SDK_PORT_3DS software DMA hooks in lib/NitroSDK/src/mi (mi_dma.c, mi_dma_hblank.c,
 * mi_dma_card.c, mi_dma_gxcommand.c). Each DMA register setup must call NitroPort_DmaStart exactly
 * once, for its channel, after the channel's registers hold the new transfer. The stub plays the
 * port: it records the registers at the call and clears the busy bit, as a finished one-shot does.
 * The channel-0 dummy transfer and the channel-0 source-address check are not used on the port. */
#include "hw_ram.h"

/* Defined in mi_dma_hblank.c, mi_dma_card.c and mi_dma_gxcommand.c; not in the public headers. */
void MI_HBlankDmaCopy32(u32 dmaNo, const void *src, void *dest, u32 size);
void MIi_CardDmaCopy32(u32 dmaNo, const void *src, void *dest, u32 size);
void MI_SendGXCommand(u32 dmaNo, const void *src, u32 commandLength);

/* Stubs for the SDK and port functions the DMA code calls, none of which is under test.
 * os_system.c (the port's CPSR functions, which the DMA code calls to mask IRQs) needs the CPSR. */
u32 OSi_PortCpsr;
void NitroPort_RaiseIrq(u32 mask) {
    (void)mask;
}

static int g_terminated;
void OS_Terminate(void) {
    g_terminated++;
}

static int g_enter_calls;
static u32 g_enter_dma;
void OSi_EnterDmaCallback(u32 dmaNo, void (*callback)(void *arg), void *arg) {
    (void)callback;
    (void)arg;
    g_enter_calls++;
    g_enter_dma = dmaNo;
}

/* The G3X FIFO interrupt helpers, used only by the asynchronous GX send path. */
OSIrqMask OS_EnableIrqMask(OSIrqMask mask) {
    (void)mask;
    return 0;
}
OSIrqMask OS_DisableIrqMask(OSIrqMask mask) {
    (void)mask;
    return 0;
}
OSIrqMask OS_ResetRequestIrqMask(OSIrqMask mask) {
    (void)mask;
    return 0;
}
void OS_SetIrqFunction(OSIrqMask intrBit, OSIrqFunction function) {
    (void)intrBit;
    (void)function;
}
OSIrqFunction OS_GetIrqFunction(OSIrqMask intrBit) {
    (void)intrBit;
    return 0;
}

/* The hook: records the call and the channel's registers as the SDK left them. */
static int g_hook_calls;
static u32 g_hook_chan;
static u32 g_hook_sad, g_hook_dad, g_hook_cnt;
static int g_hook_bad_chan;

void NitroPort_DmaStart(u32 dmaNo) {
    vu32 *regs;
    g_hook_calls++;
    g_hook_chan = dmaNo;
    if (dmaNo >= 4) {
        g_hook_bad_chan++;
        return;
    }
    regs = (vu32 *)((u32)REG_DMA0SAD_ADDR + dmaNo * 12);
    g_hook_sad = regs[0];
    g_hook_dad = regs[1];
    g_hook_cnt = regs[2];
    regs[2] &= ~0x80000000u; /* a one-shot transfer is done: the busy bit clears */
}

#define DMA_REG(ch, word) (*(vu32 *)((u32)REG_DMA0SAD_ADDR + (ch) * 12 + (word) * 4))

static void reset_case(void) {
    int i;
    for (i = 0; i < 12; i++) {
        *(vu32 *)((u32)REG_DMA0SAD_ADDR + i * 4) = 0;
    }
    for (i = 0; i < 4; i++) {
        *(vu32 *)((u32)REG_ADDR_DMA0_CLR_DATA + i * 4) = 0;
    }
    g_hook_calls = 0;
    g_hook_chan = 0xFFFFFFFFu;
    g_hook_sad = g_hook_dad = g_hook_cnt = 0;
    g_hook_bad_chan = 0;
    g_terminated = 0;
    g_enter_calls = 0;
    g_enter_dma = 0xFFFFFFFFu;
}

static void cb(void *arg) {
    (void)arg;
}

int main(void) {
    static u32 src_buf[16], dst_buf[16];
    if (!hw_test_map_io()) {
        printf("cannot map the DS I/O page at 0x04000000\n");
        return 1;
    }

    /* The four setup functions, each on its own channel. */
    reset_case();
    MIi_DmaSetParams(3, 0x02100000, 0x03000000, 0x84000010);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 3);
    CHECK_EQ(g_hook_sad, 0x02100000u);
    CHECK_EQ(g_hook_dad, 0x03000000u);
    CHECK_EQ(g_hook_cnt, 0x84000010u);

    reset_case();
    MIi_DmaSetParams_wait(2, 0x02200000, 0x03100000, 0x84000008);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 2);
    CHECK_EQ(g_hook_sad, 0x02200000u);
    CHECK_EQ(g_hook_cnt, 0x84000008u);

    reset_case();
    MIi_DmaSetParams_noInt(1, 0x02300000, 0x03200000, 0x84000004);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 1);
    CHECK_EQ(g_hook_dad, 0x03200000u);
    CHECK_EQ(g_hook_cnt, 0x84000004u);

    /* Channel 0 through the _wait_noInt variant: the real transfer is started, not a dummy that
     * would replace it (the DS workaround is skipped on the port). */
    reset_case();
    MIi_DmaSetParams_wait_noInt(0, 0x02400000, 0x03300000, 0x84000002);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 0);
    CHECK_EQ(g_hook_sad, 0x02400000u);
    CHECK_EQ(g_hook_dad, 0x03300000u);
    CHECK_EQ(g_hook_cnt, 0x84000002u);
    CHECK_EQ(DMA_REG(0, 0), 0x02400000u);

    /* The public copies and fills. */
    reset_case();
    MI_DmaFill32(3, dst_buf, 0xA5A5A5A5u, 64);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 3);
    CHECK_EQ(g_hook_cnt, 0x85000010u); /* MI_CNT_CLEAR32(64) */

    reset_case();
    MI_DmaCopy32(3, src_buf, dst_buf, 64);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 3);
    CHECK_EQ(g_hook_sad, (u32)src_buf);
    CHECK_EQ(g_hook_dad, (u32)dst_buf);
    CHECK_EQ(g_hook_cnt, 0x84000010u); /* MI_CNT_COPY32(64) */

    /* Channel 0 with a source in the GBA-slot range: the DS check would stop the game; the port
     * must start the copy instead, with the real registers (no dummy). */
    reset_case();
    MI_DmaCopy16(0, (const void *)0x08000000u, dst_buf, 32);
    CHECK_EQ(g_terminated, 0);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 0);
    CHECK_EQ(g_hook_sad, 0x08000000u);
    CHECK_EQ(g_hook_cnt, 0x80000010u); /* MI_CNT_COPY16(32) */
    CHECK_EQ(DMA_REG(0, 2) & 0x80000000u, 0);

    /* Channel 0 I/O-register source: the same check, the same result. */
    reset_case();
    MI_DmaCopy32(0, (const void *)0x04000000u, dst_buf, 16);
    CHECK_EQ(g_terminated, 0);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 0);

    reset_case();
    MI_DmaFill32Async(2, dst_buf, 0x0u, 32, 0, 0);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 2);
    CHECK_EQ(g_enter_calls, 0);

    reset_case();
    MI_DmaCopy32Async(1, src_buf, dst_buf, 32, cb, 0);
    CHECK_EQ(g_enter_calls, 1);
    CHECK_EQ(g_enter_dma, 1);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 1);
    CHECK_EQ(g_hook_cnt & 0x40000000u, 0x40000000u); /* MI_CNT_COPY32_IF: interrupt on finish */

    /* MI_StopDma: a control write; the port's start is a no-op after the enable bit is cleared. */
    reset_case();
    MI_StopDma(2);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 2);
    CHECK_EQ(DMA_REG(2, 2) & 0x80000000u, 0);

    /* Channel 0 stopped: the dummy must not be left busy with nothing to clear it. */
    reset_case();
    MI_StopDma(0);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(DMA_REG(0, 2), 0);

    /* MI_WaitDma: no control register is written on the port, so no start and no dummy. */
    reset_case();
    MI_WaitDma(0);
    CHECK_EQ(g_hook_calls, 0);
    CHECK_EQ(DMA_REG(0, 2), 0);

    /* H-blank copy and card receive go through MIi_DmaSetParams. */
    reset_case();
    MI_HBlankDmaCopy32(1, src_buf, dst_buf, 16);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 1);
    CHECK_EQ(g_hook_cnt, 0x96600004u); /* H-blank timing, 16 bytes */

    reset_case();
    MIi_CardDmaCopy32(3, (const void *)0x02100000u, dst_buf, 4);
    CHECK_EQ(g_hook_calls, 1);
    CHECK_EQ(g_hook_chan, 3);

    /* MI_SendGXCommand sends 1000 bytes in chunks of 472 (118 words): three starts, all channel 3. */
    reset_case();
    MI_SendGXCommand(3, src_buf, 1000);
    CHECK_EQ(g_hook_calls, 3);
    CHECK_EQ(g_hook_bad_chan, 0);
    CHECK_EQ(g_hook_chan, 3);
    CHECK_EQ(g_hook_cnt, 0x84400000u | (56 / 4)); /* the last chunk */

    return hw_test_finish("test_dma_hooks");
}
