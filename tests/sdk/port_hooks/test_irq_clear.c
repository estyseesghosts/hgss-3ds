/* Host test for the SDK_PORT_3DS write-1-to-clear emulation of reg_OS_IF in
 * lib/NitroSDK/src/os/os_interrupt.c (OS_ResetRequestIrqMask) and os_tick.c (OS_SetTick).
 * On the DS, a store to IF clears the request bits that are 1 in it. In the port, IF is memory,
 * so a store of a mask would set those bits. reg_OS_IF is the real register address, backed by
 * the page hw_ram.h maps. */
#include "hw_ram.h"

/* os_system.c (the port's CPSR functions) needs these two. */
u32 OSi_PortCpsr;
void NitroPort_RaiseIrq(u32 mask) {
    (void)mask;
}

/* os_tick.c (software timers, patch 0602) calls the port on every timer write and
 * before every tick read. This test never reads a timer, so these only need to exist. */
void NitroPort_TimerSetReload(int id, u16 count) {
    (void)id;
    (void)count;
}
void NitroPort_TimerSetControl(int id, u16 control) {
    (void)id;
    (void)control;
}
void NitroPort_SyncTimers(void) {
}

/* os_interrupt.c and os_tick.c reference the IRQ table, the IRQ stack symbols and the timer
 * reservation. The functions under test never reach them, so these are placeholders of the
 * declared types. */
OSIrqFunction OS_IRQTable[32];
OSIrqCallbackInfo OSi_IrqCallbackInfo[OSi_IRQCALLBACK_NUM];
OSThreadQueue OSi_IrqThreadQueue;
u32 SDK_AUTOLOAD_DTCM_START[4];
void SDK_IRQ_STACKSIZE(void) {
}
void OSi_SetTimerReserved(s32 timerNum) {
    (void)timerNum;
}

int main(void) {
    if (!hw_test_map_io()) {
        printf("cannot map the DS I/O page at 0x04000000\n");
        return 1;
    }

    /* OS_ResetRequestIrqMask: returns the old requests and clears the bits in the mask. */
    reg_OS_IF = 0x0F;
    CHECK_EQ(OS_ResetRequestIrqMask(0x05), 0x0F);
    CHECK_EQ(reg_OS_IF, 0x0A);

    /* Clearing bits that are not requested must not request them (a store would set all of them). */
    reg_OS_IF = 0x0;
    CHECK_EQ(OS_ResetRequestIrqMask(0xFFFF), 0x0);
    CHECK_EQ(reg_OS_IF, 0x0);

    /* Requests outside the mask stay requested. */
    reg_OS_IF = 0x0401;
    CHECK_EQ(OS_ResetRequestIrqMask(0x0001), 0x0401);
    CHECK_EQ(reg_OS_IF, 0x0400);

    /* Bit 31 and the high bits behave the same way. */
    reg_OS_IF = 0x80000020u;
    CHECK_EQ(OS_ResetRequestIrqMask(0x00000020), 0x80000020u);
    CHECK_EQ(reg_OS_IF, 0x80000000u);

    /* OS_SetTick clears only the timer 0 request (OS_IE_TIMER0 = 0x8); a DMA request stays. */
    reg_OS_IF = OS_IE_TIMER0 | OS_IE_DMA0;
    OS_SetTick(0x12345678);
    CHECK_EQ(reg_OS_IF, OS_IE_DMA0);

    /* With no timer 0 request pending, OS_SetTick must not request one. */
    reg_OS_IF = OS_IE_DMA0;
    OS_SetTick(0x00010000);
    CHECK_EQ(reg_OS_IF, OS_IE_DMA0);

    return hw_test_finish("test_irq_clear");
}
