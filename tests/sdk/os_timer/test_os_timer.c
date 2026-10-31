/*
 * Host test for the SDK_PORT_3DS software-timer hooks (patch 0602): OS_SetTimerCount /
 * OS_SetTimerControl in lib/include/nitro/os/common/timer_shared.h and the sync calls at
 * the start of OS_GetTick / OS_GetTickLo in lib/NitroSDK/src/os/os_tick.c.
 * Built and run by run.sh. Synthetic values only.
 *
 * The port side (3ds_port/nitro/src/port_timer.c) is stubs that record every call. Timer
 * registers are preset to a sentinel first: with the patch the Set inlines call the port
 * instead of writing the registers, so a direct write shows up as a changed sentinel.
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
#define SENTINEL16 0x5A5Au

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EQU32(name, got, want) do { checks++; if ((u32)(got) != (u32)(want)) { fails++; \
    printf("FAIL line %d: %s = 0x%08x, want 0x%08x\n", __LINE__, name, \
           (unsigned)(u32)(got), (unsigned)(u32)(want)); } } while (0)

/* ---- the port side (stubs) ---- */
static int reloadCalls, controlCalls, syncCalls, reservedCalls;
static int lastReloadId;
static u16 lastReloadCount;
static int lastControlId;
static u16 lastControl;
static int lastReserved;

void NitroPort_TimerSetReload(int id, u16 count)
{
    reloadCalls++;
    lastReloadId = id;
    lastReloadCount = count;
}

void NitroPort_TimerSetControl(int id, u16 control)
{
    controlCalls++;
    lastControlId = id;
    lastControl = control;
}

void NitroPort_SyncTimers(void)
{
    syncCalls++;
}

/* ---- stubs for os_tick.o imports that the checks never exercise ---- */
void OSi_SetTimerReserved(s32 timerNum)
{
    reservedCalls++;
    lastReserved = timerNum;
}

void OS_SetIrqFunction(OSIrqMask intrBit, OSIrqFunction function)
{
    (void)intrBit;
    (void)function;
}

OSIrqMask OS_EnableIrqMask(OSIrqMask mask)
{
    return mask;
}

OSIntrMode OS_DisableInterrupts(void)
{
    return 0;
}

OSIntrMode OS_RestoreInterrupts(OSIntrMode state)
{
    (void)state;
    return 0;
}

void OSi_EnterTimerCallback(u32 timerNo, void (*callback)(void *), void *arg)
{
    (void)timerNo;
    (void)callback;
    (void)arg;
}

/* Non-static globals of os_tick.o, driven directly. */
extern vu64 OSi_TickCounter;

/* ---- the functions under test ---- */
void OS_InitTick(void);
OSTick OS_GetTick(void);
u16 OS_GetTickLo(void);
void OS_SetTick(OSTick tick);
void OSi_CountUpTick(void);

static void MapIoPage(void)
{
    void *p = mmap((void *)(unsigned long)IO_BASE, 0x1000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    if (p != (void *)(unsigned long)IO_BASE) {
        printf("cannot map the I/O page at 0x%08x (got %lx)\n", IO_BASE, (unsigned long)p);
        exit(2);
    }
}

static u16 TmCountReg(int id)
{
    return *(REGType16 *)((u32)REG_TM0CNT_L_ADDR + (u32)id * 4);
}

static void PresetTimerRegs(void)
{
    int id;

    for (id = 0; id < 4; id++) {
        *(REGType16 *)((u32)REG_TM0CNT_L_ADDR + (u32)id * 4) = SENTINEL16;
        *(REGType16 *)((u32)REG_TM0CNT_H_ADDR + (u32)id * 4) = SENTINEL16;
    }
}

static void CheckSetCallsPort(void)
{
    PresetTimerRegs();
    reloadCalls = controlCalls = 0;
    OS_SetTimerCount(OS_TIMER_1, 0x1234);
    CHECK(reloadCalls == 1, "reload calls %d, want 1", reloadCalls);
    CHECK(lastReloadId == 1, "reload id %d, want 1", lastReloadId);
    EQU32("reload count", lastReloadCount, 0x1234);
    /* The patched inline calls the port instead of writing the register. */
    EQU32("count reg untouched", TmCountReg(1), SENTINEL16);

    OS_SetTimerControl(OS_TIMER_2, 0xC0);
    CHECK(controlCalls == 1, "control calls %d, want 1", controlCalls);
    CHECK(lastControlId == 2, "control id %d, want 2", lastControlId);
    EQU32("control value", lastControl, 0xC0);
}

static void CheckInitTick(void)
{
    reloadCalls = controlCalls = reservedCalls = 0;
    OS_InitTick();
    CHECK(reservedCalls == 1, "reserved calls %d, want 1", reservedCalls);
    CHECK(lastReserved == 0, "reserved timer %d, want 0", lastReserved);
    /* Control off, count zero, control on with the tick prescaler. */
    CHECK(controlCalls == 2, "init control calls %d, want 2", controlCalls);
    EQU32("init control value", lastControl,
          (u32)(REG_OS_TM0CNT_H_E_MASK | REG_OS_TM0CNT_H_I_MASK | OS_TIMER_PRESCALER_64));
    CHECK(reloadCalls == 1, "init reload calls %d, want 1", reloadCalls);
    EQU32("init reload count", lastReloadCount, 0);
}

static void CheckGetTick(void)
{
    OSTick tick;

    OSi_TickCounter = 5;
    *(REGType16 *)((u32)REG_TM0CNT_L_ADDR) = 0x4000;
    reg_OS_IF = 0;
    syncCalls = 0;
    tick = OS_GetTick();
    CHECK(syncCalls == 1, "get sync calls %d, want 1", syncCalls);
    EQU32("tick", (u32)tick, (5u << 16) | 0x4000u);

    /* A pending timer-0 interrupt with bit 15 clear counts one more high word. */
    *(REGType16 *)((u32)REG_TM0CNT_L_ADDR) = 0x1000;
    reg_OS_IF = OS_IE_TIMER0;
    tick = OS_GetTick();
    EQU32("tick carry", (u32)tick, (6u << 16) | 0x1000u);
}

static void CheckGetTickLo(void)
{
    *(REGType16 *)((u32)REG_TM0CNT_L_ADDR) = 0x2222;
    syncCalls = 0;
    EQU32("tick lo", OS_GetTickLo(), 0x2222);
    CHECK(syncCalls == 1, "lo sync calls %d, want 1", syncCalls);
}

static void CheckSetTick(void)
{
    reloadCalls = controlCalls = 0;
    OS_SetTick(0x00030004);
    EQU32("set counter", (u32)OSi_TickCounter, 3);
    CHECK(reloadCalls == 1, "set reload calls %d, want 1", reloadCalls);
    EQU32("set reload", lastReloadCount, 4);
    CHECK(controlCalls == 2, "set control calls %d, want 2", controlCalls);
}

static void CheckCountUp(void)
{
    OSi_TickCounter = 41;
    OSi_CountUpTick();
    EQU32("countup", (u32)OSi_TickCounter, 42);
}

int main(void)
{
    MapIoPage();
    CheckSetCallsPort();
    CheckInitTick();
    CheckGetTick();
    CheckGetTickLo();
    CheckSetTick();
    CheckCountUp();
    printf("%s: %d checks, %d failures\n", fails == 0 ? "PASS" : "FAIL", checks, fails);
    return fails != 0;
}
