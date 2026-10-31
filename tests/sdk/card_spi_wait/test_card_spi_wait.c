/* Host test for the SDK_PORT_3DS CARD_SpiWait stub (lib/NitroSDK/src/card/card_spi_wait*.c).
 * The MI registers (reg_MI_MCCNT0 at 0x04000000 + 0x1A0, reg_MI_MCD0 at + 0x1A2) live in a PROT_NONE page here.
 * Any access to that page faults; the handler exits 3, so a register access fails run.sh. A forked child proves
 * the page really faults, so a pass means no access happened, not that the guard was missing.
 * The stub must not link anything but OS_GetLockID and the tick, so those are the only stubs needed here. */
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <nitro.h>
#include <nitro/card/card_spi_wait_internal.h>

int printf(const char *, ...); /* lib/include shadows the system headers; see tests/sdk/os_system */

#define MI_PAGE_BASE 0x04000000u

CardSpiWait gCardSpiWait;
u16 gCardSpiLockId = 0xFFFD;

static int lock_calls, tick_calls;

s32 OS_GetLockID(void) {
    lock_calls++;
    return 7;
}

u64 OS_GetTick(void) {
    tick_calls++;
    return 0;
}

static void on_fault(int sig) {
    (void)sig;
    (void)!write(2, "MI register page touched\n", 25);
    _exit(3);
}

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    static u8 buf[16];
    static u8 payload[16];
    int i, ret, nonzero, s1, s2, status;
    pid_t pid;

    if (mmap((void *)MI_PAGE_BASE, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0)
            != (void *)MI_PAGE_BASE) {
        printf("card_spi_wait: could not map the guard page\n");
        return 2;
    }
    signal(SIGSEGV, on_fault);
    signal(SIGBUS, on_fault);

    /* The guard is live: the child reads the register and must die with status 3. */
    pid = fork();
    if (pid == 0) {
        volatile u16 probe;
        close(2);
        probe = reg_MI_MCCNT0;
        (void)probe;
        _exit(0);
    }
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 3, "guard page faults on a register read");
    CHECK((uintptr_t)&reg_MI_MCCNT0 >= MI_PAGE_BASE && (uintptr_t)&reg_MI_MCCNT0 < MI_PAGE_BASE + 4096,
          "reg_MI_MCCNT0 lies in the guard page");

    /* CARD_SpiWaitInit: wait defaults and the lock id, nothing else. */
    gCardSpiWait.dummyWait = gCardSpiWait.writeWait = gCardSpiWait.readWait = 0xEE;
    gCardSpiWait.active = 0;
    gCardSpiLockId = 0xFFFD;
    CARD_SpiWaitInit();
    CHECK(gCardSpiWait.dummyWait == 50 && gCardSpiWait.writeWait == 50 && gCardSpiWait.readWait == 50,
          "init: waits default to 50");
    CHECK(gCardSpiWait.active == 1, "init: active set");
    CHECK(gCardSpiLockId == 7 && lock_calls == 1, "init: lock id taken from OS_GetLockID");
    CARD_SpiWaitInit();
    CHECK(gCardSpiLockId == 7 && lock_calls == 1, "init: lock id kept on a second init");

    /* CARD_Set*WaitCycles: store the settings (the callers use 50, 0 and 7 here). */
    CARD_SetSpiWriteWaitCycles(7);
    CHECK(gCardSpiWait.writeWait == 7, "write wait stored");
    CARD_SetSpiReadWaitCycles(0);
    CHECK(gCardSpiWait.readWait == 0, "read wait stored");
    CARD_SetSpiWriteWaitCycles(50);
    CARD_SetSpiReadWaitCycles(50);
    CHECK(gCardSpiWait.writeWait == 50 && gCardSpiWait.readWait == 50, "waits restored to 50");

    /* CARD_SpiWaitReadRange: a zero-length block, buffer[0] = 0, the rest of the buffer untouched. */
    for (i = 0; i < 16; i++) {
        buf[i] = 0x5A;
    }
    ret = CARD_SpiWaitReadRange(buf);
    CHECK(ret == 0, "read: returns length 0");
    CHECK(buf[0] == 0, "read: buffer[0] is 0");
    CHECK(buf[1] == 0x5A && buf[15] == 0x5A, "read: bytes after buffer[0] untouched");

    /* A poll loop (pokewalker_ir.c:216) sees length 0 every time, so it never takes the packet path. */
    nonzero = 0;
    for (i = 0; i < 1000; i++) {
        if (CARD_SpiWaitReadRange(buf) != 0) {
            nonzero++;
        }
    }
    CHECK(nonzero == 0, "read: poll loop never sees a packet");

    /* CARD_SpiWaitWriteRange: does nothing, the source is not changed. */
    for (i = 0; i < 16; i++) {
        payload[i] = (u8)i;
    }
    CARD_SpiWaitWriteRange(payload, 9);
    CARD_SpiWaitWriteRange(payload, 0);
    {
        int same = 1;
        for (i = 0; i < 16; i++) {
            if (payload[i] != (u8)i) {
                same = 0;
            }
        }
        CHECK(same, "write: source bytes unchanged");
    }

    /* CARD_SpiWaitGetStatus: 0xAA, twice, as pokewalker_ir.c:1099-1100 and fieldmap.c:775-776 need. */
    s1 = CARD_SpiWaitGetStatus();
    s2 = CARD_SpiWaitGetStatus();
    CHECK(s1 == 0xAA, "status: first probe is 0xAA");
    CHECK(s2 == 0xAA, "status: second probe is 0xAA");
    CHECK(s2 == 170, "status: FieldMap_DSProtCardSpiCheck (second probe == 170) passes");

    /* The Pokewalker start sequence (pokewalker_ir.c:63-66, 1099-1102) with the stub. */
    CARD_SpiWaitInit();
    CARD_SetSpiWriteWaitCycles(50);
    CARD_SetSpiReadWaitCycles(50);
    (void)CARD_SpiWaitReadRange(buf);
    if (CARD_SpiWaitGetStatus() == 0xAA) {
        CARD_SetSpiWriteWaitCycles(0);
        CARD_SetSpiReadWaitCycles(0);
    }
    CHECK(gCardSpiWait.writeWait == 0 && gCardSpiWait.readWait == 0, "start: cycles drop to 0 on 0xAA");

    /* No wait loop on the tick anywhere: the DS code polls OS_GetTick for every byte. */
    CHECK(tick_calls == 0, "no call polls the tick");

    printf("card_spi_wait: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
