/* Host test for the SDK_PORT_3DS C versions in lib/NitroSDK/src/os/os_system.c.
 * The CPSR lives in OSi_PortCpsr; NitroPort_RaiseIrq is stubbed to count calls. */
#include <nitro.h>

int printf(const char *, ...); /* lib/include shadows the system headers; see tests/sdk/fx */
u32 OSi_PortCpsr;
static int raise_calls, raise_last = -1;
void NitroPort_RaiseIrq(u32 mask) { raise_calls++; raise_last = (int)mask; }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define I HW_PSR_IRQ_DISABLE
#define F HW_PSR_FIQ_DISABLE

int main(void) {
    u32 mode = 0x1F; /* system mode bits, to prove they are preserved */

    /* Disable: returns previous I bit, sets I. */
    OSi_PortCpsr = mode; raise_calls = 0;
    CHECK(OS_DisableInterrupts() == 0, "disable from enabled returns 0");
    CHECK(OSi_PortCpsr == (mode | I), "I set, mode kept");
    CHECK(OS_DisableInterrupts() == I, "disable when already disabled returns I");
    CHECK(raise_calls == 0, "disable never raises");

    /* Enable: returns previous I bit, clears I, raises only on masked->unmasked. */
    OSi_PortCpsr = mode | I; raise_calls = 0;
    CHECK(OS_EnableInterrupts() == I, "enable from disabled returns I");
    CHECK(OSi_PortCpsr == mode, "I cleared, mode kept");
    CHECK(raise_calls == 1 && raise_last == 0, "pending IRQs taken on unmask (mask 0)");
    raise_calls = 0;
    CHECK(OS_EnableInterrupts() == 0, "enable when enabled returns 0");
    CHECK(raise_calls == 0, "no raise when already unmasked");

    /* Restore: sets I from prev only, preserves the rest, raises on masked->unmasked. */
    OSi_PortCpsr = mode | I | F; raise_calls = 0;
    CHECK(OS_RestoreInterrupts(0) == I, "restore(0) returns old I");
    CHECK(OSi_PortCpsr == (mode | F), "restore(0) clears I only, F kept");
    CHECK(raise_calls == 1, "restore to unmasked raises");
    raise_calls = 0;
    CHECK(OS_RestoreInterrupts(I) == 0, "restore(I) returns old I (0)");
    CHECK(OSi_PortCpsr == (mode | F | I), "restore(I) sets I");
    CHECK(raise_calls == 0, "restore to masked does not raise");
    OSi_PortCpsr = mode | I; raise_calls = 0;
    OS_RestoreInterrupts(I);
    CHECK(raise_calls == 0, "masked->masked does not raise");

    /* IrqAndFiq variants use both bits. */
    OSi_PortCpsr = mode; raise_calls = 0;
    CHECK(OS_DisableInterrupts_IrqAndFiq() == 0, "disable I+F from clear returns 0");
    CHECK(OSi_PortCpsr == (mode | I | F), "I and F set");
    CHECK(OS_DisableInterrupts_IrqAndFiq() == (I | F), "returns both bits");
    OSi_PortCpsr = mode | F; /* only F set, I clear */
    CHECK(OS_DisableInterrupts_IrqAndFiq() == F, "returns only the bits that were set");
    OSi_PortCpsr = mode | I | F; raise_calls = 0;
    CHECK(OS_RestoreInterrupts_IrqAndFiq(0) == (I | F), "restore I+F(0) returns old");
    CHECK(OSi_PortCpsr == mode, "both cleared");
    CHECK(raise_calls == 1, "raise on I masked->unmasked");
    OSi_PortCpsr = mode | I | F; raise_calls = 0;
    OS_RestoreInterrupts_IrqAndFiq(I); /* leave IRQ masked, unmask FIQ */
    CHECK(OSi_PortCpsr == (mode | I), "F cleared, I kept");
    CHECK(raise_calls == 0, "IRQ still masked: no raise");

    /* Getters. */
    OSi_PortCpsr = mode | I | F;
    CHECK(OS_GetCpsrIrq() == I, "GetCpsrIrq is the I bit only");
    CHECK((u32)OS_GetProcMode() == (mode & HW_PSR_CPU_MODE_MASK), "GetProcMode is the mode bits");
    OSi_PortCpsr = 0x13;
    CHECK(OS_GetCpsrIrq() == 0, "GetCpsrIrq clear");
    CHECK((u32)OS_GetProcMode() == 0x13, "GetProcMode svc");

    OS_SpinWait(0);
    OS_SpinWait(0xFFFFFFFFu); /* must return immediately */

    printf("os_system: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
