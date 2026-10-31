/*
 * Host test for the SDK_PORT_3DS arena/stack defaults (patch 0302): the linker-symbol
 * replacements in lib/NitroSDK/src/os/{os_arena.c,os_thread.c}. Built and run by run.sh.
 * Synthetic values only.
 *
 * Without the patch this suite does not link: the OSi_* defaults are casts of DS linker
 * symbols (SDK_MAIN_ARENA_LO and friends) that nothing defines on the host. With the patch
 * the defaults are the port's memory blocks, so OS_InitArena/OS_InitThread run and every
 * bound is checked exactly. os_interrupt.c's one-line change (OSi_IRQ_STACK_TOP) is covered
 * by the preprocessor identity proof and the survey compile; the IRQ stack it places is
 * only observable through the interrupt entry path.
 */
/* No <stdio.h>: the project include path (lib/include) shadows stdarg.h, which breaks the
 * system stdio.h. Output goes through write(1); printf is declared by hand. */
#include <stdint.h>
#include <unistd.h>

#include <nitro.h>

int printf(const char *, ...);

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EQPTR(name, got, want) do { checks++; if ((void *)(got) != (void *)(want)) { fails++; \
    printf("FAIL line %d: %s = 0x%08x, want 0x%08x\n", __LINE__, name, \
           (unsigned)(u32)(got), (unsigned)(u32)(want)); } } while (0)
#define EQU32(name, got, want) do { checks++; if ((u32)(got) != (u32)(want)) { fails++; \
    printf("FAIL line %d: %s = 0x%08x, want 0x%08x\n", __LINE__, name, \
           (unsigned)(u32)(got), (unsigned)(u32)(want)); } } while (0)

/* DTCM backing, as in 3ds_port/nitro/src/port_memory.c:28. HW_DTCM is the address of this
 * symbol, so the launcher/IRQ stacks the SDK places in DTCM land in real memory here. */
u32 SDK_AUTOLOAD_DTCM_START[0x4000 / sizeof(u32)];

/* ---- stubs for os_arena.o/os_thread.o imports that the checks never exercise ---- */
u32 OS_GetConsoleType(void)
{
    return 0; /* size bits clear: not a 4 MB unit */
}

OSIntrMode OS_DisableInterrupts(void)
{
    return 0;
}

OSIntrMode OS_EnableInterrupts(void)
{
    return 0;
}

OSIntrMode OS_RestoreInterrupts(OSIntrMode state)
{
    (void)state;
    return 0;
}

OSProcMode OS_GetProcMode(void)
{
    return 0;
}

void OS_Terminate(void)
{
}

void OS_Halt(void)
{
}

void OS_SetProtectionRegion1(u32 param)
{
    (void)param;
}

void OS_SetProtectionRegion2(u32 param)
{
    (void)param;
}

void OS_InitContext(OSContext *context, u32 newpc, u32 newsp)
{
    (void)context;
    (void)newpc;
    (void)newsp;
}

BOOL OS_SaveContext(OSContext *context)
{
    (void)context;
    return FALSE;
}

void OS_LoadContext(OSContext *context)
{
    (void)context;
}

void MIi_CpuClear32(u32 data, void *destp, u32 size)
{
    u32 n = size / sizeof(u32);
    u32 *dst = (u32 *)destp;
    u32 i;

    for (i = 0; i < n; i++) {
        dst[i] = data;
    }
}

void OS_CreateAlarm(OSAlarm *alarm)
{
    (void)alarm;
}

void OS_CancelAlarm(OSAlarm *alarm)
{
    (void)alarm;
}

void OS_SetAlarm(OSAlarm *alarm, OSTick tick, OSAlarmHandler handler, void *arg)
{
    (void)alarm;
    (void)tick;
    (void)handler;
    (void)arg;
}

void OSi_UnlockAllMutex(OSThread *thread)
{
    (void)thread;
}

/* ---- the functions under test (signatures copied from the SDK headers) ---- */
void OS_InitArena(void);
void *OS_GetArenaHi(OSArenaId id);
void *OS_GetArenaLo(OSArenaId id);
void *OS_GetInitArenaHi(OSArenaId id);
void *OS_GetInitArenaLo(OSArenaId id);
void *OS_AllocFromArenaLo(OSArenaId id, u32 size, u32 align);
void *OS_AllocFromArenaHi(OSArenaId arena, u32 size, u32 align);
void OS_InitThread(void);

static void CheckInitDefaults(void)
{
    /* With the patch these are the port's blocks; without it they are casts of DS linker
     * symbols, which do not even link here. */
    EQPTR("main lo", OS_GetInitArenaLo(OS_ARENA_MAIN), (void *)HW_MAIN_MEM);
    EQPTR("main hi", OS_GetInitArenaHi(OS_ARENA_MAIN), (void *)HW_MAIN_MEM_MAIN_END);
    EQPTR("itcm lo", OS_GetInitArenaLo(OS_ARENA_ITCM), (void *)HW_ITCM);
    EQPTR("itcm hi", OS_GetInitArenaHi(OS_ARENA_ITCM), (void *)HW_ITCM_ARENA_HI_DEFAULT);
    EQPTR("dtcm lo", OS_GetInitArenaLo(OS_ARENA_DTCM), (void *)HW_DTCM);
    /* OSi_SYS_STACKSIZE == 0 takes the sysStackLo = HW_DTCM branch, which is >= the DTCM
     * arena low (itself HW_DTCM), so the DTCM high is the bottom of DTCM. */
    EQPTR("dtcm hi", OS_GetInitArenaHi(OS_ARENA_DTCM), (void *)HW_DTCM);
    /* The port's blocks really back these addresses. */
    EQPTR("main mem", (void *)HW_MAIN_MEM, (void *)NitroPort_MainMem);
    EQPTR("dtcm", (void *)HW_DTCM, (void *)SDK_AUTOLOAD_DTCM_START);
    EQPTR("itcm", (void *)HW_ITCM, (void *)NitroPort_Itcm);
}

static void CheckInitArena(void)
{
    void *lo, *hi;

    OS_InitArena();
    EQPTR("arena main lo", OS_GetArenaLo(OS_ARENA_MAIN), (void *)HW_MAIN_MEM);
    EQPTR("arena main hi", OS_GetArenaHi(OS_ARENA_MAIN), (void *)HW_MAIN_MEM_MAIN_END);
    EQPTR("arena dtcm lo", OS_GetArenaLo(OS_ARENA_DTCM), (void *)HW_DTCM);

    /* Allocation moves the low bound and stays inside the arena. */
    lo = OS_AllocFromArenaLo(OS_ARENA_MAIN, 16, 4);
    EQPTR("alloc lo", lo, (void *)HW_MAIN_MEM);
    EQPTR("alloc lo moves", OS_GetArenaLo(OS_ARENA_MAIN), (void *)(HW_MAIN_MEM + 16));
    hi = OS_AllocFromArenaHi(OS_ARENA_MAIN, 16, 4);
    EQPTR("alloc hi", hi, (void *)(HW_MAIN_MEM_MAIN_END - 16));
    EQPTR("alloc hi moves", OS_GetArenaHi(OS_ARENA_MAIN), (void *)(HW_MAIN_MEM_MAIN_END - 16));

    /* The MAINEX arena is never enabled by the SDK (its flag is static and nothing sets
     * it), so allocation there fails; this pins that behaviour. */
    CHECK(OS_AllocFromArenaLo(OS_ARENA_MAINEX, 16, 4) == NULL, "mainex lo alloc is not NULL");
    CHECK(OS_AllocFromArenaHi(OS_ARENA_MAINEX, 16, 4) == NULL, "mainex hi alloc is not NULL");
}

static void CheckLauncherStack(void)
{
    /* OSi_SYS_STACKSIZE == 0: the launcher thread's stack top is the bottom of DTCM and
     * its bottom is the IRQ stack top (HW_DTCM_SVC_STACK - 2048). The check numbers the
     * SDK writes must land in the DTCM backing array, not on unmapped memory. */
    OSThread *launcher;

    OS_InitThread();
    launcher = OS_GetCurrentThread();
    EQU32("launcher top", launcher->stackTop, HW_DTCM);
    EQU32("launcher bottom", launcher->stackBottom, HW_DTCM_SVC_STACK - 2048);
    EQU32("launcher check bottom", *(u32 *)(launcher->stackBottom - sizeof(u32)),
          0xfddb597dUL);
    EQU32("launcher check top", *(u32 *)launcher->stackTop, 0x7bf9dd5bUL);
}

int main(void)
{
    CheckInitDefaults();
    CheckInitArena();
    CheckLauncherStack();
    printf("%s: %d checks, %d failures\n", fails == 0 ? "PASS" : "FAIL", checks, fails);
    return fails != 0;
}
