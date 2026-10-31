/* Host test for the SDK_PORT_3DS C OS_IrqHandler / OS_IrqHandler_ThreadSwitch
 * (lib/NitroSDK/src/os/os_irqHandler.c). The DS I/O registers are mapped as plain RAM at their
 * real addresses, as the port does. NitroPort_IrqThreadSwitch is stubbed to record call order. */
#include <nitro.h>
#include <sys/mman.h>

int printf(const char *, ...); /* lib/include shadows the system headers; see tests/sdk/fx */

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int log_[16], nlog;
static void rec(int v) { if (nlog < 16) log_[nlog++] = v; }
void NitroPort_IrqThreadSwitch(void) { rec(1000); }

#define H(n) static void h##n(void) { rec(n); }
H(0) H(1) H(2) H(3) H(31)
OSIrqFunction OS_IRQTable[32];

static void setup(u32 ime, u32 ie, u32 iflag) {
    reg_OS_IME = (u16)ime; reg_OS_IE = ie; reg_OS_IF = iflag; nlog = 0;
}

int main(void) {
    void *io = mmap((void *)0x04000000, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (io != (void *)0x04000000) { printf("cannot map the I/O page\n"); return 2; }
    OS_IRQTable[0] = h0; OS_IRQTable[1] = h1; OS_IRQTable[2] = h2; OS_IRQTable[3] = h3; OS_IRQTable[31] = h31;

    setup(0, 0xFF, 0xFF); OS_IrqHandler(0);
    CHECK(nlog == 0 && reg_OS_IF == 0xFF, "IME off: nothing happens, IF untouched");

    setup(1, 0x00, 0xFF); OS_IrqHandler(0);
    CHECK(nlog == 0 && reg_OS_IF == 0xFF, "nothing enabled: nothing happens");

    setup(1, 0x0F, 0xF0); OS_IrqHandler(0);
    CHECK(nlog == 0 && reg_OS_IF == 0xF0, "pending but not enabled: nothing happens");

    setup(1, 0x06, 0x07); OS_IrqHandler(0);
    CHECK(nlog == 2 && log_[0] == 1 && log_[1] == 1000, "lowest enabled+pending (bit 1) runs, then the thread switch");
    CHECK(reg_OS_IF == 0x05, "only the serviced flag is cleared (IF %x)", (unsigned)reg_OS_IF);

    OS_IrqHandler(0);
    CHECK(nlog == 4 && log_[2] == 2 && log_[3] == 1000, "next call services bit 2");
    CHECK(reg_OS_IF == 0x01, "IF after second service %x", (unsigned)reg_OS_IF);

    setup(1, 0x80000001u, 0x80000000u); OS_IrqHandler(0);
    CHECK(nlog == 2 && log_[0] == 31 && log_[1] == 1000 && reg_OS_IF == 0, "bit 31 works");

    setup(1, 0x0F, 0x0F); OS_IrqHandler(0);
    CHECK(log_[0] == 0, "bit 0 has the highest priority");

    setup(1, 0x03, 0x01); nlog = 0; OS_IrqHandler_ThreadSwitch();
    CHECK(nlog == 1 && log_[0] == 1000, "ThreadSwitch calls the port");

    printf("irq_handler: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
