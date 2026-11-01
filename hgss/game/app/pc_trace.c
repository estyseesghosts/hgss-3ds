/*
 * --trace-pxi and --trace-irq for the PC runner.
 *
 * The port layer (3ds_port/) is byte-identical to platinum.lock, so tracing
 * goes through the linker's --wrap instead of edits there: the harness links
 * the host program with --wrap=PXI_SendWordByFifo, --wrap=NitroPort_Arm7Reply,
 * --wrap=NitroPort_RaiseIrq and --wrap=OS_WaitIrq, and the functions here run
 * before the real ones. Every call from another object is seen; calls made
 * inside the defining object itself (V-blank and H-blank raise their
 * interrupt from port_os.c) are not, which is why --trace-irq also reports
 * the enabled mask at each OS_WaitIrq: a game waiting for an interrupt that
 * the CPU never enabled shows up there.
 *
 * This file is compiled into the host program only.
 */

#include <stdio.h>

#include <nitro.h>
#include <nitro_port.h>

/* The real functions the linker wraps. */
extern int __real_PXI_SendWordByFifo(int fifotag, u32 data, BOOL err);
extern void __real_NitroPort_Arm7Reply(int tag, u32 data, BOOL err);
extern void __real_NitroPort_RaiseIrq(u32 mask);
extern void __real_OS_WaitIrq(BOOL clear, OSIrqMask irqFlags);

BOOL NitroPort_TracePxiOn;
BOOL NitroPort_TraceIrqOn;

void NitroPort_SetTrace(BOOL pxi, BOOL irq)
{
    NitroPort_TracePxiOn = pxi;
    NitroPort_TraceIrqOn = irq;
}

/* The port's PXI tag names (3ds_port/nitro/src/pxi.c's sTagNames). */
static const char *TagName(int tag)
{
    static const char *const names[32] = {
        [0] = "EX",
        [1] = "USER_0",
        [2] = "USER_1",
        [3] = "SYSTEM",
        [4] = "NVRAM",
        [5] = "RTC",
        [6] = "TOUCHPANEL",
        [7] = "SOUND",
        [8] = "PM",
        [9] = "MIC",
        [10] = "WM",
        [11] = "FS",
        [12] = "OS",
        [13] = "CTRDG",
        [14] = "CARD",
        [15] = "WVR",
        [16] = "CTRDG_Ex",
        [17] = "CTRDG_PHI",
    };

    return tag >= 0 && tag < 32 && names[tag] != NULL ? names[tag] : "?";
}

int __wrap_PXI_SendWordByFifo(int fifotag, u32 data, BOOL err)
{
    if (NitroPort_TracePxiOn) {
        NitroPort_Log("[pxi] -> ARM7 tag %d (%s) data %#lx err %d",
                      fifotag, TagName(fifotag), (unsigned long)data, err ? 1 : 0);
    }
    return __real_PXI_SendWordByFifo(fifotag, data, err);
}

void __wrap_NitroPort_Arm7Reply(int tag, u32 data, BOOL err)
{
    if (NitroPort_TracePxiOn) {
        NitroPort_Log("[pxi] <- ARM7 tag %d (%s) data %#lx err %d",
                      tag, TagName(tag), (unsigned long)data, err ? 1 : 0);
    }
    __real_NitroPort_Arm7Reply(tag, data, err);
}

void __wrap_NitroPort_RaiseIrq(u32 mask)
{
    if (NitroPort_TraceIrqOn) {
        NitroPort_Log("[irq] raise mask %#lx (IE %#lx, IF %#lx, IME %lu)",
                      (unsigned long)mask, (unsigned long)reg_OS_IE,
                      (unsigned long)reg_OS_IF, (unsigned long)reg_OS_IME);
    }
    __real_NitroPort_RaiseIrq(mask);
}

void __wrap_OS_WaitIrq(BOOL clear, OSIrqMask irqFlags)
{
    if (NitroPort_TraceIrqOn) {
        NitroPort_Log("[irq] wait mask %#lx (IE %#lx, IME %lu, IRQ %s)",
                      (unsigned long)irqFlags, (unsigned long)reg_OS_IE,
                      (unsigned long)reg_OS_IME,
                      (OSi_PortCpsr & HW_PSR_IRQ_DISABLE) ? "masked" : "enabled");
    }
    __real_OS_WaitIrq(clear, irqFlags);
}
