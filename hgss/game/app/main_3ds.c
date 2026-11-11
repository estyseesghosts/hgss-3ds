/*
 * The game on a 3DS: the Phase 5 shell around Heartgold_Run.
 *
 * Adapted from 3ds_port/game/app/main_3ds.c (Platinum's shell): the Phase A
 * remaster display (GPU compositor, drawer threads, 400x240/320x240), input,
 * APT, a crash log, optional NDSP and the PC run's scripted input. The
 * remaster is the only display path; there is no classic 1:1 fallback.
 *
 * Like Platinum's shell, this file includes only libctru and heartgold.h:
 * libctru's headers cannot be mixed with the NitroSDK headers that
 * heartgold.c uses. The port hooks are plain C.
 *
 * Needs the player's data pack on the SD card. A HeartGold build reads
 *   sdmc:/3ds/pokeheartgold/heartgold3ds.pak
 * and a SoulSilver build reads
 *   sdmc:/3ds/pokeheartgold-ss/soulsilver3ds.pak
 * each with manifest.json next to it, keeps the save in the same folder
 * (heartgold.sav or soulsilver.sav, a raw 512 KiB save, as emulators keep
 * them), and writes log.txt there. A non-empty previous log.txt is renamed
 * to log-YYYYMMDD-HHMMSS-<build>.txt at boot, so past logs are never
 * overwritten; log.txt is always the current run.
 *
 * Alternate configs: holding START at launch reads port_A.ini, holding
 * SELECT reads port_B.ini, instead of port.ini. Hold START+SELECT for 2 s
 * at any time to leave cleanly for the Homebrew Launcher.
 *
 * Scripted input: put a tests/inputs-style script at script.txt in the
 * game's folder, or name it in port.ini:
 *   script=script.txt
 * The replay is frame-for-frame the PC runner's, so a hardware run repeats
 * the PC run. Without a script the console's buttons and touch screen drive
 * the game.
 *
 * Frame cap: one emulated DS V-blank per displayed frame, 60 Hz by default.
 *   fps=60        (the default; the game runs at its intended speed)
 *   fps=30        comparison mode: the game clock is per frame, so the game
 *                 runs at half speed. Missing or invalid values use 60.
 */

#include <3ds.h>
#include <3ds/ndsp/ndsp.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "3ds_input.h"
#include "heartgold.h"
/* Build identity (gen/build_id.h, written by tools/hgss_game.py): the
 * commit + dirty flag, so every log traces to the binary that wrote it.
 * Builds without the generated header (host tools) report "unknown". */
#ifdef __has_include
#if __has_include("build_id.h")
#include "build_id.h"
#endif
#endif
#ifndef HGSS_BUILD_ID
#define HGSS_BUILD_ID "unknown"
#endif
#include "port_pica.h"
/* The scene type and the geometry thread's ops. Unlike nitro_port.h this
 * header is standalone (stdint/stddef only), so it can sit next to libctru's. */
#include <nitro_port_gx3d.h>
#include <nitro_port_render2d.h>
#include <nitro_port_wide.h>

#define DATA_DIR HEARTGOLD_DATA_DIR
#define PAK_PATH DATA_DIR "/" HEARTGOLD_PAK_NAME
#define MANIFEST_PATH DATA_DIR "/manifest.json"
#define SAVE_PATH DATA_DIR "/" HEARTGOLD_SAVE_NAME
#define LOG_PATH DATA_DIR "/log.txt"
#define INI_PATH DATA_DIR "/port.ini"
#define INI_PATH_A DATA_DIR "/port_A.ini"
#define INI_PATH_B DATA_DIR "/port_B.ini"
#define DEFAULT_SCRIPT DATA_DIR "/script.txt"

/* libctru puts the Thread block at the base of the same allocation the stack
 * grows down into, so an overflow here corrupts the very struct that
 * threadJoin reads. The DS game's stack is its own, but the port's frames ride
 * on this one; 1 MiB costs nothing (the heap has tens of MB) and takes the
 * "did we silently overwrite Thread.finished?" question off the table. */
#define GAME_STACK_SIZE (1024 * 1024)
#define LOG_RING 64
/* 512, not 220: the [perf] lines ran past the old buffer and were truncated
 * mid-field on hardware ("handover 0.00 m", "scene build 970.07 ms, GPU 0.00
 * ms, handover 0."), which is exactly the data a performance run needs. */
#define LOG_LINE 512
/* How much of the log file stays in stdio's buffer before a flush. The old
 * code flushed every line, which put an SD write in the frame path during
 * hitches and loads; the ring below keeps the last lines for the exception
 * log, and FlushLog() puts the rest on the card once a second. */
#define LOG_BUF_BYTES (32 * 1024)

/* One emulated DS V-blank per frame. 30 Hz is a comparison mode only: the
 * game's clock is the per-frame V-blank, so it halves the game's speed. */
#define DEFAULT_FPS 60

/* The port's hooks (see nitro_port.h). Declared here rather than included:
 * nitro_port.h pulls in NitroSDK's types, which cannot live next to
 * libctru's.
 *
 * These must be STRONG references. A weak one does not make the linker
 * extract the definition from libnitro.a, so it binds to NULL and the call is
 * skipped -- that is how the first hardware build drew nothing (draw_us=1)
 * while the game ran for 39,000 frames. */
void NitroPort_Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void NitroPort_SetButtons(uint16_t held, int lidClosed);
void NitroPort_SetTouch(int down, uint16_t x, uint16_t y);
void NitroPort_CardFlush(void);
int NitroPort_EngineAOnTop(void);
void NitroPort_SoundRender(int16_t *stereo, size_t frames);
void NitroPort_SoundPumpReplies(void);
/* Display-gating diagnostics (port_compose_pica.c, unconditional counters):
 * loop passes vs draws vs skip reasons. Declared here, not included: this
 * file cannot include the port's headers next to libctru's. */
void NitroPort_ComposeDbgStats(u64 *noNeed, u64 *wait2D, u64 *midLate,
                               u64 *noBuf, u64 *draws, u64 *pickTicks,
                               u64 *prepareTicks, u64 *stageTicks,
                               u64 *endTicks, u64 *skipTicks,
                               u64 *needA, u64 *needB,
                               u64 *drawsScene, u64 *serialSkipped);
#ifndef HGSS_PROFILE_OFF
/* P1: compose causes + end-phase split (port_compose_pica.c). */
void NitroPort_ComposeCauseStats(u64 *scene, u64 *set, u64 *forced, u64 *other,
                                 u64 *endXfer, u64 *endFlight, u64 *endLock, u64 *endSubmit);
#endif
/* Display-loop passes and sGpuWake signals (counted here, at the sites). */
static u64 sDbgLoopIters, sDbgWakes;
/* The display state's H-blank lines: captured once a V-blank, so a battle's
 * start transition or a wavy screen keeps its per-line scrolls. */
int NitroPort_HBlankLinesOnce(void);
/* The same walk split into the register copy, the DMAs and the handlers. */
void NitroPort_HBlankSplit(u64 *keep, u64 *dma, u64 *irq);
#ifndef HGSS_PROFILE_OFF
/* P1: the H-blank parts outside keep/dma/irq (port_os.c). */
void NitroPort_HBlankSplit2(u64 *refresh, u64 *sw, u64 *cmp);
#endif
#ifdef HGSS_A0_DIAG
/* P6: the walks where the fused per-line compare and the old scan over all
 * the lines were compared, and the ones where they differed (port_os.c). */
void NitroPort_HBlankScanCheck(u64 *walks, u64 *mismatch);
#endif
/* Slice S4: H-blank attribution counters (3ds_port/nitro, diagnostics only).
 * The walk's interrupt handlers, one entry per IRQ bit (layout mirrors
 * nitro_port.h's NitroPortIrqTime, which this file cannot include next to
 * libctru's headers: nitro's u64 is aligned(4) for DS save-layout compat,
 * so the struct is 12 bytes with stride 12 -- a plain uint64_t mirror would
 * be 16 bytes and every entry past [0] would read garbage). The alignment
 * reduction only takes on the typedef (as in nitro's types.h), not on the
 * member. SyncTimers cost/count; the sound-pump tick sink; the H-blank
 * DMA transfer count. */
typedef uint64_t NitroPortTick __attribute__((aligned(4)));
typedef struct NitroPortIrqTime { NitroPortTick ticks; uint32_t calls; } NitroPortIrqTime;
_Static_assert(sizeof(NitroPortIrqTime) == 12, "NitroPortIrqTime mirror must be 12 bytes like nitro_port.h");
void NitroPort_HBlankIrqByBit(NitroPortIrqTime *out);
void NitroPort_SyncTimersStats(u64 *calls, u64 *ticks);
void NitroPort_NoteSoundPumpTicks(u64 ticks);
void NitroPort_SoundPumpTickStats(u64 *ticks, u32 *calls);
u32 NitroPort_DmaHBlankTransfers(void);
/* The 2D renderer draws the field's repeating weather BGs itself unless the
 * compositor claims them; the scene is scrolled with the game's own offsets. */
void NitroPort_Render2DBgOffsets(uint16_t out[8]);

/* The GPU half, from port_gx3d_pica.c and port_compose_pica.c (compiled into
 * the program only). PicaInit starts citro3d; the display thread draws each
 * finished scene with NitroPort_ComposeFrame. */
int NitroPort_PicaInit(void);
void NitroPort_PicaSetShadowRoom(int on);
void NitroPort_PicaExit(void);

/* libctru hands the process a heap and a linear heap. The remaster's render
 * targets, layer sets and 3D textures all live in linear memory, and on an
 * Old 3DS the game's own heap was using 798 KB of 15,732 KB at startup while
 * the compositor was left 408 KB for every 3D texture in the game. The split
 * below gives the game 12 MB on a small (64 MiB) process -- its pak block
 * cache (4 MB) and wide-art slots (2.9 MB), not its DS heap, are what use it
 * -- and the rest to the display. A larger process keeps libctru's caps. */
#define HEAP_SMALL_TOTAL (48u << 20)
#define HEAP_O3DS_GAME   (12u << 20)
extern char *fake_heap_start;
extern char *fake_heap_end;
extern u32 __ctru_heap, __ctru_linear_heap, __ctru_heap_size, __ctru_linear_heap_size;

void __system_allocateHeaps(void)
{
    Handle reslimit = 0;
    s64 maxCommit = 0, currentCommit = 0;
    ResourceLimitType type = RESLIMIT_COMMIT;
    u32 remaining;
    Result rc;

    if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE))) {
        svcBreak(USERBREAK_PANIC);
    }
    svcGetResourceLimitLimitValues(&maxCommit, reslimit, &type, 1);
    svcGetResourceLimitCurrentValues(&currentCommit, reslimit, &type, 1);
    svcCloseHandle(reslimit);
    remaining = (u32)(maxCommit - currentCommit) & ~0xFFFu;

    if (remaining <= HEAP_SMALL_TOTAL) {
        /* A 64 MiB process (an Old 3DS, or anything else this small): the
         * game gets its 10 MB and the display gets the rest. The old test
         * was `remaining > 16 MB * 2`, which a 30.7 MiB Old 3DS fails, so it
         * silently fell through to libctru's half split and the display got
         * 15.7 MB. */
        __ctru_heap_size = remaining > HEAP_O3DS_GAME ? HEAP_O3DS_GAME : (remaining / 2u) & ~0xFFFu;
        __ctru_linear_heap_size = remaining - __ctru_heap_size;
    } else {
        __ctru_linear_heap_size = (remaining / 2u) & ~0xFFFu;
        __ctru_heap_size = remaining - __ctru_linear_heap_size;
        if (__ctru_heap_size > (24u << 20)) {
            __ctru_heap_size = 24u << 20;
            __ctru_linear_heap_size = remaining - __ctru_heap_size;
            if (__ctru_linear_heap_size > (32u << 20)) {
                __ctru_linear_heap_size = 32u << 20;
                __ctru_heap_size = remaining - __ctru_linear_heap_size;
            }
        }
    }
    rc = svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size, MEMOP_ALLOC,
                          MEMPERM_READ | MEMPERM_WRITE);
    if (R_FAILED(rc)) {
        svcBreak(USERBREAK_PANIC);
    }
    rc = svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size,
                          MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE);
    if (R_FAILED(rc)) {
        svcBreak(USERBREAK_PANIC);
    }
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    fake_heap_start = (char *)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}

static FILE *sLog;
static LightLock sLogLock;
static bool sLogLockReady;
static char sLogBuf[LOG_BUF_BYTES];
static u64 sLogLastFlush;
static volatile bool sGameThreadAlive;
static bool sThreadsStuck;
static unsigned long sFrame;
static volatile bool sQuit;
static volatile bool sPaused;
static bool sAudio;
static bool sScriptLoaded;
static HgssScript sScript;
static u64 sLastFrameTick;
static bool sHaveLastFrameTick;
static unsigned long long sPrevStateUs;   /* (the hand-off of a frame ago) */
/* DrawFrame's phases, in ARM11 ticks: the H-blank lines (and the BG offsets),
 * then the hand-off (state copy, the drawers' signals). Kept apart because
 * "13.5 ms" could be any of them. */
static u64 sFrameHBlankTicks, sFrameHandoffTicks;
static u64 sPrevHBlankTicks, sPrevHandoffTicks;
static u64 sFrameHbKeep, sFrameHbDma, sFrameHbIrq;
static u64 sPrevHbKeep, sPrevHbDma, sPrevHbIrq;
#ifndef HGSS_PROFILE_OFF
/* P1: the H-blank parts outside keep/dma/irq (see NitroPort_HBlankSplit2). */
static u64 sFrameHbRefresh, sFrameHbSwitch, sFrameHbCmp;
static u64 sPrevHbRefresh, sPrevHbSwitch, sPrevHbCmp;
#endif
static int sFrameCap = DEFAULT_FPS;
static u64 sNextFrameTick;
#ifndef HGSS_PROFILE_OFF
/* P1: game-thread accounting (see WaitForFrameCap): work ticks from the
 * OnFrame entry to the cap call, ticks slept in the cap, and frames that
 * arrived already late. */
static u64 sGameWorkTicks, sGameSleepTicks;
static u32 sGameLateFrames;
#endif

/* The last log lines, for the exception log. */
static char sRing[LOG_RING][LOG_LINE];
static int sRingCount;
static int sRingNext;

/* ------------------------------------------------------------------ hooks */

/* Put the buffered lines on the card. The caller holds sLogLock, or nothing
 * else is running yet. */
static void FlushLogLocked(void)
{
    if (sLog != NULL) {
        fflush(sLog);
        sLogLastFlush = svcGetSystemTick();
    }
}

static void FlushLog(void)
{
    if (sLogLockReady) {
        LightLock_Lock(&sLogLock);
    }
    FlushLogLocked();
    if (sLogLockReady) {
        LightLock_Unlock(&sLogLock);
    }
}

static void LogLine(const char *line)
{
    snprintf(sRing[sRingNext], LOG_LINE, "%s", line);
    sRingNext = (sRingNext + 1) % LOG_RING;
    if (sRingCount < LOG_RING) {
        sRingCount++;
    }
}

void NitroPort_Log(const char *fmt, ...)
{
    char line[LOG_LINE];
    va_list args;

    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    /* Two threads write this file (the game's and the main one). stdio is not
     * reentrant, and without the lock a line can be lost or torn -- the
     * hardware log showed a game-thread line after the main thread had
     * already declared the thread ended. */
    if (sLogLockReady) {
        LightLock_Lock(&sLogLock);
    }
    fprintf(stderr, "[%lu] %s\n", sFrame, line);
    if (sLog != NULL) {
        fprintf(sLog, "[%lu] %s\n", sFrame, line);
        /* Once a second, not once a line: the card write belongs off the
         * frame path. The caller holding the lock means this never races
         * with another thread's fprintf. */
        if (svcGetSystemTick() - sLogLastFlush >= SYSCLOCK_ARM11) {
            FlushLogLocked();
        }
    }
    LogLine(line);
    if (sLogLockReady) {
        LightLock_Unlock(&sLogLock);
    }
}

/* The 3DS's buttons and circle pad, as the DS's PAD bits. */
static uint16_t DsButtons(uint32_t held, circlePosition circle)
{
    uint16_t out = 0;
    const int dead = 40;

    if (held & KEY_A)      out |= HGSS_BUTTON_A;
    if (held & KEY_B)      out |= HGSS_BUTTON_B;
    if (held & KEY_SELECT) out |= HGSS_BUTTON_SELECT;
    if (held & KEY_START)  out |= HGSS_BUTTON_START;
    if (held & KEY_DRIGHT) out |= HGSS_KEY_RIGHT;
    if (held & KEY_DLEFT)  out |= HGSS_KEY_LEFT;
    if (held & KEY_DUP)    out |= HGSS_KEY_UP;
    if (held & KEY_DDOWN)  out |= HGSS_KEY_DOWN;
    if (held & KEY_R)      out |= HGSS_BUTTON_R;
    if (held & KEY_L)      out |= HGSS_BUTTON_L;
    if (held & KEY_X)      out |= HGSS_BUTTON_X;
    if (held & KEY_Y)      out |= HGSS_BUTTON_Y;
    /* D-pad and circle pad both drive the DS D-pad. */
    if (circle.dx > dead)  out |= HGSS_KEY_RIGHT;
    if (circle.dx < -dead) out |= HGSS_KEY_LEFT;
    if (circle.dy > dead)  out |= HGSS_KEY_UP;
    if (circle.dy < -dead) out |= HGSS_KEY_DOWN;
    return out;
}

/* ------------------------------------------------------ the 3D's threads */

/* Platinum's proven classic path: a geometry thread builds the DS's scenes off
 * the game's thread, and a GPU thread draws each finished scene into an
 * off-screen PICA target, hands the RGBA8 image to the port's image API
 * (NitroPort_Gx3dSubmitRGBA8), and converts it to the DS's RGB555 there. The
 * 2D compositor then reads the published image for engine A -- that is the 3D
 * layer the field and the intro were missing.
 *
 * The thread failed -> 2D only, exactly as before this pipeline existed. Both
 * failures are logged and nothing else changes. */
static Thread sGpuThread;
static LightEvent sGpuWake, sGpuStarted;
static volatile bool sGpuRunning, sGpuWakeReady;
static volatile unsigned long sGpuScenes;
/* A thread that would not stop: end the process rather than tear libctru and
 * citro3d down around it (Platinum's sStopFailed rule). */
static bool sGpuStopFailed;
/* New 3DS: the geometry thread gets core 2 (the game and the 2D composite
 * stay on core 0; the GPU thread is there as well, below the game). */
static bool sNew3ds;
/* The audio thread's core, for the [perf] line (set in AudioStart; -2 is
 * "any core", the New 3DS default). Tentative here so PerfLog above can
 * read it; initialised at the audio section. */
static int sAudioCore;
/* `audiocore=` / `o3dsched=` overrides (ReadDisplayOptions):
 * declared here because the drawer and audio threads start before that
 * section. */
static int sIniAudioCore = -1;
/* `o3dsched=display-first`: on an Old 3DS only, demote the always-ready
 * geometry worker below the event-driven display thread on core 1 (1 by
 * default on Old 3DS, 0 with `o3dsched=default`: today's order). Same-core
 * threads don't time-slice: whichever is lower never runs while the higher
 * is ready, and log-9's 0.0 composes/s under a saturated core 1 is that
 * starvation. */
static int sIniSched = 0;

static Thread sGeoThread;
static LightEvent sGeoWorkerEvent, sGeoProducerEvent;
static volatile int sGeoRunning;

/* The port calls this at each SWAP_BUFFERS (port_gx3d.c's weak default is a
 * no-op). It is how the GPU thread learns a scene is ready. */
void NitroPort_Gx3dScenePublished(void)
{
    if (sGpuWakeReady) {
        sDbgWakes++;
        LightEvent_Signal(&sGpuWake);
    }
}

static ComposeConfig sCompose;
static volatile bool sSuspended;
static volatile bool sComposing;      /* the display thread is inside a compose */
static volatile bool sResumePending;  /* ONRESTORE arrived; resume after aptMainLoop */
static bool sAptHome, sAptAsleep;     /* which APT source still holds the app */
static volatile bool sComposeForce;   /* redraw everything (back from the HOME menu) */
static bool VBlankStart(s32 priority);
static void VBlankStop(void);
static void GpuComposeLoop(void);
static void AudioStop(void);
/* Below, next to the other exits: START+SELECT's direct jump to hbmenu. */
static void ExitToLauncher(void);

#ifdef HGSS_A0_DIAG
static void A0ThreadRunning(const char *role);
#endif
#if defined(HGSS_A0_DIAG) || !defined(HGSS_PROFILE_OFF)
/* Lowest-priority probes count completed batches, not CPU cycles. Their
 * startup calibration measures the available idle rate on each core; OS
 * scheduling and the requested core-1 share are part of that baseline. */
static Thread sA0IdleThread[2];
static volatile bool sA0IdleRunning;
static bool sA0IdleAlive[2];
typedef struct { u64 count; unsigned char padding[56]; } A0IdleCounter;
static A0IdleCounter sA0IdleCounter[2] __attribute__((aligned(64)));
static u64 sA0IdleCal[2], sA0IdleLast[2];
static u64 sA0IdleCalTicks;

static void A0IdleMain(void *arg)
{
    int core = (int)(uintptr_t)arg;

    while (__atomic_load_n(&sA0IdleRunning, __ATOMIC_RELAXED)) {
        for (unsigned i = 0; i < 4096; i++) __asm__ volatile("" ::: "memory");
        __atomic_fetch_add(&sA0IdleCounter[core].count, 4096u, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&sA0IdleAlive[core], false, __ATOMIC_RELEASE);
    threadExit(0);
}

static void A0IdleStart(void)
{
    u64 start;
    u64 before[2];

    __atomic_store_n(&sA0IdleRunning, true, __ATOMIC_RELAXED);
    for (int core = 0; core < 2; core++) {
        __atomic_store_n(&sA0IdleAlive[core], true, __ATOMIC_RELEASE);
        sA0IdleThread[core] = threadCreate(A0IdleMain, (void *)(uintptr_t)core,
                                           8 * 1024, 0x3f, core, false);
        if (!sA0IdleThread[core])
            __atomic_store_n(&sA0IdleAlive[core], false, __ATOMIC_RELEASE);
    }
    for (int core = 0; core < 2; core++)
        before[core] = __atomic_load_n(&sA0IdleCounter[core].count, __ATOMIC_RELAXED);
    start = svcGetSystemTick();
    svcSleepThread(250000000LL);
    sA0IdleCalTicks = svcGetSystemTick() - start;
    for (int core = 0; core < 2; core++)
        sA0IdleCal[core] = __atomic_load_n(&sA0IdleCounter[core].count, __ATOMIC_RELAXED) - before[core];
    NitroPort_Log("[a0] idle calibration: %llu ticks core0 %llu core1 %llu counts (250ms before workers; system share included)",
                  (unsigned long long)sA0IdleCalTicks,
                  (unsigned long long)sA0IdleCal[0], (unsigned long long)sA0IdleCal[1]);
    if (!sA0IdleThread[0] || !sA0IdleThread[1])
        NitroPort_Log("[a0] idle probe creation failed for at least one core");
}

static void A0IdleStop(void)
{
    __atomic_store_n(&sA0IdleRunning, false, __ATOMIC_RELAXED);
    for (int core = 0; core < 2; core++) {
        if (sA0IdleThread[core]) {
            u64 start = svcGetSystemTick();
            while (__atomic_load_n(&sA0IdleAlive[core], __ATOMIC_ACQUIRE) &&
                   svcGetSystemTick() - start < SYSCLOCK_ARM11)
                svcSleepThread(1000000LL);
            if (__atomic_load_n(&sA0IdleAlive[core], __ATOMIC_ACQUIRE) ||
                R_FAILED(threadJoin(sA0IdleThread[core], 1000000000LL))) {
                NitroPort_Log("[a0] idle probe %d did not stop", core);
                sThreadsStuck = true;
            }
            /* Keep the stack until process exit: some hardware has reported
             * a completed join before a thread fully exits. */
            sA0IdleThread[core] = NULL;
        }
    }
}
#endif
#ifndef HGSS_A0_DIAG
#define A0ThreadRunning(role) ((void)0)
#endif

static void GpuMain(void *arg)
{
    s32 priority = 0x30;

    (void)arg;
    A0ThreadRunning("display");
    if (!NitroPort_PicaInit()) {
        NitroPort_Log("[gx3d] citro3d init failed; there is no display");
        sGpuRunning = false;
        LightEvent_Signal(&sGpuStarted);
        threadExit(0);
    }
    if (!NitroPort_ComposeInit(&sCompose)) {
        NitroPort_Log("[compose] the GPU display could not be set up; there is no display");
        sGpuRunning = false;
        NitroPort_PicaExit();
        LightEvent_Signal(&sGpuStarted);
        threadExit(0);
    }
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    if (!VBlankStart(priority - 1)) {
        sGpuRunning = false;
        NitroPort_ComposeExit();
        NitroPort_PicaExit();
        LightEvent_Signal(&sGpuStarted);
        threadExit(0);
    }
    NitroPort_Log("[compose] GPU display ready (remaster, no classic fallback)");
    LightEvent_Signal(&sGpuStarted);
    GpuComposeLoop();
    VBlankStop();
    NitroPort_ComposeExit();
    NitroPort_PicaExit();
    threadExit(0);
}

static bool GpuStart(s32 priority, int core)
{
    LightEvent_Init(&sGpuWake, RESET_ONESHOT);
    LightEvent_Init(&sGpuStarted, RESET_ONESHOT);
    sGpuRunning = true;
    sGpuWakeReady = true;
    /* Core 0 on a New 3DS, as Platinum does; an Old 3DS puts the display on
     * core 1, whose share the geometry thread asks for. On an Old 3DS the
     * display's priority is below the drawer's so a publishing drawer finishes
     * first; on a New 3DS both stay as they were. */
    sGpuThread = threadCreate(GpuMain, NULL, 128 * 1024, priority, core, false);
    if (sGpuThread == NULL) {
        sGpuRunning = false;
        sGpuWakeReady = false;
        NitroPort_Log("[compose] couldn't start the display thread");
        return false;
    }
    /* PicaInit/ComposeInit run inside the thread: on failure it says so,
     * clears sGpuRunning and this wait returns with the flag clear. */
    LightEvent_Wait(&sGpuStarted);
    if (!sGpuRunning) {
        threadJoin(sGpuThread, 1000 * 1000000LL);
        threadFree(sGpuThread);
        sGpuThread = NULL;
        sGpuWakeReady = false;
        return false;
    }
    NitroPort_Log("[compose] the display on core %d, priority 0x%02x",
                  core, (unsigned)priority);
    NitroPort_Log("[compose] display thread ready");
    return true;
}

static void GpuStop(void)
{
    if (sGpuThread == NULL) {
        return;
    }
    sGpuRunning = false;
    if (sGpuWakeReady) {
        LightEvent_Signal(&sGpuWake);
    }
    if (R_SUCCEEDED(threadJoin(sGpuThread, 2000 * 1000000LL))) {
        threadFree(sGpuThread);
    } else {
        sGpuStopFailed = true;
        NitroPort_Log("[gx3d] the 3D thread didn't stop; quitting without it");
    }
    sGpuThread = NULL;
    sGpuWakeReady = false;
}

static void GeoWaitWorker(void *arg) { (void)arg; LightEvent_Wait(&sGeoWorkerEvent); }
static void GeoWakeWorker(void *arg) { (void)arg; LightEvent_Signal(&sGeoWorkerEvent); }
static void GeoWaitProducer(void *arg) { (void)arg; LightEvent_Wait(&sGeoProducerEvent); }
static void GeoWakeProducer(void *arg) { (void)arg; LightEvent_Signal(&sGeoProducerEvent); }
static uint64_t GeoTicks(void *arg) { (void)arg; return svcGetSystemTick(); }

static void GeoMain(void *arg)
{
    (void)arg;
    A0ThreadRunning("geometry");
    NitroPort_Gx3dThreadMain(&sGeoRunning);
    threadExit(0);
}

/* Ask the system for a share of core 1, once, before any thread runs there.
 *
 * On an Old 3DS core 1 belongs to the system. Both the geometry thread and
 * (since the 80% measurement) the display thread run on it, so the request
 * cannot wait for the geometry start-up: a core-1 thread created before the
 * limit is raised can be throttled hard while it initialises citro3d and the
 * compositor. APT only allows the main thread to ask, which is where this is
 * called from. */
static bool sCore1ShareAsked;

static void RequestCore1Share(void)
{
    static const u32 limits[] = { 80, 60, 40, 30 };
    u32 accepted = 0, asked = 0;
    size_t i;

    if (sCore1ShareAsked) {
        return;
    }
    for (i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
        if (R_SUCCEEDED(APT_SetAppCpuTimeLimit(limits[i]))) {
            asked = limits[i];
            sCore1ShareAsked = true;
            break;
        }
    }
    if (!sCore1ShareAsked) {
        NitroPort_Log("[perf] no core 1 share: the 3D and the display stay on core 0");
        return;
    }
    /* What the system actually gave us: the first plan assumed 30% and every
     * core-1 placement decision depended on it, unmeasured. */
    if (R_SUCCEEDED(APT_GetAppCpuTimeLimit(&accepted))) {
        NitroPort_Log("[perf] core 1 share: asked %lu%%, the system allows %lu%%",
                      (unsigned long)asked, (unsigned long)accepted);
    } else {
        NitroPort_Log("[perf] core 1 share: asked %lu%%, and cannot be read back",
                      (unsigned long)asked);
    }
}

static bool GpuStartGeometry(s32 priority)
{
    NitroGx3dThreadOps ops;

    memset(&ops, 0, sizeof(ops));
    ops.wait_worker = GeoWaitWorker;
    ops.wake_worker = GeoWakeWorker;
    ops.wait_producer = GeoWaitProducer;
    ops.wake_producer = GeoWakeProducer;
    ops.ticks = GeoTicks;
    LightEvent_Init(&sGeoWorkerEvent, RESET_ONESHOT);
    LightEvent_Init(&sGeoProducerEvent, RESET_ONESHOT);
    sGeoRunning = 1;
    /* Before the thread runs: it needs the ops. Nothing has been drawn yet,
     * so if it can't start the engine goes back to the game's thread. */
    NitroPort_Gx3dStartThread(&ops);
    if (sNew3ds) {
        /* Core 2 is the New 3DS's extra core; if the kernel will not give it
         * (an Old 3DS in a New 3DS's clothes, no 804 MHz mode), any core
         * still builds scenes correctly, just not off the game's thread. */
        sGeoThread = threadCreate(GeoMain, NULL, 64 * 1024, priority - 1, 2, false);
        if (sGeoThread == NULL) {
            sGeoThread = threadCreate(GeoMain, NULL, 64 * 1024, priority - 1, -2, false);
        }
    } else {
        /* An Old 3DS has no core 2. Its core 1 is the system's: ask for a
         * share of it (Platinum's path) so the game, the display and the
         * drawer keep core 0. If no share can be had, leave scene building
         * on the game's thread rather than take its core.
         *
         * The request has already been made before the display thread
         * started on core 1 (RequestCore1Share below); this only reads it
         * back. */
        if (sCore1ShareAsked) {
            /* o3dsched=display-first (the Old 3DS default): demote the
             * always-ready geometry worker below the event-driven display
             * thread on this core (0x33 under the display's 0x32). Same-core
             * threads don't time-slice: a lower thread never runs while a
             * higher one is ready, so with geometry above, the display
             * starves outright in saturated areas (log-9's 0.0 composes/s)
             * instead of just slowing down. New 3DS keeps its profile. */
            s32 geoPriority = sIniSched ? priority + 3 : priority - 1;

            sGeoThread = threadCreate(GeoMain, NULL, 64 * 1024, geoPriority, 1, false);
        }
    }
    if (sGeoThread == NULL) {
        NitroPort_Gx3dStopThread();
        sGeoRunning = 0;
        NitroPort_Log("[gx3d] no geometry thread; the game's thread builds scenes");
        return false;
    }
    NitroPort_Log("[gx3d] geometry on its own thread (core %d)", sNew3ds ? 2 : 1);
    return true;
}

static void GpuStopGeometry(void)
{
    /* Drain while the worker still runs. NitroPort_Gx3dStopThread waits for
     * the ring to empty, and on a close the game's thread left
     * NitroPort_OnFrame before its Gx3dFlush: its last frame is still in the
     * producer's buffer. With the thread already joined and gone, that wait
     * has nobody to consume the words and never ends (log-12's quit hung
     * here, after "closing at frame"). */
    NitroPort_Gx3dStopThread();
    if (sGeoThread != NULL) {
        sGeoRunning = 0;
        LightEvent_Signal(&sGeoWorkerEvent);
        if (R_SUCCEEDED(threadJoin(sGeoThread, 1000 * 1000000LL))) {
            threadFree(sGeoThread);
        } else {
            sGpuStopFailed = true;
            NitroPort_Log("[gx3d] the geometry thread didn't stop; quitting without it");
        }
        sGeoThread = NULL;
    }
}

/* ---------------------------------------------------- the remaster display
 *
 * Platinum's display architecture, with HGSS's game behind it:
 *  - the game thread hands a frame's display state to up to two drawer
 *    threads, which split it into the layers behind/in front of the 3D and
 *    publish them as textures;
 *  - the GPU thread composes each new 3D scene with that 2D at the screen's
 *    resolution (port_compose_pica.c);
 *  - the V-blank thread presents the frames that are due.
 * There is no classic 1:1 path: the remaster is the program's display. */

#define ENGINE_A 1
#define ENGINE_B 2

typedef struct Drawer {
    Thread thread;
    LightEvent go;
    int screens;                    /* ENGINE_A, ENGINE_B, or both */
    int core;
    s32 priority;
    volatile bool busy;
    volatile bool reading;
    volatile bool quit;
    volatile bool aOnTop;
    volatile u32 stamp;
    volatile bool offerB;
    u32 lastBStamp;
    bool lastAOnTop;
    volatile unsigned bSkips;
    u32 images;
    /* [perf]: the frames it was handed, and where their time went (all
     * written on the drawer's thread, read by the game's thread at the log). */
    volatile u64 ticks;             /* the whole frame */
    volatile u64 renderTicks;       /* RenderScreen */
    volatile u64 publishTicks;      /* ShowScreen and Compose2DCurrent */
    volatile unsigned count;        /* frames handled */
    volatile unsigned drawn;        /* screens actually redrawn */
    volatile u64 screenRenderTicks[2]; /* physical top/bottom, wall ticks */
    volatile unsigned screenRenderCalls[2];
#ifndef HGSS_PROFILE_OFF
    /* P1: offer-to-start latency. The game thread stamps signalTick at
     * LightEvent_Signal; the drawer accumulates start - signalTick. */
    volatile u64 signalTick;
    volatile u64 signalLatency;
    volatile unsigned signalCount;
#endif
    u64 logTicks, logRenderTicks, logPublishTicks;
    unsigned logCount, logDrawn, logBskips;
#ifndef HGSS_PROFILE_OFF
    u64 logSignalLatency;
    unsigned logSignalCount;
#endif
    u64 logScreenRenderTicks[2];
    unsigned logScreenRenderCalls[2];
} Drawer;

static Drawer sDrawers[2];
static int sDrawerCount;
static bool sAOnTop;
static volatile bool sMustDraw[2] = { true, true };
#ifdef HGSS_A0_DIAG
/* A0: forced full redraws bypassing the unchanged check (toast, publish
 * failure, resume). Indexed by physical screen (0 = top, 1 = bottom). */
static volatile unsigned sA0MustDraw[2];
#endif
/* How many drawers were handed the frame in flight. The last one to finish
 * wakes the display thread: one compose per game frame on an Old 3DS (one
 * drawer, both screens), one for the pair on a New 3DS. */
static volatile int sDrawFramePending;
#ifndef HGSS_PROFILE_OFF
/* P1: DrawFrame offers dropped because every drawer was busy. Written on the
 * game thread, read at the log. */
static volatile unsigned sDrawDropped;
#endif
static Thread sVBlankThread;
static volatile bool sVBlankRunning;

/* The pixel-filter presets X+R cycles through (cheapest first), the one in
 * use, and the toast naming it. The upper screen's 3D/stereo and smooth3d
 * are left alone. */
typedef struct FilterPreset {
    const char *name;
    int filter2d;               /* COMPOSE_FILTER_* */
    int filter3d;               /* ComposeConfig.filter3d */
} FilterPreset;
static const FilterPreset sFilterPresets[] = {
    { "NEAREST/NEAREST", COMPOSE_FILTER_NEAREST, 0 },
    { "SHARP/NEAREST",   COMPOSE_FILTER_SHARP,   0 },
    { "SHARP/SHARP",     COMPOSE_FILTER_SHARP,   2 },
    { "SHARP/XBRZ-S",    COMPOSE_FILTER_SHARP,   4 },
    { "SCALE2X/XBRZ-S",  COMPOSE_FILTER_SCALE2X, 4 },
};
#define FILTER_PRESETS ((int)(sizeof(sFilterPresets) / sizeof(sFilterPresets[0])))
static int sFilterPreset = 1;                 /* (the default: SHARP/NEAREST) */
static bool sFilterCombo;                     /* (X+R held on the frame before) */
/* START+SELECT held to leave for the Homebrew Launcher: the tick the combo
 * started, or 0 when it is not held. Compared against EXIT_HOLD_TICKS below. */
#define EXIT_HOLD_SECONDS 2
#define EXIT_HOLD_TICKS ((u64)EXIT_HOLD_SECONDS * SYSCLOCK_ARM11)
static u64 sExitHoldStart;
/* The toast: what it says, when it goes (ticks; 0 none), and that a drawer
 * must redraw the lower screen. The game thread sets it, a drawer draws it. */
#define TOAST_SECONDS 2
#define TOAST_TICKS ((u64)TOAST_SECONDS * SYSCLOCK_ARM11)
static const char *sToastLabel;
static volatile u64 sToastUntil;
static volatile bool sToastDirty;

/* Half-A lower-screen experiments.  All are off unless explicitly selected
 * in port.ini; the ordinary build retains the established policy. */
static int sIniLower30;
static int sIniDrawerAB;
static int sIniRefHashB;
static int sIniRefHashA;
static int sIniRefHashDiag;
static int sIniPartialB;
static int sIniPartialBDiag;
static int sIniPartialBChar;
static int sIniBandPubB;
static int sIniBandPubBDiag;
/* sw2d=1: force full software raster (no GPU-BG takes). Measurement-only
 * Azahar campaign switch, default off; the taken path stays default. */
static int sIniSw2d;

#ifdef HGSS_A0_DIAG
/* A0 thread topology (O3THR-0): first line of each worker, logging the
 * effective core and priority. Requested placement is static in the
 * creation calls below; a hardware drop proves by these lines which
 * core-1 workers actually exist and run. */
static void A0ThreadRunning(const char *role)
{
    s32 prio = -1;

    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    NitroPort_Log("[a0] thread %s: running on core %d at prio 0x%02x",
                  role, (int)svcGetProcessorID(), (unsigned)prio);
}
#endif


static bool OnTop(int engine, bool aOnTop)
{
    return (engine == 0) == aOnTop;
}

uint32_t NitroPort_Gx3dStamp(void)
{
    return NitroPort_ComposeVBlanks();
}

static void VBlankMain(void *unused)
{
    (void)unused;
    A0ThreadRunning("vblank");
    while (__atomic_load_n(&sVBlankRunning, __ATOMIC_ACQUIRE)) {
        gspWaitForVBlank();
        if (!__atomic_load_n(&sVBlankRunning, __ATOMIC_ACQUIRE)) {
            break;
        }
        NitroPort_ComposeVBlank();
        if (__atomic_load_n(&sGpuWakeReady, __ATOMIC_ACQUIRE)) {
            LightEvent_Signal(&sGpuWake);
        }
    }
    threadExit(0);
}

static bool VBlankStart(s32 priority)
{
    __atomic_store_n(&sVBlankRunning, true, __ATOMIC_RELEASE);
    sVBlankThread = threadCreate(VBlankMain, NULL, 16 * 1024, priority - 3, 0, false);
    if (sVBlankThread == NULL) {
        __atomic_store_n(&sVBlankRunning, false, __ATOMIC_RELEASE);
        NitroPort_Log("[compose] couldn't start the V-blank thread");
        return false;
    }
    return true;
}

static void VBlankStop(void)
{
    if (sVBlankThread == NULL) {
        return;
    }
    __atomic_store_n(&sVBlankRunning, false, __ATOMIC_RELEASE);
    if (R_SUCCEEDED(threadJoin(sVBlankThread, 1000 * 1000000LL))) {
        threadFree(sVBlankThread);
    } else {
        sGpuStopFailed = true;
    }
    sVBlankThread = NULL;
}

/* The toast's own 5x7 font: A-Z, 0-9, space, '/' and '-' (all the preset
 * names use). One byte a row, five bits across, top row first. */
typedef struct ToastGlyph {
    char c;
    uint8_t rows[7];
} ToastGlyph;
static const ToastGlyph sToastFont[] = {
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { '/', { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
};

static const ToastGlyph *toast_glyph(char c)
{
    unsigned i;

    for (i = 0; i < sizeof(sToastFont) / sizeof(sToastFont[0]); i++) {
        if (sToastFont[i].c == c) {
            return &sToastFont[i];
        }
    }
    return NULL;   /* (space: no glyph) */
}

/* One toast pixel into the lower screen's layers: the picture behind the 3D
 * always, and in front of it (opaque) when the picture is split. */
static void toast_set(ComposeLayers *layers, int x, int y, uint16_t color, int has3d)
{
    int i;

    if (x < 0 || y < 0 || x >= layers->width || y >= layers->height) {
        return;
    }
    i = y * layers->width + x;
    layers->under[i] = color;
    if (has3d) {
        layers->over[i] = (uint16_t)(0x8000u | color);
        layers->overAlpha[i] = 16;
    }
}

/* The preset name in a dark box at the bottom of the 256-wide picture. */
static void toast_draw(ComposeLayers *layers)
{
    static const uint16_t box = 0x0421;   /* (RGB555: a very dark grey) */
    static const uint16_t text = 0x7FFF;  /* (white) */
    const char *label = __atomic_load_n(&sToastLabel, __ATOMIC_ACQUIRE);
    int len = label != NULL ? (int)strlen(label) : 0;
    int cw = 6, pad = 2, boxW, boxH, x0, y0, x, y, i;
    int has3d = layers->has3d && layers->over != NULL && layers->overAlpha != NULL;

    if (len <= 0 || layers->under == NULL || layers->width <= 0 || layers->height <= 0) {
        return;
    }
    boxW = len * cw - 1 + 2 * pad;
    boxH = 7 + 2 * pad;
    if (boxW > layers->width) {
        boxW = layers->width;
    }
    if (boxH > layers->height) {
        boxH = layers->height;
    }
    x0 = (layers->width - boxW) / 2;
    y0 = layers->height - boxH - 2;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    for (y = y0; y < y0 + boxH; y++) {
        for (x = x0; x < x0 + boxW; x++) {
            toast_set(layers, x, y, box, has3d);
        }
    }
    x = x0 + pad;
    y = y0 + pad;
    for (i = 0; i < len; i++) {
        const ToastGlyph *g = toast_glyph(label[i]);
        int row, col;

        if (g != NULL) {
            for (row = 0; row < 7; row++) {
                for (col = 0; col < 5; col++) {
                    if (g->rows[row] & (1u << (4 - col))) {
                        toast_set(layers, x + col, y + row, text, has3d);
                    }
                }
            }
        }
        x += cw;
    }
}

static bool RenderScreen(int engine, bool aOnTop, u32 stamp)
{
    bool top = OnTop(engine, aOnTop);
    ComposeLayers *layers = NitroPort_ComposeLayersOf(engine);
    NitroRender2DSplit split;
    int drawn;

    memset(&split, 0, sizeof(split));
    split.over = layers->over;
    split.overAlpha = layers->overAlpha;
    split.snapshot = 1;
    split.windowsToEdge = 1;
    if (top && sCompose.wide) {
        NitroPort_WideLayout(engine, &split);
        /* The field's repeating weather BGs: faster scrolled on the GPU
         * with the 3D than redrawn in the picture every frame. S5c: the
         * port also nominates text BGs 1-3 (GpuBgOf proves each one;
         * untaken ones draw in software exactly as before). */
        if (engine == 0 && NitroPort_ComposeDrawsBgs()) {
            /* sw2d=1 (measurement only): no GPU-BG takes, full software
             * raster every offered frame, so every publish carries a new
             * picture. Reference path by construction; costs drawer CPU. */
            split.gpuBgs = sIniSw2d ? 0 : NitroPort_RepeatingBgs() | NitroPort_NominateGpuBgs();
        }
    }
    if (!top && __atomic_exchange_n(&sToastDirty, false, __ATOMIC_ACQ_REL)) {
        /* (the preset changed, or the toast expired: draw the lower picture
         * again so the toast comes or goes) */
        sMustDraw[1] = true;
    }
    if (sMustDraw[top ? 0 : 1]) {
        sMustDraw[top ? 0 : 1] = false;
#ifdef HGSS_A0_DIAG
        __atomic_fetch_add(&sA0MustDraw[top ? 0 : 1], 1u, __ATOMIC_RELAXED);
#endif
        NitroPort_Render2DSplit(engine, layers->under, &split);
        drawn = 1;
    } else {
        drawn = NitroPort_Render2DSplitIfChanged(engine, layers->under, &split);
    }
    if (!drawn) {
        int bright = NitroPort_Render2DMasterBright(engine, 1);

        if (!top) layers->flatCpuPlanes = 0; /* lower toast can modify these planes */

        /* A fade changes only the GPU brightness. Publish the untouched
         * layers again so the compositor sees the new value, without
         * redrawing the 320x192 picture. */
        if (bright != layers->masterBright) {
            layers->masterBright = bright;
            layers->stamp = stamp;
            layers->displayOff = 0;
            return true;
        }
        return false;
    }
    layers->has3d = split.has3d && layers->over != NULL;
    layers->scroll3d = split.scroll3d;
    layers->scroll3dLines = split.scroll3dLines;
    if (split.scroll3dLines) {
        memcpy(layers->scroll3dLine, split.scroll3dLine, sizeof(layers->scroll3dLine));
    }
    layers->tint3d = split.tint3d;
    layers->eva = split.eva;
    layers->evb = split.evb;
    layers->gpuQuads = split.nGpuQuads;
    layers->gpuQuadsBehind = split.gpuQuadsBehind;
    memcpy(layers->gpuQuad, split.gpuQuads,
           (size_t)split.nGpuQuads * sizeof(layers->gpuQuad[0]));
    for (int slot = 0; slot < NITRO_GPU_BG_SLOTS; slot++) {
        layers->gpuBgSlotBg[slot] = split.gpuBgSlotBg[slot];
        layers->gpuBgW[slot] = split.gpuBgW[slot];
        layers->gpuBgH[slot] = split.gpuBgH[slot];
        layers->gpuBgWrap[slot] = split.gpuBgWrap[slot];
        layers->gpuBgKey[slot] = split.gpuBgKey[slot];
        layers->gpuBgPicture[slot] = NULL;
        if (split.gpuBgSlotBg[slot] < 0) {
            continue;
        }
        /* S5/S6 bundle: the slot's whole map (up to 512 x 512), made again
         * when its key (map, tiles, colours: not the scroll) changes. */
        static uint16_t picture[NITRO_GPU_BG_SLOTS][512 * 512];
        static uint64_t pictureKey[NITRO_GPU_BG_SLOTS];
        static int pictureValid[NITRO_GPU_BG_SLOTS];

        if (!pictureValid[slot] || pictureKey[slot] != split.gpuBgKey[slot]) {
            pictureValid[slot] = NitroPort_Render2DBgPicture(split.gpuBgSlotBg[slot], 1, picture[slot]);
            pictureKey[slot] = split.gpuBgKey[slot];
        }
        if (pictureValid[slot]) {
            layers->gpuBgPicture[slot] = picture[slot];
        }
    }
    layers->gpuObjAtlasPicture = split.gpuObjAtlasPicture;
    layers->gpuObjAtlasKey = split.gpuObjAtlasKey;
    layers->evy = split.evy;
    layers->masterBright = split.masterBright;
    layers->backdrop = split.backdrop;
    layers->flatCpuPlanes = top && split.flatCpuPlanes;
    layers->width = split.width > 256 ? split.width : 256;
    layers->height = split.height > 192 ? split.height : 192;
    layers->top = split.top;
    layers->stretch = split.stretch;
    layers->stamp = stamp;
    layers->displayOff = 0;
    if (!top) {
        u64 until = __atomic_load_n(&sToastUntil, __ATOMIC_ACQUIRE);

        if (until != 0 && svcGetSystemTick() < until) {
            toast_draw(layers);
        }
    }
    return true;
}

static bool ShowScreen(int engine, bool aOnTop)
{
    bool top = OnTop(engine, aOnTop);
    bool published = NitroPort_ComposePublish(engine, top ? 0 : 1) != 0;

    if (!published) {
        sMustDraw[top ? 0 : 1] = true;
    }
    return published;
}

static void DrawerMain(void *arg)
{
    Drawer *d = arg;

    A0ThreadRunning("drawer");

    for (;;) {
        u64 start;
        bool drawA = false, drawB = false;

        LightEvent_Wait(&d->go);
        if (d->quit) {
            break;
        }
        if (!__atomic_load_n(&d->busy, __ATOMIC_ACQUIRE)) {
            continue;
        }
        start = svcGetSystemTick();
#ifndef HGSS_PROFILE_OFF
        /* P1: offer-to-start latency (see signalTick). */
        __atomic_fetch_add(&d->signalLatency,
                           start - __atomic_load_n(&d->signalTick, __ATOMIC_ACQUIRE),
                           __ATOMIC_RELAXED);
        __atomic_fetch_add(&d->signalCount, 1u, __ATOMIC_RELAXED);
#endif
        if (d->screens & ENGINE_A) {
            u64 t = svcGetSystemTick();
            int screen = OnTop(0, d->aOnTop) ? 0 : 1;

            drawA = RenderScreen(0, d->aOnTop, d->stamp);
            t = svcGetSystemTick() - t;
            __atomic_fetch_add(&d->renderTicks, t, __ATOMIC_RELAXED);
            __atomic_fetch_add(&d->screenRenderTicks[screen], t, __ATOMIC_RELAXED);
            __atomic_fetch_add(&d->screenRenderCalls[screen], 1u, __ATOMIC_RELAXED);
        }
        /* Publish A before doing any lower-screen work.  On the single
         * Old-3DS drawer this is the important ordering: a slow B render may
         * not hold an otherwise complete upper picture hostage. */
        __atomic_store_n(&d->reading, false, __ATOMIC_RELEASE);
        if (d->screens & ENGINE_A) {
            u64 t = svcGetSystemTick();

            if ((drawA ? ShowScreen(0, d->aOnTop) :
                 !sMustDraw[OnTop(0, d->aOnTop) ? 0 : 1])) {
                NitroPort_Compose2DCurrent(OnTop(0, d->aOnTop) ? 0 : 1, d->stamp);
            }
            __atomic_fetch_add(&d->publishTicks, svcGetSystemTick() - t, __ATOMIC_RELAXED);
        }
        if (d->screens & ENGINE_A) {
            NitroPort_Compose2DDone(d->stamp);
        }
        if ((d->screens & ENGINE_B) && d->offerB) {
            u64 t = svcGetSystemTick();
            int screen = OnTop(1, d->aOnTop) ? 0 : 1;
            u32 now = NitroPort_ComposeVBlanks();
            u32 age = now - d->stamp;
            /* A deferred lower screen contributes no fresh picture. */
            drawB = false;
            int coherent = !sIniDrawerAB || d->lastAOnTop != d->aOnTop ||
                           NitroPort_Render2DMasterBright(0, 1) !=
                           NitroPort_Render2DMasterBright(1, 1);

            /* A newly available upper snapshot wins for at most two ticks.
             * At age three B is forced, and swaps/fades always take the pair. */
            if (sIniDrawerAB && age > 0 && age < 3 && !coherent) {
                __atomic_fetch_add(&d->bSkips, 1u, __ATOMIC_RELAXED);
                d->offerB = false;
                drawB = false;
            } else {
                drawB = RenderScreen(1, d->aOnTop, d->stamp);
                d->lastBStamp = d->stamp;
                d->lastAOnTop = d->aOnTop;
            }
            if (d->offerB) {
                u64 t2 = svcGetSystemTick() - t;
                __atomic_fetch_add(&d->renderTicks, t2, __ATOMIC_RELAXED);
                __atomic_fetch_add(&d->screenRenderTicks[screen], t2, __ATOMIC_RELAXED);
                __atomic_fetch_add(&d->screenRenderCalls[screen], 1u, __ATOMIC_RELAXED);
            }
            t = svcGetSystemTick();
            if (d->offerB && (drawB ? ShowScreen(1, d->aOnTop) :
                              !sMustDraw[OnTop(1, d->aOnTop) ? 0 : 1])) {
                NitroPort_Compose2DCurrent(OnTop(1, d->aOnTop) ? 0 : 1, d->stamp);
            }
            __atomic_fetch_add(&d->publishTicks, svcGetSystemTick() - t, __ATOMIC_RELAXED);
        }
        __atomic_fetch_add(&d->ticks, svcGetSystemTick() - start, __ATOMIC_RELAXED);
        __atomic_fetch_add(&d->count, 1u, __ATOMIC_RELAXED);
        __atomic_fetch_add(&d->drawn, (unsigned)drawA + (unsigned)drawB, __ATOMIC_RELAXED);
        /* Before busy is cleared: the game can hand this drawer another
         * frame as soon as it is false, and that frame has its own count. */
        if (__atomic_sub_fetch(&sDrawFramePending, 1, __ATOMIC_ACQ_REL) == 0 &&
            __atomic_load_n(&sGpuWakeReady, __ATOMIC_ACQUIRE)) {
            sDbgWakes++;
            LightEvent_Signal(&sGpuWake);
        }
        __atomic_store_n(&d->busy, false, __ATOMIC_RELEASE);
    }
    threadExit(0);
}

static bool StartDrawer(int screens, s32 priority, int core)
{
    Drawer *d = &sDrawers[sDrawerCount];

    memset(d, 0, sizeof(*d));
    LightEvent_Init(&d->go, RESET_ONESHOT);
    d->screens = screens;
    d->core = core;
    d->priority = priority;
    d->thread = threadCreate(DrawerMain, d, 64 * 1024, priority, core, false);
    if (d->thread == NULL && core != 0) {
        d->thread = threadCreate(DrawerMain, d, 64 * 1024, priority, 0, false);
        d->core = 0;
    }
    if (d->thread == NULL) {
        NitroPort_Log("[compose] couldn't start a drawing thread (core %d)", core);
        return false;
    }
    sDrawerCount++;
    return true;
}

static bool DrawersStart(s32 priority, bool new3ds)
{
    if (new3ds) {
        bool a = StartDrawer(ENGINE_A, priority + 1, 2);
        bool b = StartDrawer(ENGINE_B, priority + 2, 0);
        if (!a || !b) {
            return false;
        }
    } else {
        if (!StartDrawer(ENGINE_A | ENGINE_B, priority + 1, 0)) {
            return false;
        }
    }
    NitroPort_Log("[compose] drawing: engine A on core %d, engine B on core %d",
                  sDrawers[0].core, sDrawerCount > 1 ? sDrawers[1].core : sDrawers[0].core);
    return true;
}

static void DrawersStop(void)
{
    int i;

    for (i = 0; i < sDrawerCount; i++) {
        Drawer *d = &sDrawers[i];

        d->quit = true;
        LightEvent_Signal(&d->go);
        if (R_SUCCEEDED(threadJoin(d->thread, 1000000000ULL))) {
            threadFree(d->thread);
        } else {
            sGpuStopFailed = true;
            NitroPort_Log("[compose] a drawing thread didn't stop");
        }
    }
    sDrawerCount = 0;
}

/* Hand this frame's display state to every free drawer. The game waits only
 * for the state copy (NitroPort_Render2DTakeState), not for the drawing. */
static bool DrawFrame(void)
{
    u32 images = NitroPort_Gx3dImageCount();
    int i;
    bool any = false;
    bool take[2] = { false, false };
    u64 phase;

    if (__atomic_load_n(&sSuspended, __ATOMIC_SEQ_CST)) {
        return false;
    }
    /* Three timers, not one: the log only knew "the hand-off took 13.5 ms on
     * an Old 3DS", which every audit then attributed to a different phase.
     * Two ticks per phase is free enough to leave in. */
    phase = svcGetSystemTick();
    NitroPort_Compose2DOffered(NitroPort_ComposeVBlanks());
    sAOnTop = NitroPort_EngineAOnTop() != 0;
    /* The frame's H-blank lines, once a V-blank: the battles' start slides
     * the 3D's lines apart with them. The weather BGs the compositor draws
     * itself follow this frame's scrolls whether a drawer takes it or not. */
    (void)NitroPort_HBlankLinesOnce();
    if (NitroPort_ComposeDrawsBgs()) {
        uint16_t ofs[8];

        NitroPort_Render2DBgOffsets(ofs);
        NitroPort_ComposeNoteBgOffsets(NitroPort_ComposeVBlanks(), ofs);
    }
    sFrameHBlankTicks = svcGetSystemTick() - phase;
    NitroPort_HBlankSplit(&sFrameHbKeep, &sFrameHbDma, &sFrameHbIrq);
#ifndef HGSS_PROFILE_OFF
    NitroPort_HBlankSplit2(&sFrameHbRefresh, &sFrameHbSwitch, &sFrameHbCmp);
#endif
    phase = svcGetSystemTick();
    for (i = 0; i < sDrawerCount; i++) {
        if (!__atomic_load_n(&sDrawers[i].busy, __ATOMIC_ACQUIRE)) {
            any = true;
            take[i] = true;
        }
    }
    if (!any) {
#ifndef HGSS_PROFILE_OFF
        /* P1: the offer died here (every drawer busy). */
        sDrawDropped++;
#endif
        sFrameHandoffTicks = svcGetSystemTick() - phase;
        return false;
    }
    for (i = 0; i < sDrawerCount; i++) {
        Drawer *d = &sDrawers[i];

        if (take[i]) {
            /* Add, don't store: a busy drawer from the frame before may
             * still be about to decrement its own count. */
            __atomic_add_fetch(&sDrawFramePending, 1, __ATOMIC_ACQ_REL);
            d->aOnTop = sAOnTop;
            d->stamp = NitroPort_ComposeVBlanks();
            d->offerB = !sIniLower30 || ((d->stamp & 1u) == 0);
            d->images = images;
            if (d->screens & ENGINE_A) {
                NitroPort_Render2DTakeState(0);
                NitroPort_Compose2DHanded(d->stamp);
            }
            if ((d->screens & ENGINE_B) && d->offerB) {
                NitroPort_Render2DTakeState(1);
            }
            __atomic_store_n(&d->reading, true, __ATOMIC_RELEASE);
            __atomic_store_n(&d->busy, true, __ATOMIC_RELEASE);
#ifndef HGSS_PROFILE_OFF
            __atomic_store_n(&d->signalTick, svcGetSystemTick(), __ATOMIC_RELEASE);
#endif
            LightEvent_Signal(&d->go);
        }
    }
    sFrameHandoffTicks = svcGetSystemTick() - phase;
    return true;
}

static void GpuComposeLoop(void)
{
    u32 seen = 0;
    const NitroGx3dScene *scene = NULL;
    /* This scene's own frame was drawn on the 3D's screen (or there is no
     * 3D to show); otherwise it waits for its 2D or an LCD framebuffer and
     * is drawn again at a later wake. The newest scene is kept and composed
     * again whenever the 2D changes: a text box appearing over a still field
     * must not wait for new 3D. */
    bool sceneDone = true;

    while (__atomic_load_n(&sGpuRunning, __ATOMIC_ACQUIRE)) {
        bool aOnTop, forced;
        int bit, noBuf;

        LightEvent_Wait(&sGpuWake);
        if (!__atomic_load_n(&sGpuRunning, __ATOMIC_ACQUIRE)) {
            break;
        }
        sDbgLoopIters++;
        if (__atomic_load_n(&sSuspended, __ATOMIC_SEQ_CST)) {
            if (scene != NULL) {
                NitroPort_Gx3dReleaseScene(scene);
                scene = NULL;
            }
            sceneDone = true;
            continue;
        }
        aOnTop = NitroPort_EngineAOnTop() != 0;
        forced = __atomic_exchange_n(&sComposeForce, false, __ATOMIC_ACQ_REL);
        bit = aOnTop ? COMPOSE_DRAWN_UPPER : COMPOSE_DRAWN_LOWER;
        noBuf = aOnTop ? COMPOSE_NO_BUFFER_UPPER : COMPOSE_NO_BUFFER_LOWER;
        if (scene == NULL || NitroPort_Gx3dNewerScene(seen)) {
            if (scene != NULL && !sceneDone) {
                /* Still waiting (for its 2D, or a framebuffer): drawn now
                 * with the 2D there is, before the newer one takes its
                 * place. Not on for a second newer scene (late already),
                 * and not if no framebuffer is free (the next V-blank). */
                int r = 0;
                bool suspended;

                /* Busy first, then the check: with the hook's suspended-first
                 * order this is a Dekker handshake. No frame may start once
                 * APT has begun taking the GPU. */
                __atomic_store_n(&sComposing, true, __ATOMIC_SEQ_CST);
                suspended = __atomic_load_n(&sSuspended, __ATOMIC_SEQ_CST);
                if (!suspended) {
                    r = NitroPort_ComposeFrame(scene, seen, aOnTop, osGet3DSliderState(),
                                               forced, 1, 0);
                }
                __atomic_store_n(&sComposing, false, __ATOMIC_RELEASE);
                if (suspended) {
                    NitroPort_Gx3dReleaseScene(scene);
                    scene = NULL;
                    sceneDone = true;
                    continue;
                }
                if (!(r & bit) && (r & noBuf) &&
                    NitroPort_Gx3dNewerScenes(seen) < 2u) {
                    if (forced) {
                        __atomic_store_n(&sComposeForce, true, __ATOMIC_RELEASE);
                    }
                    continue;
                }
            }
            if (scene != NULL) {
                NitroPort_Gx3dReleaseScene(scene);
                scene = NULL;
            }
            scene = NitroPort_Gx3dAcquireScene(&seen);
            if (scene != NULL) {
                sceneDone = false;
                sGpuScenes++;
            }
        }
        {
            /* scene may be NULL (a menu, the intro before its first 3D):
             * the compositor still draws the 2D layers that changed. */
            int r = 0;
            bool suspended;

            __atomic_store_n(&sComposing, true, __ATOMIC_SEQ_CST);
            suspended = __atomic_load_n(&sSuspended, __ATOMIC_SEQ_CST);
            if (!suspended) {
                r = NitroPort_ComposeFrame(scene, seen, aOnTop, osGet3DSliderState(),
                                           forced, 0, 0);
            }
            __atomic_store_n(&sComposing, false, __ATOMIC_RELEASE);
            if (suspended) {
                continue;   /* (the top of the loop releases the scene) */
            }
            sceneDone = scene == NULL || (r & bit) ||
                        (!(r & noBuf) && !NitroPort_ComposeSceneWaiting(seen));
        }
    }
    if (scene != NULL) {
        NitroPort_Gx3dReleaseScene(scene);
    }
}

/* Once a frame, from the game's idle path: keep the HOME menu serviced, feed
 * the buttons/touch and draw. */
static unsigned long long TicksToUs(u64 ticks)
{
    return (unsigned long long)ticks * 1000000ULL / SYSCLOCK_ARM11;
}

/* --------------------------------------------------------------- [perf]
 *
 * Once every 600 frames the game's thread samples the display, geometry and
 * audio counters and logs their deltas. The drawer threads own their own
 * counters; the APIs read here are the ones documented as safe from any
 * thread (and only read cumulative totals or reset the per-call peaks). */
typedef struct PerfDisplay {
    u32 frames, published, timed, late, dropped, stereo, smooth;
    u64 cpuTicks, gpuTicks, waitTicks, publishTicks;
    u64 buildTicks, sceneWaitTicks, scenePublishTicks;
    u32 texConverted;
    u32 bgFullUploads, bgDeltaUpdates, bgDirtyTiles, bgFlushedBytes;
    u64 texels, texTicks;
} PerfDisplay;

typedef struct PerfGeometry {
    u64 busy;
    u32 scenes, skipped;
    u64 producerWait;
    u32 wakeups;
    u32 flowQueued, flowDone, flowDropped;
#ifndef HGSS_PROFILE_OFF
    /* P1: built vs skipped split (NitroPort_Gx3dSceneSplit). */
    u32 built;
    u64 builtTicks, skippedTicks;
#endif
} PerfGeometry;

static bool sPerfHave;
static u64 sPerfLastTick;
static u64 sPerfSoundTicks;
static u32 sPerfAudioDropped;
static u32 sPerfAudioFrames;
/* Slice A: the 2D draw profile baselines (NitroPort_Render2DProfile). */
static NitroRender2DProfile sPerf2d[2];
static ComposePublishProfile sPerfPublish[2];
/* Slice S4: H-blank attribution baselines (cumulative counters only; the
 * per-IRQ-bit table is last-walk state and is read as-is). */
static u64 sPerfSyncCalls, sPerfSyncTicks, sPerfPumpTicks;
static u32 sPerfPumpCalls, sPerfDmaXfers;
/* The audio thread's mixer time (written there, read at the log). */
static volatile u64 sSoundMixTicks;
#ifndef HGSS_PROFILE_OFF
/* P1: DrawFrame-drop baseline (see the [perf] offers line). */
static unsigned sPerfDrawDropped;
#endif
static PerfDisplay sPerfDisplay;
static PerfGeometry sPerfGeometry;
/* P3: the flat picture's pass baselines (see the [perf] flatskip line). */
static NitroFlatStats sPerfFlat;
#if defined(HGSS_A0_DIAG) || !defined(HGSS_PROFILE_OFF)
/* P1: idle-probe baselines (see the [perf] idle line). */
static u64 sPerfIdleCount[2], sPerfIdleSpan;
#endif
#ifndef HGSS_PROFILE_OFF
/* P1: game-thread accounting baselines (see the [perf] game line). */
static u64 sPerfGameWork, sPerfGameSleep;
static u32 sPerfGameLate;
#endif

static double PerfPerSecond(u32 count, double seconds)
{
    return seconds > 0.0 ? (double)count / seconds : 0.0;
}

static double PerfMsPer(u64 ticks, double count)
{
    return count > 0.0 ? (double)ticks * 1000.0 / (double)SYSCLOCK_ARM11 / count : 0.0;
}

#ifdef HGSS_TEXTURE_PROFILE
/* Cumulative since boot so the first report includes cold-start conversions.
 * Allocation includes eviction/wait; wait overlaps allocation. Quantization
 * is 256 ticks per call, and counters wrap modulo 2^32 units. */
static void PerfTextureProfile(void)
{
    PicaTextureProfile p;
    const double ms=256000.0/(double)SYSCLOCK_ARM11;
    NitroPort_PicaTextureProfile(&p);
    NitroPort_Log("[texprof] cumulative lookups %lu hashes %lu conversions %lu new %lu evictions %lu failures %lu",
                  p.lookups,p.hashes,p.conversions,p.newEntries,p.evictions,p.failures);
    NitroPort_Log("[texprof] cumulative wall ms search %.2f hash %.2f alloc %.2f wait %.2f convert %.2f decode %.2f flush %.2f (alloc includes wait; convert includes decode)",
                  p.search*ms,p.hash*ms,p.allocation*ms,p.wait*ms,p.convert*ms,p.decode*ms,p.flush*ms);
    NitroPort_Log("[texprof] worst miss wall %.2f ms param %08lx bytes %lu (cumulative; snapshot may straddle conversion)",
                  p.worst*ms,p.worstParam,p.worstBytes);
}
#endif

static void PerfLog(u64 now)
{
    u64 build, wait, publish, texels, texTicks, busy, producerWait, longest;
    u64 soundTicks;
    u32 scenes, skipped, wakeups, converted;
    u32 flowQueued, flowDone, flowDropped;
#ifndef HGSS_PROFILE_OFF
    u32 built = 0;
    u64 builtTicks = 0, skippedTicks = 0;
#endif
    ComposeStats st;
    float cmdPeak;
    unsigned cmdCut;
    double seconds;
    int i;

    NitroPort_PicaTimes(&build, &wait, &publish);
    NitroPort_PicaTextureStats(&converted, &texels, &texTicks);
#ifdef HGSS_TEXTURE_PROFILE
    PerfTextureProfile();
#endif
    NitroPort_ComposeStats(&st);
    NitroPort_PicaCmdStats(&cmdPeak, &cmdCut);
    NitroPort_Gx3dThreadStats(&busy, &scenes, &skipped);
#ifndef HGSS_PROFILE_OFF
    NitroPort_Gx3dSceneSplit(&built, &builtTicks, &skippedTicks);
#endif
    producerWait = NitroPort_Gx3dProducerWaitMicros();
    NitroPort_Gx3dThreadWakeups(&wakeups, &longest);
    NitroPort_Gx3dSceneFlow(&flowQueued, &flowDone, &flowDropped);
    soundTicks = __atomic_load_n(&sSoundMixTicks, __ATOMIC_RELAXED);
    {
        /* Display-gating diagnostics: loop passes vs draws vs skip reasons
         * (port_compose_pica.c) plus loop passes and wake signals here.
         * First line is cumulative since boot; later lines are intervals. */
        static u64 lastIters, lastWakes, lastNoNeed, lastWait2D, lastMidLate,
            lastNoBuf, lastDraws, lastPick, lastPrepare, lastStage, lastEnd,
            lastSkip, lastNeedA, lastNeedB, lastDrawsScene, lastSerialSkipped;
#ifndef HGSS_PROFILE_OFF
        /* P1: compose-cause + end-split baselines. */
        static u64 lastCauseScene, lastCauseSet, lastCauseForced, lastCauseOther,
            lastEndXfer, lastEndFlight, lastEndLock, lastEndSubmit;
#endif
        static u32 lastVBlanks;
        u32 vblanks = NitroPort_ComposeVBlanks();
        u64 noNeed, wait2D, midLate, noBuf, draws;
        u64 pickTicks, prepareTicks, stageTicks, endTicks, skipTicks;
        u64 needA, needB, drawsScene, serialSkipped;
        u64 dDraws, dSkips;
#ifndef HGSS_PROFILE_OFF
        u64 causeScene, causeSet, causeForced, causeOther;
        u64 endXfer, endFlight, endLock, endSubmit;
#endif

        NitroPort_ComposeDbgStats(&noNeed, &wait2D, &midLate, &noBuf, &draws,
                                  &pickTicks, &prepareTicks, &stageTicks,
                                  &endTicks, &skipTicks, &needA, &needB,
                                  &drawsScene, &serialSkipped);
#ifndef HGSS_PROFILE_OFF
        NitroPort_ComposeCauseStats(&causeScene, &causeSet, &causeForced, &causeOther,
                                    &endXfer, &endFlight, &endLock, &endSubmit);
#endif
        dDraws = draws - lastDraws;
        dSkips = noNeed - lastNoNeed;
        NitroPort_Log("[perf] loop: %llu passes (%llu draws, %llu nonew, %llu wait2d, %llu midlate, %llu nobuf), %llu wakes; need upper %llu lower %llu; %u vblanks; scene-draws %llu serials-skipped %llu",
                      (unsigned long long)(sDbgLoopIters - lastIters),
                      (unsigned long long)dDraws,
                      (unsigned long long)dSkips,
                      (unsigned long long)(wait2D - lastWait2D),
                      (unsigned long long)(midLate - lastMidLate),
                      (unsigned long long)(noBuf - lastNoBuf),
                      (unsigned long long)(sDbgWakes - lastWakes),
                      (unsigned long long)(needA - lastNeedA),
                      (unsigned long long)(needB - lastNeedB),
                      (unsigned)(vblanks - lastVBlanks),
                      (unsigned long long)(drawsScene - lastDrawsScene),
                      (unsigned long long)(serialSkipped - lastSerialSkipped));
        lastVBlanks = vblanks;
        lastNeedA = needA;
        lastNeedB = needB;
        lastDrawsScene = drawsScene;
        lastSerialSkipped = serialSkipped;
        NitroPort_Log("[perf] compose-phase ms/draw: pick %.2f prepare %.2f stage %.2f end %.2f"
#ifndef HGSS_PROFILE_OFF
                      " (xfer %.2f flight %.2f lock %.2f submit %.2f)"
#endif
                      "; skip %.2f ms/pass"
#ifndef HGSS_PROFILE_OFF
                      "; cause scene %llu set %llu forced %llu other %llu"
#endif
                      ,
                      dDraws ? (double)(pickTicks - lastPick) * 1000.0 / (double)SYSCLOCK_ARM11 / (double)dDraws : 0.0,
                      dDraws ? (double)(prepareTicks - lastPrepare) * 1000.0 / (double)SYSCLOCK_ARM11 / (double)dDraws : 0.0,
                      dDraws ? (double)(stageTicks - lastStage) * 1000.0 / (double)SYSCLOCK_ARM11 / (double)dDraws : 0.0,
                      dDraws ? (double)(endTicks - lastEnd) * 1000.0 / (double)SYSCLOCK_ARM11 / (double)dDraws : 0.0,
#ifndef HGSS_PROFILE_OFF
                      dDraws ? (double)(endXfer - lastEndXfer) * 1000.0 / (double)SYSCLOCK_ARM11 / (double)dDraws : 0.0,
                      dDraws ? (double)(endFlight - lastEndFlight) * 1000.0 / (double)SYSCLOCK_ARM11 / (double)dDraws : 0.0,
                      dDraws ? (double)(endLock - lastEndLock) * 1000.0 / (double)SYSCLOCK_ARM11 / (double)dDraws : 0.0,
                      dDraws ? (double)(endSubmit - lastEndSubmit) * 1000.0 / (double)SYSCLOCK_ARM11 / (double)dDraws : 0.0,
#endif
                      dSkips ? (double)(skipTicks - lastSkip) * 1000.0 / (double)SYSCLOCK_ARM11 / (double)dSkips : 0.0
#ifndef HGSS_PROFILE_OFF
                      , (unsigned long long)(causeScene - lastCauseScene),
                      (unsigned long long)(causeSet - lastCauseSet),
                      (unsigned long long)(causeForced - lastCauseForced),
                      (unsigned long long)(causeOther - lastCauseOther)
#endif
                      );
        lastIters = sDbgLoopIters;
        lastWakes = sDbgWakes;
        lastNoNeed = noNeed;
        lastWait2D = wait2D;
        lastMidLate = midLate;
        lastNoBuf = noBuf;
        lastDraws = draws;
        lastPick = pickTicks;
        lastPrepare = prepareTicks;
        lastStage = stageTicks;
        lastEnd = endTicks;
        lastSkip = skipTicks;
#ifndef HGSS_PROFILE_OFF
        lastCauseScene = causeScene;
        lastCauseSet = causeSet;
        lastCauseForced = causeForced;
        lastCauseOther = causeOther;
        lastEndXfer = endXfer;
        lastEndFlight = endFlight;
        lastEndLock = endLock;
        lastEndSubmit = endSubmit;
#endif
    }

    if (!sPerfHave) {
        /* The first log after the counters start is the baseline; the next
         * one has a complete interval to divide by. */
        sPerfHave = true;
        sPerfLastTick = now;
#ifdef HGSS_A0_DIAG
        for (i = 0; i < 2; i++)
            sA0IdleLast[i] = __atomic_load_n(&sA0IdleCounter[i].count, __ATOMIC_RELAXED);
#endif
        sPerfSoundTicks = soundTicks;
        sPerfAudioDropped = (u32)ndspGetDroppedFrames();
        sPerfAudioFrames = (u32)ndspGetFrameCount();
        NitroPort_SyncTimersStats(&sPerfSyncCalls, &sPerfSyncTicks);
        NitroPort_SoundPumpTickStats(&sPerfPumpTicks, &sPerfPumpCalls);
        sPerfDmaXfers = NitroPort_DmaHBlankTransfers();
        sPerfDisplay.frames = st.frames;
        sPerfDisplay.published = st.published;
        sPerfDisplay.timed = st.timed;
        sPerfDisplay.late = st.late;
        sPerfDisplay.dropped = st.dropped;
        sPerfDisplay.stereo = st.stereo;
        sPerfDisplay.smooth = st.smooth;
        sPerfDisplay.cpuTicks = st.cpuTicks;
        sPerfDisplay.gpuTicks = st.gpuTicks;
        sPerfDisplay.waitTicks = st.waitTicks;
        sPerfDisplay.publishTicks = st.publishTicks;
        sPerfDisplay.bgFullUploads = st.bgFullUploads;
        sPerfDisplay.bgDeltaUpdates = st.bgDeltaUpdates;
        sPerfDisplay.bgDirtyTiles = st.bgDirtyTiles;
        sPerfDisplay.bgFlushedBytes = st.bgFlushedBytes;
        sPerfDisplay.buildTicks = build;
        sPerfDisplay.sceneWaitTicks = wait;
        sPerfDisplay.scenePublishTicks = publish;
        sPerfDisplay.texConverted = converted;
        sPerfDisplay.texels = texels;
        sPerfDisplay.texTicks = texTicks;
        sPerfGeometry.busy = busy;
        sPerfGeometry.scenes = scenes;
        sPerfGeometry.skipped = skipped;
        sPerfGeometry.producerWait = producerWait;
        sPerfGeometry.wakeups = wakeups;
        sPerfGeometry.flowQueued = flowQueued;
        sPerfGeometry.flowDone = flowDone;
        sPerfGeometry.flowDropped = flowDropped;
        NitroPort_Render2DProfile(0, &sPerf2d[0]);
        NitroPort_Render2DProfile(1, &sPerf2d[1]);
        memcpy(sPerfPublish, st.publish, sizeof(sPerfPublish));
        for (i = 0; i < sDrawerCount; i++) {
            Drawer *d = &sDrawers[i];

            d->logTicks = __atomic_load_n(&d->ticks, __ATOMIC_RELAXED);
            d->logRenderTicks = __atomic_load_n(&d->renderTicks, __ATOMIC_RELAXED);
            d->logPublishTicks = __atomic_load_n(&d->publishTicks, __ATOMIC_RELAXED);
            d->logCount = __atomic_load_n(&d->count, __ATOMIC_RELAXED);
            d->logDrawn = __atomic_load_n(&d->drawn, __ATOMIC_RELAXED);
            d->logBskips = __atomic_load_n(&d->bSkips, __ATOMIC_RELAXED);
#ifndef HGSS_PROFILE_OFF
            d->logSignalLatency = __atomic_load_n(&d->signalLatency, __ATOMIC_RELAXED);
            d->logSignalCount = __atomic_load_n(&d->signalCount, __ATOMIC_RELAXED);
#endif
            for (int screen = 0; screen < 2; screen++) {
                d->logScreenRenderTicks[screen] = __atomic_load_n(&d->screenRenderTicks[screen], __ATOMIC_RELAXED);
                d->logScreenRenderCalls[screen] = __atomic_load_n(&d->screenRenderCalls[screen], __ATOMIC_RELAXED);
            }
        }
        NitroPort_Log("[perf] counters started (frame rates follow at the next log)");
        return;
    }

    seconds = (double)(now - sPerfLastTick) / (double)SYSCLOCK_ARM11;
    if (seconds < 0.001) {
        seconds = 0.001;   /* (a clock that did not move: no huge rates) */
    }

    for (i = 0; i < 2; i++) {
        const ComposePublishProfile *p = &st.publish[i], *last = &sPerfPublish[i];
        u32 calls = p->calls - last->calls;
        NitroPort_Log("[perf] publish-%c wall/call %.2f ms, check %.2f, lockwait %.2f, "
                      "convert+flush %.2f, bg+obj %.2f; calls %u reuse %u flat %u no-buffer %u invalid %u (wall, check includes wait)",
                      'A' + i, PerfMsPer(p->wallTicks - last->wallTicks, calls),
                      PerfMsPer(p->checkTicks - last->checkTicks, calls),
                      PerfMsPer(p->lockWaitTicks - last->lockWaitTicks, calls),
                      PerfMsPer(p->convertTicks - last->convertTicks, calls),
                      PerfMsPer(p->bgTicks - last->bgTicks, calls), (unsigned)calls,
                      (unsigned)(p->unchanged - last->unchanged), (unsigned)(p->flatProof - last->flatProof),
                      (unsigned)(p->noBuffer - last->noBuffer),
                      (unsigned)(p->invalid - last->invalid));
    }
    memcpy(sPerfPublish, st.publish, sizeof(sPerfPublish));

    {
        u32 frames = st.frames - sPerfDisplay.frames;
        u32 published = st.published - sPerfDisplay.published;
        u32 timed = st.timed - sPerfDisplay.timed;
        u32 late = st.late - sPerfDisplay.late;
        u32 dropped = st.dropped - sPerfDisplay.dropped;
        u32 stereo = st.stereo - sPerfDisplay.stereo;
        u32 smooth = st.smooth - sPerfDisplay.smooth;
        u64 cpu = st.cpuTicks - sPerfDisplay.cpuTicks;
        u64 gpu = st.gpuTicks - sPerfDisplay.gpuTicks;
        u64 waited = st.waitTicks - sPerfDisplay.waitTicks;
        u64 pub = st.publishTicks - sPerfDisplay.publishTicks;
        u64 sceneBuild = build - sPerfDisplay.buildTicks;
        u64 sceneWait = wait - sPerfDisplay.sceneWaitTicks;
        u64 scenePublish = publish - sPerfDisplay.scenePublishTicks;
        u32 texConverted = converted - sPerfDisplay.texConverted;
        u64 tex = texels - sPerfDisplay.texels;
        u64 texTime = texTicks - sPerfDisplay.texTicks;

        NitroPort_Log("[perf] display: %.1f composes/s, %.2f ms build, %.2f ms GPU/waited "
                      "(%.2f ms waiting), %.1f publishes/s, %.2f ms publish, %u late, %u dropped, "
                      "lag %d, stereo %u, smooth %u; scene build %.2f ms, GPU %.2f ms, handover %.2f ms; "
                      "%u textures converted (%.0f texels, %.2f ms), command list %.0f%% full, %u draws cut",
                      PerfPerSecond(frames, seconds),
                      PerfMsPer(cpu, frames),
                      PerfMsPer(gpu, frames),
                      PerfMsPer(waited, frames),
                      PerfPerSecond(published, seconds),
                      PerfMsPer(pub, published),
                      (unsigned)late, (unsigned)dropped, st.lag,
                      (unsigned)stereo, (unsigned)smooth,
                      PerfMsPer(sceneBuild, timed), PerfMsPer(sceneWait, timed),
                      PerfMsPer(scenePublish, timed),
                      (unsigned)texConverted,
                      texConverted ? (double)tex / texConverted : 0.0,
                      PerfMsPer(texTime, texConverted),
                       (double)cmdPeak * 100.0, cmdCut);
        NitroPort_Log("[perf] bg-upload: full %u, delta %u, delta tiles %u, flushed KB %.1f",
                      (unsigned)(st.bgFullUploads - sPerfDisplay.bgFullUploads),
                      (unsigned)(st.bgDeltaUpdates - sPerfDisplay.bgDeltaUpdates),
                      (unsigned)(st.bgDirtyTiles - sPerfDisplay.bgDirtyTiles),
                      (double)(st.bgFlushedBytes - sPerfDisplay.bgFlushedBytes) / 1024.0);
        sPerfDisplay.frames = st.frames;
        sPerfDisplay.published = st.published;
        sPerfDisplay.timed = st.timed;
        sPerfDisplay.late = st.late;
        sPerfDisplay.dropped = st.dropped;
        sPerfDisplay.stereo = st.stereo;
        sPerfDisplay.smooth = st.smooth;
        sPerfDisplay.cpuTicks = st.cpuTicks;
        sPerfDisplay.gpuTicks = st.gpuTicks;
        sPerfDisplay.waitTicks = st.waitTicks;
        sPerfDisplay.publishTicks = st.publishTicks;
        sPerfDisplay.bgFullUploads = st.bgFullUploads;
        sPerfDisplay.bgDeltaUpdates = st.bgDeltaUpdates;
        sPerfDisplay.bgDirtyTiles = st.bgDirtyTiles;
        sPerfDisplay.bgFlushedBytes = st.bgFlushedBytes;
        sPerfDisplay.buildTicks = build;
        sPerfDisplay.sceneWaitTicks = wait;
        sPerfDisplay.scenePublishTicks = publish;
        sPerfDisplay.texConverted = converted;
        sPerfDisplay.texels = texels;
        sPerfDisplay.texTicks = texTicks;
    }

    {
        u32 made = scenes - sPerfGeometry.scenes;
        u32 left = skipped - sPerfGeometry.skipped;
        u64 geoBusy = busy - sPerfGeometry.busy;
        u32 geoWakeups = wakeups - sPerfGeometry.wakeups;
        double producerMs = (double)(producerWait - sPerfGeometry.producerWait) / 1000.0;
        u32 fQueued = flowQueued - sPerfGeometry.flowQueued;
        u32 fDone = flowDone - sPerfGeometry.flowDone;
        u32 fDropped = flowDropped - sPerfGeometry.flowDropped;
#ifndef HGSS_PROFILE_OFF
        u32 didBuild = built - sPerfGeometry.built;
        u64 buildTicks = builtTicks - sPerfGeometry.builtTicks;
        u64 skipTicks = skippedTicks - sPerfGeometry.skippedTicks;
#endif

        NitroPort_Log("[perf] geometry: %.1f scenes/s, busy %.0f%% (%.2f ms/scene), %u skipped, "
                      "producer wait %.1f ms, %u wakeups (longest %.1f ms); "
                      "flow queued %u done %u dropped %u"
#ifndef HGSS_PROFILE_OFF
                      "; built %u (%.2f ms/built), %.2f ms/skipped"
#endif
                      ,
                      PerfPerSecond(made, seconds),
                      seconds > 0.0 ? 100.0 * (double)geoBusy / (double)SYSCLOCK_ARM11 / seconds : 0.0,
                      PerfMsPer(geoBusy, made), (unsigned)left, producerMs,
                      (unsigned)geoWakeups,
                      (double)longest * 1000.0 / (double)SYSCLOCK_ARM11,
                      (unsigned)fQueued, (unsigned)fDone, (unsigned)fDropped
#ifndef HGSS_PROFILE_OFF
                      , (unsigned)didBuild, PerfMsPer(buildTicks, didBuild),
                      PerfMsPer(skipTicks, left)
#endif
                      );
        sPerfGeometry.scenes = scenes;
        sPerfGeometry.skipped = skipped;
#ifndef HGSS_PROFILE_OFF
        sPerfGeometry.built = built;
        sPerfGeometry.builtTicks = builtTicks;
        sPerfGeometry.skippedTicks = skippedTicks;
#endif
        sPerfGeometry.busy = busy;
        sPerfGeometry.wakeups = wakeups;
        sPerfGeometry.producerWait = producerWait;
        sPerfGeometry.flowQueued = flowQueued;
        sPerfGeometry.flowDone = flowDone;
        sPerfGeometry.flowDropped = flowDropped;
    }

#if defined(HGSS_A0_DIAG) || !defined(HGSS_PROFILE_OFF)
    /* P1: idle share per core from the lowest-priority spin probes, against
     * their pre-worker calibration. Skipped when the probes never started. */
    {
        u64 span = now - sPerfIdleSpan;
        int core;

        if ((__atomic_load_n(&sA0IdleAlive[0], __ATOMIC_ACQUIRE) ||
             __atomic_load_n(&sA0IdleAlive[1], __ATOMIC_ACQUIRE)) &&
            sA0IdleCalTicks > 0 && span > 0) {
            double pct[2];

            for (core = 0; core < 2; core++) {
                u64 delta = __atomic_load_n(&sA0IdleCounter[core].count, __ATOMIC_RELAXED) -
                            sPerfIdleCount[core];
                double expected = (double)sA0IdleCal[core] * (double)span /
                                  (double)sA0IdleCalTicks;
                pct[core] = expected > 0.0 ? 100.0 * (double)delta / expected : 0.0;
                if (pct[core] < 0.0) pct[core] = 0.0;
                if (pct[core] > 100.0) pct[core] = 100.0;
            }
            NitroPort_Log("[perf] idle: core0 %.1f%% core1 %.1f%%", pct[0], pct[1]);
        }
        for (core = 0; core < 2; core++)
            sPerfIdleCount[core] = __atomic_load_n(&sA0IdleCounter[core].count, __ATOMIC_RELAXED);
        sPerfIdleSpan = now;
    }
#endif
#ifndef HGSS_PROFILE_OFF
    /* P1: game-thread accounting (WaitForFrameCap accumulators). */
    {
        u64 work = sGameWorkTicks - sPerfGameWork;
        u64 slept = sGameSleepTicks - sPerfGameSleep;
        u32 late = sGameLateFrames - sPerfGameLate;

        NitroPort_Log("[perf] game: work %.2f ms, slept %.2f ms, late %u",
                      PerfMsPer(work, 1), PerfMsPer(slept, 1), (unsigned)late);
        sPerfGameWork = sGameWorkTicks;
        sPerfGameSleep = sGameSleepTicks;
        sPerfGameLate = sGameLateFrames;
    }
#endif

    {
        u64 mix = soundTicks - sPerfSoundTicks;
        u64 elapsed = now - sPerfLastTick;
        u32 droppedNow = (u32)ndspGetDroppedFrames();
        u32 dropped = droppedNow - sPerfAudioDropped;
        u32 frames = (u32)ndspGetFrameCount() - sPerfAudioFrames;

        NitroPort_Log("[perf] audio: %.1f%% of core %d (%.1f ms mixing in %.1f s), %u dropped of %u",
                      elapsed ? 100.0 * (double)mix / (double)elapsed : 0.0,
                      sAudioCore, (double)mix * 1000.0 / (double)SYSCLOCK_ARM11, seconds,
                      (unsigned)dropped, (unsigned)frames);
        sPerfSoundTicks = soundTicks;
        sPerfAudioDropped = droppedNow;
        sPerfAudioFrames = (u32)ndspGetFrameCount();
    }

    {
        /* Slice S4: which H-blank client owns the walk's handler time. Like
         * hb_keep/dma/irq, the per-bit table holds the LAST walk (it resets
         * each walk), not a per-interval delta — so these are last-walk ms
         * and call counts, not rates. The table is attribution, not a
         * partition: nested raises count under both bits, DMA-part raises
         * live in hb_dma_us, and bits >= 8 plus the walk-end switch are
         * omitted — so its sum can exceed or undershoot hb_irq_us. Bit 1 is
         * the game's H-blank callback. */
        NitroPortIrqTime bits[8];
        u64 syncCalls, syncTicks, pumpTicks;
        u32 pumpCalls, dmaXfers;
        double ms = 1000.0 / (double)SYSCLOCK_ARM11;

        NitroPort_HBlankIrqByBit(bits);
        NitroPort_SyncTimersStats(&syncCalls, &syncTicks);
        NitroPort_SoundPumpTickStats(&pumpTicks, &pumpCalls);
        dmaXfers = NitroPort_DmaHBlankTransfers();
        NitroPort_Log("[perf] hbirq: b0 %.2fms/%u b1 %.2fms/%u b2 %.2fms/%u b3 %.2fms/%u "
                      "b4 %.2fms/%u b5 %.2fms/%u b6 %.2fms/%u b7 %.2fms/%u; "
                      "sync %llu/%.2fms; pump %.2fms/%u; dma %u xfers",
                      (double)bits[0].ticks * ms, (unsigned)bits[0].calls,
                      (double)bits[1].ticks * ms, (unsigned)bits[1].calls,
                      (double)bits[2].ticks * ms, (unsigned)bits[2].calls,
                      (double)bits[3].ticks * ms, (unsigned)bits[3].calls,
                      (double)bits[4].ticks * ms, (unsigned)bits[4].calls,
                      (double)bits[5].ticks * ms, (unsigned)bits[5].calls,
                      (double)bits[6].ticks * ms, (unsigned)bits[6].calls,
                      (double)bits[7].ticks * ms, (unsigned)bits[7].calls,
                      (unsigned long long)(syncCalls - sPerfSyncCalls),
                      (double)(syncTicks - sPerfSyncTicks) * 1000.0 / (double)SYSCLOCK_ARM11,
                      (double)(pumpTicks - sPerfPumpTicks) * 1000.0 / (double)SYSCLOCK_ARM11,
                      (unsigned)(pumpCalls - sPerfPumpCalls),
                      (unsigned)(dmaXfers - sPerfDmaXfers));
        sPerfSyncCalls = syncCalls;
        sPerfSyncTicks = syncTicks;
        sPerfPumpTicks = pumpTicks;
        sPerfPumpCalls = pumpCalls;
        sPerfDmaXfers = dmaXfers;
    }

#ifndef HGSS_PROFILE_OFF
    /* P1: DrawFrame offers dropped because every drawer was busy. */
    {
        unsigned dropped = sDrawDropped - sPerfDrawDropped;

        NitroPort_Log("[perf] offers: %u dropped (drawer busy)", dropped);
        sPerfDrawDropped = sDrawDropped;
    }
#endif

    for (i = 0; i < sDrawerCount; i++) {
        Drawer *d = &sDrawers[i];
        u64 ticks = __atomic_load_n(&d->ticks, __ATOMIC_RELAXED) - d->logTicks;
        u64 render = __atomic_load_n(&d->renderTicks, __ATOMIC_RELAXED) - d->logRenderTicks;
        u64 publish = __atomic_load_n(&d->publishTicks, __ATOMIC_RELAXED) - d->logPublishTicks;
        unsigned count = __atomic_load_n(&d->count, __ATOMIC_RELAXED) - d->logCount;
        unsigned drawn = __atomic_load_n(&d->drawn, __ATOMIC_RELAXED) - d->logDrawn;
        unsigned bskips = __atomic_load_n(&d->bSkips, __ATOMIC_RELAXED) - d->logBskips;
#ifndef HGSS_PROFILE_OFF
        u64 sigLat = __atomic_load_n(&d->signalLatency, __ATOMIC_RELAXED) - d->logSignalLatency;
        unsigned sigCount = __atomic_load_n(&d->signalCount, __ATOMIC_RELAXED) - d->logSignalCount;
#endif

        NitroPort_Log("[perf] drawer %d (core %d, %s): %.1f frames/s, %.2f ms/frame, "
                      "%.2f ms render, %.2f ms publish, %u drawn, %u lower deferred"
#ifndef HGSS_PROFILE_OFF
                      ", %.2f ms signal"
#endif
                      ,
                      i, d->core,
                      d->screens == (ENGINE_A | ENGINE_B) ? "both screens"
                      : (d->screens == ENGINE_A) == sAOnTop ? "upper" : "lower",
                      PerfPerSecond(count, seconds), PerfMsPer(ticks, count),
                      PerfMsPer(render, count), PerfMsPer(publish, count), drawn, bskips
#ifndef HGSS_PROFILE_OFF
                      , PerfMsPer(sigLat, sigCount)
#endif
                      );
        for (int screen = 0; screen < 2; screen++) {
            u64 st = __atomic_load_n(&d->screenRenderTicks[screen], __ATOMIC_RELAXED);
            unsigned sc = __atomic_load_n(&d->screenRenderCalls[screen], __ATOMIC_RELAXED);
            NitroPort_Log("[perf] drawer %d %s render: %u calls, %.2f ms/call wall (includes preemption)",
                          i, screen ? "bottom" : "top", sc - d->logScreenRenderCalls[screen],
                          PerfMsPer(st - d->logScreenRenderTicks[screen], sc - d->logScreenRenderCalls[screen]));
            d->logScreenRenderTicks[screen] = st;
            d->logScreenRenderCalls[screen] = sc;
        }
        d->logTicks += ticks;
        d->logRenderTicks += render;
        d->logPublishTicks += publish;
        d->logCount += count;
        d->logDrawn += drawn;
        d->logBskips += bskips;
#ifndef HGSS_PROFILE_OFF
        d->logSignalLatency += sigLat;
        d->logSignalCount += sigCount;
#endif
    }
    {
        /* Slice A: where the drawer render time goes (both engines combined;
         * ms/s per phase, i.e. percent of a core, plus line counts). Slice B
         * adds the per-frame setup and the text-row cache hit rate. */
        NitroRender2DProfile cur[2], *a = &cur[0], *b = &cur[1];
        u64 text, layer, obj, merge, blend, window, finish, setup, setupPal, setupKey;
        u64 el = now - sPerfLastTick;
        double inv;
        unsigned rows, rowsHit;

        NitroPort_Render2DProfile(0, a);
        NitroPort_Render2DProfile(1, b);
        text = (a->textTicks - sPerf2d[0].textTicks) + (b->textTicks - sPerf2d[1].textTicks);
        layer = (a->layerTicks - sPerf2d[0].layerTicks) + (b->layerTicks - sPerf2d[1].layerTicks);
        obj = (a->objTicks - sPerf2d[0].objTicks) + (b->objTicks - sPerf2d[1].objTicks);
        merge = (a->mergeTicks - sPerf2d[0].mergeTicks) + (b->mergeTicks - sPerf2d[1].mergeTicks);
        blend = (a->blendTicks - sPerf2d[0].blendTicks) + (b->blendTicks - sPerf2d[1].blendTicks);
        window = (a->windowTicks - sPerf2d[0].windowTicks) + (b->windowTicks - sPerf2d[1].windowTicks);
        finish = (a->finishTicks - sPerf2d[0].finishTicks) + (b->finishTicks - sPerf2d[1].finishTicks);
        setup = (a->setupTicks - sPerf2d[0].setupTicks) + (b->setupTicks - sPerf2d[1].setupTicks);
        setupPal = (a->setupPalTicks - sPerf2d[0].setupPalTicks) + (b->setupPalTicks - sPerf2d[1].setupPalTicks);
        setupKey = (a->setupKeyTicks - sPerf2d[0].setupKeyTicks) + (b->setupKeyTicks - sPerf2d[1].setupKeyTicks);
        rowsHit = (a->rowHits - sPerf2d[0].rowHits) + (b->rowHits - sPerf2d[1].rowHits);
        rows = rowsHit + (a->rowMisses - sPerf2d[0].rowMisses) + (b->rowMisses - sPerf2d[1].rowMisses);
        inv = el ? 100.0 / (double)el : 0.0;
#ifndef HGSS_PROFILE_OFF
        /* P1: finer setup split + take/commit + flat fills + key byte counts. */
        {
            u64 transp = (a->setupTransparentTicks - sPerf2d[0].setupTransparentTicks) +
                         (b->setupTransparentTicks - sPerf2d[1].setupTransparentTicks);
            u64 decide = (a->setupDecideTicks - sPerf2d[0].setupDecideTicks) +
                         (b->setupDecideTicks - sPerf2d[1].setupDecideTicks);
            u64 take = (a->takeTicks - sPerf2d[0].takeTicks) + (b->takeTicks - sPerf2d[1].takeTicks);
            u64 flatfill = (a->flatFillTicks - sPerf2d[0].flatFillTicks) +
                           (b->flatFillTicks - sPerf2d[1].flatFillTicks);
            u64 keyMap = (a->keyMapBytes - sPerf2d[0].keyMapBytes) + (b->keyMapBytes - sPerf2d[1].keyMapBytes);
            u64 keyTile = (a->keyTileBytes - sPerf2d[0].keyTileBytes) + (b->keyTileBytes - sPerf2d[1].keyTileBytes);
            u64 check = (a->setupCheckTicks - sPerf2d[0].setupCheckTicks) +
                        (b->setupCheckTicks - sPerf2d[1].setupCheckTicks);
            NitroPort_Log("[perf] 2d: text %.1f%%, affine %.1f%%, obj %.1f%%, merge %.1f%%, blend %.1f%%, "
                          "window %.1f%%, finish %.1f%%, setup %.1f%% (pal %.1f%%, key %.1f%%, %u lines, %u empty, %u blend, rows %u/%u), "
                          "decide %.1f%%, transp %.1f%%, check %.1f%%, take %.2f ms, flatfill %.2f ms, key %llu/%llu B",
#else
        NitroPort_Log("[perf] 2d: text %.1f%%, affine %.1f%%, obj %.1f%%, merge %.1f%%, blend %.1f%%, "
                      "window %.1f%%, finish %.1f%%, setup %.1f%% (pal %.1f%%, key %.1f%%, %u lines, %u empty, %u blend, rows %u/%u)",
#endif
                          (double)text * inv, (double)layer * inv, (double)obj * inv,
                          (double)merge * inv, (double)blend * inv, (double)window * inv,
                          (double)finish * inv, (double)setup * inv,
                          (double)setupPal * inv, (double)setupKey * inv,
                          (unsigned)((a->lines - sPerf2d[0].lines) + (b->lines - sPerf2d[1].lines)),
                          (unsigned)((a->emptyLines - sPerf2d[0].emptyLines) +
                                     (b->emptyLines - sPerf2d[1].emptyLines)),
                          (unsigned)((a->blendLines - sPerf2d[0].blendLines) +
                                     (b->blendLines - sPerf2d[1].blendLines)),
                          rowsHit, rows
#ifndef HGSS_PROFILE_OFF
                          , (double)decide * inv, (double)transp * inv,
                          (double)check * inv,
                          PerfMsPer(take, 1), PerfMsPer(flatfill, 1),
                          (unsigned long long)keyMap, (unsigned long long)keyTile
#endif
                          );
#ifndef HGSS_PROFILE_OFF
        } /* (end P1 block) */
#endif
        /* Slice S5/S6 bundle: why layers stay on software (engine A only).
         * Cumulative counters, differenced per interval like rows; read
         * before the snapshot below. */
        NitroPort_Log("[perf] gpubg: taken %u, reject setup/hblank/off/mode/mosaic/ext/blend/behind/between/obj/obl/size/nowr/str/dsp/win/bg0 %u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u, obj %u/%u/%u/%u/%u/%u/%u, region %u/%u",
                      (unsigned)(a->gpuBgTaken - sPerf2d[0].gpuBgTaken + a->gpuObjTaken - sPerf2d[0].gpuObjTaken),
                      (unsigned)(a->gpuBgReject[0] - sPerf2d[0].gpuBgReject[0]),
                      (unsigned)(a->gpuBgReject[1] - sPerf2d[0].gpuBgReject[1]),
                      (unsigned)(a->gpuBgReject[2] - sPerf2d[0].gpuBgReject[2]),
                      (unsigned)(a->gpuBgReject[3] - sPerf2d[0].gpuBgReject[3]),
                      (unsigned)(a->gpuBgReject[4] - sPerf2d[0].gpuBgReject[4]),
                      (unsigned)(a->gpuBgReject[5] - sPerf2d[0].gpuBgReject[5]),
                      (unsigned)(a->gpuBgReject[6] - sPerf2d[0].gpuBgReject[6]),
                      (unsigned)(a->gpuBgReject[7] - sPerf2d[0].gpuBgReject[7]),
                      (unsigned)(a->gpuBgReject[8] - sPerf2d[0].gpuBgReject[8]),
                      (unsigned)(a->gpuBgReject[9] - sPerf2d[0].gpuBgReject[9]),
                      (unsigned)(a->gpuBgReject[10] - sPerf2d[0].gpuBgReject[10]),
                      (unsigned)(a->gpuBgReject[11] - sPerf2d[0].gpuBgReject[11]),
                      (unsigned)(a->gpuBgReject[12] - sPerf2d[0].gpuBgReject[12]),
                      (unsigned)(a->gpuBgReject[13] - sPerf2d[0].gpuBgReject[13]),
                      (unsigned)(a->gpuBgReject[14] - sPerf2d[0].gpuBgReject[14]),
                      (unsigned)(a->gpuBgReject[15] - sPerf2d[0].gpuBgReject[15]),
                      (unsigned)(a->gpuBgReject[16] - sPerf2d[0].gpuBgReject[16]),
                      (unsigned)(a->gpuObjReject[0] - sPerf2d[0].gpuObjReject[0]),
                      (unsigned)(a->gpuObjReject[1] - sPerf2d[0].gpuObjReject[1]),
                      (unsigned)(a->gpuObjReject[2] - sPerf2d[0].gpuObjReject[2]),
                      (unsigned)(a->gpuObjReject[3] - sPerf2d[0].gpuObjReject[3]),
                      (unsigned)(a->gpuObjReject[4] - sPerf2d[0].gpuObjReject[4]),
                      (unsigned)(a->gpuObjReject[5] - sPerf2d[0].gpuObjReject[5]),
                      (unsigned)(a->gpuObjReject[6] - sPerf2d[0].gpuObjReject[6]),
                      (unsigned)(a->gpuRegionClean[0] - sPerf2d[0].gpuRegionClean[0]),
                      (unsigned)(a->gpuRegionClean[1] - sPerf2d[0].gpuRegionClean[1]));
        sPerf2d[0] = *a;
        sPerf2d[1] = *b;
    }

    {
        /* What the 10 MB game heap and the linear heap actually hold, so a
         * run can say whether the split is still right. The game heap's
         * high-water is the number that matters: shrink it and it OOMs. */
        struct mallinfo mi = mallinfo();
        unsigned long freeKB = (unsigned long)(linearSpaceFree() / 1024u);
        static unsigned long sLinearLowKB;

        NitroPort_Log("[perf] mem: linear_free=%lu KB game_heap_used=%lu KB",
                      freeKB,
                      (unsigned long)(mi.uordblks / 1024u));
        /* Half-A A-M1: name when linear drops (correlate the tick with scene
         * transitions vs texture-cache growth in the [gx3d] line). */
        if (sLinearLowKB == 0 || freeKB < sLinearLowKB) {
            if (sLinearLowKB != 0 && sLinearLowKB - freeKB >= 256) {
                NitroPort_Log("[perf] mem: linear low-water %lu KB (was %lu KB)",
                              freeKB, sLinearLowKB);
            }
            sLinearLowKB = freeKB;
        }
        /* Half-A L3: referenced-range bytes vs the full 304 KiB sweep. */
        if (sIniRefHashB || sIniRefHashDiag) {
            uint64_t refB, fullB;
            unsigned disag;

            NitroPort_Render2DRefHashStats(&refB, &fullB, &disag);
            NitroPort_Log("[perf] refHashB: %llu referenced bytes, %llu full-hash bytes, %u disagreements",
                          (unsigned long long)refB, (unsigned long long)fullB, disag);
        }
        /* P3: the flat picture's passes. skipped: the picture was the same
         * and the pass was skipped; redrawn: it changed, or the frame was
         * not modelled by the referenced-range signature (not-modelled
         * counts those); disagreements: the diagnostic's; why: which input
         * differed when a pass did not skip. */
        {
            NitroFlatStats fs;

            NitroPort_Render2DFlatStats(&fs);
            NitroPort_Log("[perf] flatskip: %u skipped, %u redrawn, %u not-modelled, %u disagreements, why small %u vram %u scroll %u buffers %u",
                          fs.skipped - sPerfFlat.skipped, fs.redrawn - sPerfFlat.redrawn,
                          fs.notModelled - sPerfFlat.notModelled,
                          fs.disagreements - sPerfFlat.disagreements,
                          fs.whySmall - sPerfFlat.whySmall, fs.whyVram - sPerfFlat.whyVram,
                          fs.whyScroll - sPerfFlat.whyScroll,
                          fs.whyBuffers - sPerfFlat.whyBuffers);
            sPerfFlat = fs;
        }
        /* Half-A L4/L5: band takes vs whole renders; uploaded bytes. */
        if (sIniPartialB || sIniPartialBDiag) {
            uint64_t frames, objTakes, bgTakes, unchZero, fallbacks, partLines;
            uint64_t tryUnmodelled, tryUnderMismatch, refreshAdopts, refreshInvalidates;
            uint64_t epilogueInvalidates, epilogueEntries, tryPostRender, rangeEntryB;
            unsigned diagMismatch;

            NitroPort_Render2DPartialBStats(&frames, &objTakes, &bgTakes, &unchZero,
                                            &fallbacks, &partLines, &diagMismatch);
            NitroPort_Render2DPartialBDebug(&tryUnmodelled, &tryUnderMismatch,
                                            &refreshAdopts, &refreshInvalidates,
                                            &epilogueInvalidates, &epilogueEntries,
                                            &tryPostRender, &rangeEntryB);
            NitroPort_Log("[perf] partialB-dbg: %llu try-unmodelled, %llu try-under-mismatch, %llu refresh-adopts, %llu refresh-invalidates, %llu epilogue-invalidates, %llu epilogue-entries, %llu try-post-render, %llu range-entries",
                          (unsigned long long)tryUnmodelled, (unsigned long long)tryUnderMismatch,
                          (unsigned long long)refreshAdopts, (unsigned long long)refreshInvalidates,
                          (unsigned long long)epilogueInvalidates, (unsigned long long)epilogueEntries,
                          (unsigned long long)tryPostRender, (unsigned long long)rangeEntryB);
            NitroPort_Log("[perf] partialB: %llu frames (%llu obj-band, %llu bg-band, %llu unchanged, %llu whole), %llu band lines, %u mismatches",
                          (unsigned long long)frames, (unsigned long long)objTakes,
                          (unsigned long long)bgTakes, (unsigned long long)unchZero,
                          (unsigned long long)fallbacks, (unsigned long long)partLines,
                          diagMismatch);
            if (sIniPartialBChar) {
                uint64_t charTakes;
                NitroPort_Render2DPartialBCharStats(&charTakes);
                NitroPort_Log("[perf] partialB-char: %llu text-character bands",
                              (unsigned long long)charTakes);
            }
        }
        if (sIniBandPubB || sIniBandPubBDiag) {
            uint64_t convB, flushB;
            uint32_t hinted, uploaded, viol;

            NitroPort_ComposeBandPublishStats(&convB, &flushB, &hinted, &uploaded, &viol);
            NitroPort_Log("[perf] bandPubB: %llu converted, %llu flushed bytes, %u hinted, %u uploaded bands, %u subset violations",
                          (unsigned long long)convB, (unsigned long long)flushB,
                          hinted, uploaded, viol);
        }
#ifdef HGSS_A0_DIAG
        {
            NitroRedrawStats r0, r1;
            NitroPartialBReasons p;
            unsigned mustTop, mustBottom;

            NitroPort_Render2DRedrawStats(0, &r0);
            NitroPort_Render2DRedrawStats(1, &r1);
            NitroPort_Render2DPartialBReasons(&p);
            mustTop = __atomic_load_n(&sA0MustDraw[0], __ATOMIC_RELAXED);
            mustBottom = __atomic_load_n(&sA0MustDraw[1], __ATOMIC_RELAXED);
            NitroPort_Log("[a0] redraw A: flat %llu reuseHit %llu reuseScroll %llu forced %llu firstNote %llu layout %llu regs %llu pal %llu oam %llu misc %llu vram %llu unchanged %llu",
                          (unsigned long long)r0.flat, (unsigned long long)r0.reuseHit,
                          (unsigned long long)r0.reuseScroll, (unsigned long long)r0.forced,
                          (unsigned long long)r0.firstNote, (unsigned long long)r0.layoutChanged,
                          (unsigned long long)r0.regsChanged, (unsigned long long)r0.paletteChanged,
                          (unsigned long long)r0.oamChanged, (unsigned long long)r0.miscChanged,
                          (unsigned long long)r0.vramChanged, (unsigned long long)r0.unchanged);
            NitroPort_Log("[a0] redraw B: flat %llu reuseHit %llu reuseScroll %llu forced %llu firstNote %llu layout %llu regs %llu pal %llu oam %llu misc %llu vram %llu unchanged %llu mustDraw %u",
                          (unsigned long long)r1.flat, (unsigned long long)r1.reuseHit,
                          (unsigned long long)r1.reuseScroll, (unsigned long long)r1.forced,
                          (unsigned long long)r1.firstNote, (unsigned long long)r1.layoutChanged,
                          (unsigned long long)r1.regsChanged, (unsigned long long)r1.paletteChanged,
                          (unsigned long long)r1.oamChanged, (unsigned long long)r1.miscChanged,
                          (unsigned long long)r1.vramChanged, (unsigned long long)r1.unchanged,
                          mustBottom);
            NitroPort_Log("[a0] redraw bytes/scanlines A: small %llu vram %llu scan %llu; B: small %llu vram %llu scan %llu; mustDraw top %u",
                          (unsigned long long)r0.smallBytes, (unsigned long long)r0.vramBytes,
                          (unsigned long long)r0.scanlines, (unsigned long long)r1.smallBytes,
                          (unsigned long long)r1.vramBytes, (unsigned long long)r1.scanlines,
                          mustTop);
            {
                /* P3: which register word changed between the last flat
                 * render and a pass that did not skip. */
                uint32_t mask, first8, every;
                unsigned passes, vramCnt, powcnt, b1or, b1b, b1a;

                NitroPort_Render2DFlatRegDiff(&mask, &first8, &passes, &vramCnt, &powcnt);
                NitroPort_Render2DFlatRegEvery(&every, &b1or, &b1b, &b1a);
                NitroPort_Log("[a0] flat regdiff: %u passes, word mask 0x%08x, bytes0-7 0x%02x, VRAMCNT %u, POWCNT1 %u, every 0x%08x, dispcnt1 0x%02x (was 0x%02x)",
                              passes, mask, first8, vramCnt, powcnt, every, b1or, b1b);
            }
            NitroPort_Log("[a0] flat vram race: %llu draws hashed before and after, %llu saw the game write VRAM during the draw",
                          (unsigned long long)r0.flatRaceDraws,
                          (unsigned long long)r0.flatRaceChanges);
            {
                /* P6: the H-blank walk's fused compare against the old scan
                 * over all the lines: the two answers must agree. */
                uint64_t walks, mismatch;

                NitroPort_HBlankScanCheck(&walks, &mismatch);
                NitroPort_Log("[a0] hblank scan check: %llu walks, %llu disagreements",
                              (unsigned long long)walks, (unsigned long long)mismatch);
            }
            NitroPort_Log("[a0] partialB whole: shared %llu rest %llu extpal %llu obj-wide %llu bg-kind %llu bg-regs %llu bg-lines %llu bg-char %llu bg-no-map %llu bg-no-band %llu bg-wide %llu mixed %llu",
                          (unsigned long long)p.shared, (unsigned long long)p.rest, (unsigned long long)p.extPal,
                          (unsigned long long)p.objWide, (unsigned long long)p.bgKind,
                          (unsigned long long)p.bgRegs, (unsigned long long)p.bgLines,
                          (unsigned long long)p.bgChar, (unsigned long long)p.bgNoMap,
                          (unsigned long long)p.bgNoBand, (unsigned long long)p.bgWide,
                          (unsigned long long)p.mixed);
            NitroPort_Log("[a0] partialB rest terms: layout %llu regs %llu vramcnt %llu power %llu palette %llu lines %llu other %llu (terms can overlap)",
                          (unsigned long long)p.restLayout, (unsigned long long)p.restRegs,
                          (unsigned long long)p.restVramCnt, (unsigned long long)p.restPower,
                          (unsigned long long)p.restPalette, (unsigned long long)p.restLines,
                          (unsigned long long)p.restOther);
            NitroPort_Log("[a0] partialB bg-char layers: %llu/%llu/%llu/%llu",
                          (unsigned long long)p.bgCharLayer[0], (unsigned long long)p.bgCharLayer[1],
                          (unsigned long long)p.bgCharLayer[2], (unsigned long long)p.bgCharLayer[3]);
            for (int i = 0; i < 28; i++) {
                if (p.regWords[i] || p.lineWords[i])
                    NitroPort_Log("[a0] partialB reg offset %02x: regs %llu lines %llu changed %08lx last %08lx->%08lx",
                                  i * 4, (unsigned long long)p.regWords[i],
                                  (unsigned long long)p.lineWords[i],
                                  (unsigned long)p.regChangedBits[i], (unsigned long)p.regBefore[i],
                                  (unsigned long)p.regAfter[i]);
            }
        }
#endif
    }
#ifdef HGSS_A0_DIAG
    if (sA0IdleCalTicks && now > sPerfLastTick) {
        double idle[2] = { -1.0, -1.0 };
        for (i = 0; i < 2; i++) {
            u64 count = __atomic_load_n(&sA0IdleCounter[i].count, __ATOMIC_RELAXED);
            u64 delta = count - sA0IdleLast[i];
            sA0IdleLast[i] = count;
            if (sA0IdleCal[i])
                idle[i] = 100.0 * (double)delta * (double)sA0IdleCalTicks /
                          ((double)sA0IdleCal[i] * (double)(now - sPerfLastTick));
        }
        NitroPort_Log("[a0] idle probes: core0 %.1f%% core1 %.1f%% of calibrated idle throughput (-1 unavailable; includes system-share effects)",
                      idle[0], idle[1]);
    }
#endif
    sPerfLastTick = now;
}

/* Hold each frame start to a wall-clock grid of SYSCLOCK_ARM11 / fps ticks.
 *
 * The emulated DS's only clock is the V-blank the port raises once per
 * OnFrame, so a frame that finishes before the display refresh must still
 * wait. gfxSwapBuffers used to provide that wait, but it only blocks while
 * the previous frame is still being presented: once the composite got under
 * one refresh it returned early and the game ran at 87 Hz (log-9). This is a
 * deadline, not a second V-blank: a frame that already overran returns
 * without sleeping, and one more than a whole period behind starts a fresh
 * grid instead of bursting to catch up. */
static void WaitForFrameCap(u64 frameStart)
{
    u64 period = SYSCLOCK_ARM11 / (u64)sFrameCap;
    u64 now;

    if (sNextFrameTick == 0) {
        sNextFrameTick = frameStart + period;
    }
    now = svcGetSystemTick();
#ifndef HGSS_PROFILE_OFF
    /* P1: game work ends where the cap wait begins (frameStart is the
     * OnFrame entry tick). */
    sGameWorkTicks += now - frameStart;
#endif
    if (now < sNextFrameTick) {
        /* svcSleepThread takes nanoseconds; the grid is in ARM11 ticks. */
        u64 ns = (sNextFrameTick - now) * 1000000000ULL / SYSCLOCK_ARM11;
#ifndef HGSS_PROFILE_OFF
        u64 slept = svcGetSystemTick();
        svcSleepThread((s64)ns);
        sGameSleepTicks += svcGetSystemTick() - slept;
#else
        svcSleepThread((s64)ns);
#endif
    } else {
#ifndef HGSS_PROFILE_OFF
        /* The frame arrived already late (no sleep); a grid reset below
         * means it was more than a whole period behind. */
        sGameLateFrames++;
#endif
        if (now - sNextFrameTick > period) {
            sNextFrameTick = now + period;
            return;
        }
    }
    sNextFrameTick += period;
}

void NitroPort_OnFrame(void)
{
    u64 frameStart = svcGetSystemTick();
    u64 drawStart;
    unsigned long long totalUs;
    unsigned long long stateUs;
    uint32_t held;
    circlePosition circle;
    uint16_t buttons = 0;
    int touchX = 0, touchY = 0;
    int touchDown = 0;
    bool filterCombo;
    touchPosition touch;

    /* Before any early return: a paused or restored frame still counts, and
     * the pair printed at the log point then describes one interval. */
    totalUs = sHaveLastFrameTick ? TicksToUs(frameStart - sLastFrameTick) : 0;
    sLastFrameTick = frameStart;
    sHaveLastFrameTick = true;

    sFrame++;
    if (!aptMainLoop()) {
        /* The only thread that may ask APT: the close is handled here, once
         * per frame, and this thread leaves so the main thread's join
         * succeeds and teardown happens in order. */
        NitroPort_Log("the HOME menu asked to close (frame %lu)", sFrame);
        NitroPort_CardFlush();
        sQuit = true;
        threadExit(0);
    }
    if (__atomic_exchange_n(&sResumePending, false, __ATOMIC_ACQ_REL)) {
        /* All APT hooks have run (citro3d's restore is after ours): it is
         * safe for the display thread to touch PICA/GSP again. */
        sPaused = false;
        sMustDraw[0] = true;
        sMustDraw[1] = true;
        __atomic_store_n(&sComposeForce, true, __ATOMIC_RELEASE);
        NitroPort_ComposeHold(0);
        __atomic_store_n(&sSuspended, false, __ATOMIC_SEQ_CST);
        if (__atomic_load_n(&sGpuWakeReady, __ATOMIC_ACQUIRE)) {
            LightEvent_Signal(&sGpuWake);
        }
        NitroPort_Log("[apt] resumed after hooks (frame %lu)", sFrame);
    }
    if (sPaused) {
        svcSleepThread(16 * 1000000LL);
        return;
    }

    /* The geometry thread has everything the game wrote this frame; a
     * SWAP_BUFFERS in it is the scene the GPU thread will draw. Both shells
     * flush at the same point. */
    NitroPort_Gx3dFlush();

    /* The ARM7's alarm replies (sound ticks and the sequencer) are delivered
     * on this thread, as the PC shell does. Timed apart from game_us so the
     * audio cost doesn't hide in the game frame (Slice S4). */
    {
        u64 pumpStart = svcGetSystemTick();

        NitroPort_SoundPumpReplies();
        NitroPort_NoteSoundPumpTicks(svcGetSystemTick() - pumpStart);
    }

    hidScanInput();
    held = hidKeysHeld();
    /* START+SELECT held 2 s exits to the Homebrew Launcher. This is NOT the
     * HOME-menu close path (no APT close handling, no sQuit/WaitForGame
     * teardown): it flushes the save and the log, then ends the process
     * directly, and the loader returns to hbmenu. Wall-clock ticks, not
     * frames: the drawer can run well under 60 Hz on an Old 3DS. Physical
     * buttons only, so a scripted START+SELECT can never exit. The pair is
     * masked from the game below so the 2 s hold does not open menus on
     * the way out. */
    {
        bool exitCombo = (held & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT);

        if (exitCombo) {
            if (sExitHoldStart == 0) {
                sExitHoldStart = svcGetSystemTick();
            } else if (svcGetSystemTick() - sExitHoldStart >= EXIT_HOLD_TICKS) {
                ExitToLauncher();
            }
        } else {
            sExitHoldStart = 0;
        }
    }
    /* X+R cycles the pixel-filter presets. An edge, so one press is one
     * step; while held, X and R are masked from the game's buttons below.
     * An Old 3DS keeps its launch-time filter only: three of the five
     * presets are identical at 1 texel a pixel, and the filter3d presets
     * multiply texture memory 2-4x with under a megabyte free. */
    filterCombo = (held & (KEY_X | KEY_R)) == (KEY_X | KEY_R);
    if (!sNew3ds) {
        filterCombo = false;
    }
    if (filterCombo && !sFilterCombo) {
        const FilterPreset *p;

        sFilterPreset = (sFilterPreset + 1) % FILTER_PRESETS;
        p = &sFilterPresets[sFilterPreset];
        __atomic_store_n(&sToastLabel, p->name, __ATOMIC_RELEASE);
        __atomic_store_n(&sToastUntil, svcGetSystemTick() + TOAST_TICKS, __ATOMIC_RELEASE);
        __atomic_store_n(&sToastDirty, true, __ATOMIC_RELEASE);
        NitroPort_ComposeSetFilter(p->filter2d, p->filter3d);
    }
    sFilterCombo = filterCombo;
    {
        u64 until = __atomic_load_n(&sToastUntil, __ATOMIC_ACQUIRE);

        if (until != 0 && svcGetSystemTick() >= until) {
            __atomic_store_n(&sToastUntil, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&sToastDirty, true, __ATOMIC_RELEASE);
        }
    }
    hidCircleRead(&circle);
    buttons = DsButtons(held, circle);
    if (((held & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT))) {
        buttons &= (uint16_t)~(HGSS_BUTTON_START | HGSS_BUTTON_SELECT);
    }
    if (filterCombo) {
        buttons &= (uint16_t)~(HGSS_BUTTON_X | HGSS_BUTTON_R);
    }
    hidTouchRead(&touch);
    if (held & KEY_TOUCH) {
        /* The lower screen fills 320x240; the game's is 256x192. */
        touchDown = 1;
        touchX = touch.px * 4 / 5;
        touchY = touch.py * 4 / 5;
    }

    if (sScriptLoaded) {
        buttons |= HgssScript_HeldButtons(&sScript, (long)sFrame);
        if (HgssScript_TouchAt(&sScript, (long)sFrame, &touchX, &touchY)) {
            touchDown = 1;
        }
    }

    NitroPort_SetButtons(buttons, 0);
    NitroPort_SetTouch(touchDown, (uint16_t)touchX, (uint16_t)touchY);
    /* The upper screen's 3D view widens only while engine A is on top. */
    NitroPort_Gx3dSetWideView(NitroPort_EngineAOnTop() ? 320 : 256);
    drawStart = svcGetSystemTick();
    (void)DrawFrame();
    stateUs = TicksToUs(svcGetSystemTick() - drawStart);

    if (sFrame % 600 == 0) {
        /* One interval, one pair: total_us is this frame's wait (measured from
         * the previous entry, in time for the early returns to count too) and
         * state_us is the hand-off that happened inside it -- the frame
         * before. Measuring both at this point made frame 1800 report
         * total_us 10,810 < state_us 14,118, which is impossible and meant
         * neither number could be trusted. */
        unsigned long long prevStateUs = sPrevStateUs;
        unsigned long long gameUs = totalUs > prevStateUs ? totalUs - prevStateUs : 0;

        NitroPort_Log("frame %lu", sFrame);
        NitroPort_Log("FRAMETIME frame=%lu game_us=%llu state_us=%llu total_us=%llu "
                      "hblank_us=%llu handoff_us=%llu hb_keep_us=%llu hb_dma_us=%llu hb_irq_us=%llu"
#ifndef HGSS_PROFILE_OFF
                      " hb_ref_us=%llu hb_sw_us=%llu hb_cmp_us=%llu"
#endif
                      ,
                      sFrame, gameUs, prevStateUs, totalUs,
                      TicksToUs(sPrevHBlankTicks), TicksToUs(sPrevHandoffTicks),
                      TicksToUs(sPrevHbKeep), TicksToUs(sPrevHbDma), TicksToUs(sPrevHbIrq)
#ifndef HGSS_PROFILE_OFF
                      , TicksToUs(sPrevHbRefresh), TicksToUs(sPrevHbSwitch), TicksToUs(sPrevHbCmp)
#endif
                      );
        PerfLog(svcGetSystemTick());
        NitroPort_Log("3D images: %lu (scenes drawn: %lu)",
                      (unsigned long)NitroPort_Gx3dImageCount(), sGpuScenes);
        if (sScriptLoaded && sScript.end > 0 && (long)sFrame > sScript.end) {
            NitroPort_Log("script ended at frame %ld", sScript.end);
            sScriptLoaded = false;
        }
        /* Sprites duplicated on screen: are the duplicates in OAM (the game
         * wrote the same entry many times) or only in the picture (the port
         * drew one entry many times)? attr0 bit 9 disables a sprite only when
         * bit 8 (affine) is clear; for an affine sprite it is the double-size
         * flag, and HGSS draws its field sprites affine. The most-repeated
         * tile is the number to watch: one tile appearing twenty times is a
         * game-side duplicate in OAM. */
        {
            extern unsigned char NitroPort_Oam[0x800];
            unsigned live[2], dup[2], top[2], topTile[2];
            int e, i, j;

            for (e = 0; e < 2; e++) {
                const unsigned char *oam = NitroPort_Oam + e * 0x400;

                live[e] = 0;
                dup[e] = 0;
                top[e] = 0;
                topTile[e] = 0xFFFFu;
                for (i = 0; i < 128; i++) {
                    const unsigned char *a = oam + i * 8;
                    unsigned tile;
                    unsigned same = 0;

                    if ((a[1] & 0x03) == 0x02) {
                        continue;   /* (attr0: affine clear, disable set) */
                    }
                    live[e]++;
                    tile = (unsigned)a[4] | ((unsigned)a[5] << 8);
                    for (j = 0; j < 128; j++) {
                        if ((oam[j * 8 + 1] & 0x03) == 0x02) {
                            continue;
                        }
                        if ((((unsigned)oam[j * 8 + 4] | ((unsigned)oam[j * 8 + 5] << 8)) == tile)) {
                            same++;
                        }
                    }
                    if (same > top[e]) {
                        top[e] = same;
                        topTile[e] = tile;
                    }
                    for (j = 0; j < i; j++) {
                        if (memcmp(a, oam + j * 8, 8) == 0) {
                            dup[e]++;
                            break;
                        }
                    }
                }
            }
            NitroPort_Log("[perf] oam: top=%c; A %u shown, %u identical, most-repeated tile %u x%u; "
                          "B %u shown, %u identical, most-repeated tile %u x%u",
                          NitroPort_EngineAOnTop() ? 'A' : 'B',
                          live[0], dup[0], topTile[0], top[0],
                          live[1], dup[1], topTile[1], top[1]);
            /* (the top engine's first entries: tile at x,y. A sprite drawn
             * many times shows up here as the same tile at many positions) */
            {
                const unsigned char *oam = NitroPort_Oam + (NitroPort_EngineAOnTop() ? 0 : 0x400);
                char line[220];
                int n = 0, shown = 0;

                for (i = 0; i < 128 && shown < 10; i++) {
                    const unsigned char *a = oam + i * 8;

                    if ((a[1] & 0x03) == 0x02) {
                        continue;
                    }
                    shown++;
                    n += snprintf(line + n, sizeof(line) - (size_t)n, " %u@%u,%u",
                                  (unsigned)(((unsigned)a[4] | ((unsigned)a[5] << 8)) & 0x3FFu),
                                  (unsigned)(((unsigned)a[2] | ((unsigned)a[3] << 8)) & 0x1FFu),
                                  (unsigned)((unsigned)a[0] & 0xFFu));
                    if ((size_t)n >= sizeof(line) - 24) {
                        break;
                    }
                }
                NitroPort_Log("[perf] oam top entries:%s", shown ? line : " (none)");
            }
        }
        /* The lines above are the ones a run is judged by: put them on the
         * card before the next 600 frames, not a second later. */
        FlushLog();
    }
    sPrevStateUs = stateUs;
    sPrevHBlankTicks = sFrameHBlankTicks;
    sPrevHandoffTicks = sFrameHandoffTicks;
    sPrevHbKeep = sFrameHbKeep;
    sPrevHbDma = sFrameHbDma;
    sPrevHbIrq = sFrameHbIrq;
#ifndef HGSS_PROFILE_OFF
    sPrevHbRefresh = sFrameHbRefresh;
    sPrevHbSwitch = sFrameHbSwitch;
    sPrevHbCmp = sFrameHbCmp;
#endif

    /* Last, after both blits and the swap: this is what spaces frame starts
     * when the display work finishes early. */
    WaitForFrameCap(frameStart);
}

/* Exit to the Homebrew Launcher (START+SELECT held 2 s). Deliberately NOT
 * the HOME-menu close path: no APT handling, no sQuit, no WaitForGame
 * teardown. Order: save, then a bounded mirror of the normal quit stops
 * (drawers, geometry, display, sound), then the log, then the runtime and
 * the process. The stops matter: ending the process with the DSP still
 * streaming and the display thread inside citro3d leaves hbmenu dirty
 * hardware state and crashes on the way out (the normal path's own
 * comment). Each stop signals and joins with its own timeout, so a hung
 * thread delays the exit instead of trapping it; a stuck one takes the
 * old end-the-process fallback. Called from the game thread; the re-entry
 * guard makes a held combo during teardown wait instead of redoing it. */
static bool sExiting;
static void ExitToLauncher(void)
{
    if (sExiting) {
        for (;;) {
            svcSleepThread(100 * 1000000LL);
        }
    }
    sExiting = true;
    NitroPort_Log("START+SELECT held 2 s: exiting to the Homebrew Launcher (frame %lu)",
                  sFrame);
    NitroPort_CardFlush();
    DrawersStop();
    NitroPort_Log("[quit] drawing threads stopped");
    GpuStopGeometry();
    NitroPort_Log("[quit] geometry stopped");
    GpuStop();
    NitroPort_Log("[quit] display stopped");
    if (sAudio) {
        AudioStop();
        NitroPort_Log("[quit] sound stopped");
    }
#if defined(HGSS_A0_DIAG) || !defined(HGSS_PROFILE_OFF)
    A0IdleStop();
#endif
    FlushLog();
    if (sGpuStopFailed || sThreadsStuck) {
        /* Same fallback as the normal path: something is still inside
         * citro3d/libctru/NDSP (logged above), so skip the runtime
         * teardown and end the process. */
        NitroPort_Log("[quit] a thread is still running; ending the process");
        svcExitProcess();
    }
    if (sAudio) {
        ndspExit();
    }
    gfxExit();
    svcExitProcess();
    while (1) {
        svcSleepThread(100 * 1000000LL);
    }
}

/* The game stopped itself. */
void NitroPort_Terminate(void)
{
    NitroPort_Log("the game stopped");
    FlushLog();
    sQuit = true;
    threadExit(0);
}

/* The game asked the DS to restart. A real restart reopens the data pack and
 * starts the game again; until that exists, save what is flushed and end. */
void NitroPort_ResetSystem(u32 parameter)
{
    NitroPort_Log("reset requested (%lu): stopping", (unsigned long)parameter);
    FlushLog();
    sQuit = true;
    NitroPort_CardFlush();
    svcExitProcess();
}

/* ------------------------------------------------------------------- crash */

static void HgssOnException(ERRF_ExceptionInfo *info, CpuRegisters *regs)
{
    int i;

    if (sLog != NULL) {
        fprintf(sLog, "=== exception (type %d) pc %08lx lr %08lx cpsr %08lx fsr %08lx far %08lx ===\n",
                (int)info->type, (unsigned long)regs->pc, (unsigned long)regs->lr,
                (unsigned long)regs->cpsr, (unsigned long)info->fsr,
                (unsigned long)info->far);
        for (i = 0; i < 13; i++) {
            fprintf(sLog, "r%d %08lx%s", i, (unsigned long)regs->r[i], (i % 4 == 3) ? "\n" : " ");
        }
        fprintf(sLog, "sp %08lx pc %08lx\n--- last log ---\n",
                (unsigned long)regs->sp, (unsigned long)regs->pc);
        for (i = 0; i < sRingCount; i++) {
            int at = (sRingNext - sRingCount + i + LOG_RING) % LOG_RING;
            fprintf(sLog, "%s\n", sRing[at]);
        }
        fflush(sLog);
    }
    svcSleepThread(1000 * 1000000LL);
    svcExitProcess();
    for (;;) {
    }
}

/* ------------------------------------------------------------- starting */

/* ---------------------------------------------------------------- audio */

/* The DS sound chip mixes into NitroPort_SoundRender: 32768 Hz stereo, the
 * same stream the PC shell writes to a WAV. NDSP mixes at
 * SYSCLOCK_SOC/512 (~32728.5 Hz) and its channel-rate word is "how many
 * source samples per second", so announcing 32768 asks the DSP to read
 * 1.0012 source samples per output sample. Its interpolator for that case
 * is 2-tap linear (libctru maps any 1.0 < rate <= 4/3 to NDSP_INTERP_LINEAR,
 * even for POLYPHASE), and a 2-tap interpolator's gain depends on the
 * fractional position: at a 1.0012 ratio that position sweeps 0..1 every
 * ~25 ms, so the high band is amplitude-modulated at 39.5 Hz (0 to -2.9 dB
 * at 8 kHz, -7.8 dB at 12 kHz, -12.9 dB at 14 kHz). That is audible
 * roughness, and a per-buffer interpolation-phase reset would add a click
 * train at the 64 Hz buffer rate. Announcing the DSP's own rate instead
 * makes the ratio exactly 1.0, so the DSP copies samples and none of it
 * exists; the price is that the 32768 Hz stream is then consumed 0.12%
 * slow (two cents flat, and the whole soundtrack 0.12% longer), which is
 * inaudible. (Announcing 32768 itself introduced no pitch error -- the
 * DSP consumed exactly 32768 source samples a second -- so earlier notes
 * that called this path "two cents sharp" were describing nothing.)
 * Four blocks of 15.6 ms keep it fed while the game thread is busy; the
 * mixer takes its own lock, so this thread never holds the game up. */
#define AUDIO_RATE 32768                       /* the DS mixer's rate */
#define AUDIO_FRAMES 512                       /* per block, per channel */
#define AUDIO_BLOCKS 4
#define AUDIO_THREAD_STACK (16 * 1024)
#define AUDIO_THREAD_PRIORITY 0x2e             /* above the game (0x30) and the
                                                * geometry thread (0x2f): a
                                                * composite can hold its core
                                                * for 20-30 ms, and a 512-frame
                                                * block is due every 15.6 ms */

static Thread sAudioThread;
static volatile bool sAudioRunning;
static int16_t *sAudioData;
static ndspWaveBuf sAudioWave[AUDIO_BLOCKS];

static void AudioThread(void *arg)
{
    (void)arg;
    A0ThreadRunning("audio");
    while (sAudioRunning) {
        int slot = -1;
        int i;

        for (i = 0; i < AUDIO_BLOCKS; i++) {
            if (sAudioWave[i].status == NDSP_WBUF_FREE ||
                sAudioWave[i].status == NDSP_WBUF_DONE) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            svcSleepThread(1000000LL);   /* all four queued: let the DSP work */
            continue;
        }
        {
            u64 mixStart = svcGetSystemTick();

            NitroPort_SoundRender(sAudioData + (size_t)slot * AUDIO_FRAMES * 2u,
                                  AUDIO_FRAMES);
            __atomic_fetch_add(&sSoundMixTicks, svcGetSystemTick() - mixStart,
                               __ATOMIC_RELAXED);
        }
        /* The DSP reads the block with its own DMA, which does not see the
         * ARM11's write-back L1. libctru's ndsp never flushes (every NDSP
         * example calls this itself), so without it the DSP plays whatever
         * happens to still be in FCRAM: an untouched buffer, or -- the usual
         * case, since the game keeps evicting 64-byte lines -- a random mix
         * of the current block and pieces of the block from ~62 ms ago.
         * That sounds like the music, only scratchy/crunchy. */
        (void)DSP_FlushDataCache(sAudioData + (size_t)slot * AUDIO_FRAMES * 2u,
                                 AUDIO_FRAMES * 4u);
        ndspChnWaveBufAdd(0, &sAudioWave[slot]);
    }
}

static void AudioStart(void)
{
    size_t bytes = sizeof(int16_t) * 2u * AUDIO_FRAMES * AUDIO_BLOCKS;
    int i;

    sAudioData = linearAlloc(bytes);
    if (sAudioData == NULL) {
        NitroPort_Log("audio: out of linear memory; staying silent");
        return;
    }
    memset(sAudioData, 0, bytes);
    memset(sAudioWave, 0, sizeof(sAudioWave));
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    /* The DSP's own rate, not AUDIO_RATE: ratio exactly 1.0, no resampling.
     * See the comment above AUDIO_RATE. */
    ndspChnSetRate(0, (float)NDSP_SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    for (i = 0; i < AUDIO_BLOCKS; i++) {
        sAudioWave[i].data_pcm16 = sAudioData + (size_t)i * AUDIO_FRAMES * 2u;
        sAudioWave[i].nsamples = AUDIO_FRAMES;
        sAudioWave[i].looping = false;
    }
    sAudioRunning = true;
    /* An Old 3DS keeps the mixer on core 0 (as before wave 3): moving it to
     * core 1 was tried and reverted. Log 9 vs log 8 in the same field (same
     * OAM, same verts, same scenes/s): core 1 went geometry 61-79% -> 84-100%
     * busy for identical throughput, audio 9-28% -> 24-40% (wall-clock
     * inflation under preemption), and the display starved to 0.0 composes/s.
     * Core 1's 80% share fits geometry + display but not audio on top;
     * priority cannot create time on an oversubscribed core. `audiocore=`
     * keeps the override for A/B runs. A New 3DS keeps -2. */
    sAudioCore = sIniAudioCore >= 0 ? sIniAudioCore : (sNew3ds ? -2 : 0);
    sAudioThread = threadCreate(AudioThread, NULL, AUDIO_THREAD_STACK,
                                AUDIO_THREAD_PRIORITY, sAudioCore, false);
    if (sAudioThread == NULL && sAudioCore != 0) {
        sAudioThread = threadCreate(AudioThread, NULL, AUDIO_THREAD_STACK,
                                    AUDIO_THREAD_PRIORITY, 0, false);
        sAudioCore = 0;
    }
    if (sAudioThread == NULL) {
        sAudioRunning = false;
        NitroPort_Log("audio: can't start its thread; staying silent");
        return;
    }
    NitroPort_Log("audio: mixer %d Hz, DSP %d Hz (no resampling), %d blocks of %d frames, on core %d",
                  AUDIO_RATE, (int)(NDSP_SAMPLE_RATE + 0.5),
                  AUDIO_BLOCKS, AUDIO_FRAMES, sAudioCore);
}

static void AudioStop(void)
{
    if (sAudioThread != NULL) {
        sAudioRunning = false;
        if (R_SUCCEEDED(threadJoin(sAudioThread, 1000 * 1000000LL))) {
            threadFree(sAudioThread);
        } else {
            sThreadsStuck = true;
            NitroPort_Log("[quit] the audio thread didn't stop");
        }
        sAudioThread = NULL;
    }
}

/* The console's profile stands in for the DS's user settings. */
static void ReadProfile(HeartgoldSettings *settings)
{
    u8 language;
    u16 name[0x1C / 2];
    u8 birthday[2];
    int i;

    Heartgold_DefaultSettings(settings);
    if (R_FAILED(cfguInit())) {
        return;
    }
    if (R_SUCCEEDED(CFGU_GetSystemLanguage(&language)) && language <= 5) {
        settings->language = language;
    }
    memset(name, 0, sizeof(name));
    if (R_SUCCEEDED(CFGU_GetConfigInfoBlk2(sizeof(name), 0x000A0000, name)) && name[0] != 0) {
        for (i = 0; i < 10 && name[i] != 0; i++) {
            settings->nickname[i] = name[i];
        }
        settings->nickname[i] = 0;
    }
    if (R_SUCCEEDED(CFGU_GetConfigInfoBlk2(sizeof(birthday), 0x000A0001, birthday)) &&
        birthday[0] >= 1 && birthday[0] <= 12 && birthday[1] >= 1 && birthday[1] <= 31) {
        settings->birthMonth = birthday[0];
        settings->birthDay = birthday[1];
    }
    cfguExit();
}

/* The active config file, chosen at boot by SelectIniPath: port.ini by
 * default, port_A.ini while START is held, port_B.ini while SELECT is held.
 * Every IniValue/ScriptPath read goes through this path, so one boot with
 * a held button is one alternate settings combination with no file swaps. */
static char sIniPath[256];

/* Pick the config file from the buttons held at launch. Called once, after
 * the log is open (so the choice is logged) and before any IniValue read.
 * START -> port_A.ini, SELECT -> port_B.ini, both -> port_A.ini; anything
 * else -> port.ini. */
static void SelectIniPath(void)
{
    uint32_t held;

    snprintf(sIniPath, sizeof(sIniPath), "%s", INI_PATH);
    hidScanInput();
    held = hidKeysHeld();
    if ((held & KEY_START) && (held & KEY_SELECT)) {
        snprintf(sIniPath, sizeof(sIniPath), "%s", INI_PATH_A);
        NitroPort_Log("[ini] START+SELECT held at boot: using port_A.ini");
    } else if (held & KEY_START) {
        snprintf(sIniPath, sizeof(sIniPath), "%s", INI_PATH_A);
        NitroPort_Log("[ini] START held at boot: using port_A.ini");
    } else if (held & KEY_SELECT) {
        snprintf(sIniPath, sizeof(sIniPath), "%s", INI_PATH_B);
        NitroPort_Log("[ini] SELECT held at boot: using port_B.ini");
    } else {
        NitroPort_Log("[ini] using port.ini");
    }
}

/* One `key=value` from the active config file. Returns 1 and copies the trimmed value when
 * the key is present and not empty; 0 when the file or the key is missing.
 * The existing ScriptPath() keeps its own lenient reader; this one is for the
 * numeric settings that need an exact key. */
static int IniValue(const char *key, char *value, size_t size)
{
    FILE *file = fopen(sIniPath, "rb");
    char line[256];
    size_t keyLen = strlen(key);

    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *at = line;
        char *end;

        if (strncmp(at, key, keyLen) != 0) {
            continue;
        }
        at += keyLen;
        while (*at == ' ' || *at == '\t') {
            at++;
        }
        if (*at != '=') {
            continue;
        }
        at++;
        while (*at == ' ' || *at == '\t') {
            at++;
        }
        end = at + strlen(at);
        while (end > at && (end[-1] == '\n' || end[-1] == '\r' ||
                            end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }
        if (*at == '\0') {
            continue;
        }
        snprintf(value, size, "%s", at);
        fclose(file);
        return 1;
    }
    fclose(file);
    return 0;
}

/* `fps=60` (default) or `fps=30` in port.ini, read before the game's thread
 * starts. Anything else -- including a missing file -- keeps 60 Hz. */
static void ReadFrameCap(void)
{
    char value[16];
    char *end;
    long fps;

    sFrameCap = DEFAULT_FPS;
    if (!IniValue("fps", value, sizeof(value))) {
        NitroPort_Log("[perf] frame cap %d Hz", sFrameCap);
        return;
    }
    fps = strtol(value, &end, 10);
    while (*end == ' ' || *end == '\t') {
        end++;
    }
    if (*end != '\0' || (fps != 60 && fps != 30)) {
        NitroPort_Log("[perf] port.ini fps=%s is not 60 or 30; using %d Hz",
                      value, DEFAULT_FPS);
        NitroPort_Log("[perf] frame cap %d Hz", sFrameCap);
        return;
    }
    sFrameCap = (int)fps;
    NitroPort_Log("[perf] frame cap %d Hz", sFrameCap);
    if (sFrameCap == 30) {
        NitroPort_Log("[perf] 30 Hz is a comparison mode: the game clock is "
                      "per frame, so the game runs at half speed");
    }
}

/* The startup display knobs in port.ini (the runtime X+R presets still
 * override `filter`). Read before the ComposeConfig is filled; the defaults
 * are the ones the shell has always used. `wide=off` pillarboxes the upper
 * screen, so it is a layout choice, not a performance knob. */
static int sIniFilter = COMPOSE_FILTER_SHARP;
static int sIniStereo = 1;
static int sIniWide = 1;
/* The 2D layer's texels a DS pixel. 0 is automatic (2 for the sharp filters
 * when there is memory for them); an Old 3DS gets 1 below, because the 2x
 * sets are 13,824 KB and the failed attempt is what makes the memory ladder
 * drop the lower menu and the weather BG before it settles. */
static int sIniTexScale = 0;
/* `displaycore=0|1`: -1 is the console's default (core 1 on an Old 3DS,
 * core 0 on a New 3DS). It moves only the display thread; its priority
 * stays the console's default. */
static int sIniDisplayCore = -1;
/* `audiocore=0|1`: -1 is the console's default (core 1 on an Old 3DS, where
 * the drawer owns core 0; any core on a New 3DS, as before). It moves only
 * the audio thread; its priority stays above the game. (Declared above.) */
static int IniOnOff(const char *value, int *out)
{
    if (strcmp(value, "on") == 0 || strcmp(value, "1") == 0) {
        *out = 1;
        return 1;
    }
    if (strcmp(value, "off") == 0 || strcmp(value, "0") == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static void ReadDisplayOptions(void)
{
    char value[32];

    sIniFilter = COMPOSE_FILTER_SHARP;
    sIniStereo = 1;
    sIniWide = 1;
    sIniDisplayCore = -1;
    sIniAudioCore = -1;
    sIniSched = 0;
    sIniLower30 = 0;
    sIniDrawerAB = 0;
    sIniRefHashB = 1;
    sIniRefHashA = 1;          /* (the drawer's cost on an Old 3DS) */
    sIniRefHashDiag = 0;
    sIniPartialB = 0;
    sIniPartialBDiag = 0;
    sIniPartialBChar = 0;
    sIniBandPubB = 0;
    sIniBandPubBDiag = 0;
    sIniSw2d = 0;
    if (IniValue("filter", value, sizeof(value))) {
        if (strcmp(value, "sharp") == 0) {
            sIniFilter = COMPOSE_FILTER_SHARP;
        } else if (strcmp(value, "nearest") == 0) {
            sIniFilter = COMPOSE_FILTER_NEAREST;
        } else if (strcmp(value, "smooth") == 0) {
            sIniFilter = COMPOSE_FILTER_SMOOTH;
        } else if (strcmp(value, "scale2x") == 0) {
            sIniFilter = COMPOSE_FILTER_SCALE2X;
        } else {
            NitroPort_Log("[perf] port.ini filter=%s is not sharp, nearest, smooth "
                          "or scale2x; using sharp", value);
        }
        if (sIniFilter != COMPOSE_FILTER_SHARP) {
            NitroPort_Log("[perf] filter=%s", value);
        }
    }
    if (IniValue("stereo", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniStereo)) {
            NitroPort_Log("[perf] port.ini stereo=%s is not on/off/1/0; using on", value);
            sIniStereo = 1;
        } else if (!sIniStereo) {
            NitroPort_Log("[perf] stereo=off (the 3D is flat)");
        }
    }
    if (IniValue("wide", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniWide)) {
            NitroPort_Log("[perf] port.ini wide=%s is not on/off; using on", value);
            sIniWide = 1;
        } else if (!sIniWide) {
            NitroPort_Log("[perf] wide=off (pillarboxed; not a performance setting)");
        }
    }
    if (IniValue("texscale", value, sizeof(value))) {
        long scale = strtol(value, NULL, 10);

        if (scale == 1 || scale == 2) {
            sIniTexScale = (int)scale;
            NitroPort_Log("[perf] texscale=%d (the 2D layer's texels a pixel)", sIniTexScale);
        } else {
            NitroPort_Log("[perf] port.ini texscale=%s is not 1 or 2; automatic", value);
            sIniTexScale = 0;
        }
    }
    if (IniValue("displaycore", value, sizeof(value))) {
        if (strcmp(value, "0") == 0 || strcmp(value, "1") == 0) {
            sIniDisplayCore = value[0] - '0';
            NitroPort_Log("[perf] displaycore=%d (overriding the console's default)",
                          sIniDisplayCore);
        } else {
            NitroPort_Log("[perf] port.ini displaycore=%s is not 0 or 1; using the console's "
                          "default", value);
        }
    }
    if (IniValue("audiocore", value, sizeof(value))) {
        if (strcmp(value, "0") == 0 || strcmp(value, "1") == 0) {
            sIniAudioCore = value[0] - '0';
            NitroPort_Log("[perf] audiocore=%d (overriding the console's default)",
                          sIniAudioCore);
        } else {
            NitroPort_Log("[perf] port.ini audiocore=%s is not 0 or 1; using the console's "
                          "default", value);
        }
    }
    if (IniValue("lower30", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniLower30)) {
            NitroPort_Log("[perf] port.ini lower30=%s is not on/off; using off", value);
            sIniLower30 = 0;
        }
        NitroPort_Log("[perf] lower30=%s (engine B offered every other tick; input remains per tick)",
                      sIniLower30 ? "on" : "off");
    }
    if (IniValue("drawerAB", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniDrawerAB)) {
            NitroPort_Log("[perf] port.ini drawerAB=%s is not on/off; using off", value);
            sIniDrawerAB = 0;
        }
        NitroPort_Log("[perf] drawerAB=%s (A publishes before B; mixed-rate pair)",
                      sIniDrawerAB ? "on" : "off");
    }
    if (IniValue("refHashB", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniRefHashB)) {
            NitroPort_Log("[perf] port.ini refHashB=%s is not on/off; using off", value);
            sIniRefHashB = 0;
        }
        if (sIniRefHashB) {
            NitroPort_Log("[perf] refHashB=on (referenced engine-B ranges)");
        }
    }
    if (IniValue("refHashA", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniRefHashA)) {
            NitroPort_Log("[perf] port.ini refHashA=%s is not on/off; using on", value);
            sIniRefHashA = 1;
        }
        NitroPort_Log("[perf] refHashA=%s (referenced engine-A ranges: the change check reads only what the frame shows)",
                      sIniRefHashA ? "on" : "off");
    }
    if (IniValue("refHashDiag", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniRefHashDiag)) {
            NitroPort_Log("[perf] port.ini refHashDiag=%s is not on/off; using off", value);
            sIniRefHashDiag = 0;
        }
    }
    if (IniValue("partialB", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniPartialB)) {
            NitroPort_Log("[perf] port.ini partialB=%s is not on/off; using off", value);
            sIniPartialB = 0;
        }
        NitroPort_Log("[perf] partialB=%s (engine-B band redraw; default off)",
                      sIniPartialB ? "on" : "off");
    }
    if (IniValue("partialBDiag", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniPartialBDiag)) {
            NitroPort_Log("[perf] port.ini partialBDiag=%s is not on/off; using off", value);
            sIniPartialBDiag = 0;
        }
    }
    if (IniValue("partialBChar", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniPartialBChar)) {
            NitroPort_Log("[perf] port.ini partialBChar=%s is not on/off; using off", value);
            sIniPartialBChar = 0;
        }
        NitroPort_Log("[perf] partialBChar=%s (text character dirty bands; default off)",
                      sIniPartialBChar ? "on" : "off");
    }
    if (IniValue("bandPubB", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniBandPubB)) {
            NitroPort_Log("[perf] port.ini bandPubB=%s is not on/off; using off", value);
            sIniBandPubB = 0;
        }
        NitroPort_Log("[perf] bandPubB=%s (engine-B band-diff upload accounting; default off)",
                      sIniBandPubB ? "on" : "off");
    }
    if (IniValue("bandPubBDiag", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniBandPubBDiag)) {
            NitroPort_Log("[perf] port.ini bandPubBDiag=%s is not on/off; using off", value);
            sIniBandPubBDiag = 0;
        }
    }
    if (IniValue("sw2d", value, sizeof(value))) {
        if (!IniOnOff(value, &sIniSw2d)) {
            NitroPort_Log("[perf] port.ini sw2d=%s is not on/off; using off", value);
            sIniSw2d = 0;
        }
        NitroPort_Log("[perf] sw2d=%s (full software raster, no GPU-BG takes; measurement only)",
                      sIniSw2d ? "on" : "off");
    }
    if (IniValue("o3dsched", value, sizeof(value))) {
        if (strcmp(value, "default") == 0) {
            sIniSched = 0;
        } else if (strcmp(value, "display-first") == 0) {
            sIniSched = 1;
            NitroPort_Log("[perf] o3dsched=display-first (geometry demoted below the display on core 1)");
        } else {
            NitroPort_Log("[perf] port.ini o3dsched=%s is not default or display-first; using default",
                          value);
            sIniSched = 0;
        }
        if (sNew3ds && sIniSched) {
            NitroPort_Log("[perf] o3dsched is Old-3DS-only; ignoring it on a New 3DS");
            sIniSched = 0;
        }
    } else if (!sNew3ds) {
        /* No key (the frozen benchmark port.ini has none): Old 3DS defaults
         * to display-first. Same-core threads don't time-slice, so with
         * geometry above, the display starves outright under saturation
         * (log-9's and log-23's 0.0 composes/s) instead of slowing down;
         * the event-driven display still yields to geometry when idle. */
        sIniSched = 1;
        NitroPort_Log("[perf] o3dsched default is display-first on Old 3DS (geometry demoted below the display on core 1)");
    }
    NitroPort_Log("[perf] display options: filter %d, stereo %d, wide %d, texscale %d",
                  sIniFilter, sIniStereo, sIniWide, sIniTexScale);
    NitroPort_Render2DSetReferenceHash(sIniRefHashB, sIniRefHashDiag);
    NitroPort_Render2DSetReferenceHashA(sIniRefHashA);
    NitroPort_Render2DSetPartialBChar(sIniPartialBChar);
    NitroPort_Render2DSetPartialB(sIniPartialB, sIniPartialBDiag);
    NitroPort_ComposeSetBandPublish(sIniBandPubB, sIniBandPubBDiag);
}

/* `script=PATH` in port.ini (active file: port.ini/port_A.ini/port_B.ini),
 * or the default script path. */
static const char *ScriptPath(void)
{
    static char path[256];
    FILE *file = fopen(sIniPath, "rb");
    char line[256];

    if (file == NULL) {
        return DEFAULT_SCRIPT;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *value;
        char *end;

        if (strncmp(line, "script", 6) != 0) {
            continue;
        }
        value = line + 6;
        while (*value == ' ' || *value == '\t' || *value == '=') {
            value++;
        }
        end = value + strlen(value);
        while (end > value && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) {
            *--end = '\0';
        }
        if (*value != '\0') {
            /* A bare name (script=run.txt) is taken relative to the data
             * folder; an absolute path or an sdmc:/ path is used as
             * written. Without this the relative name would resolve against
             * the loader's working directory, which is not DATA_DIR. */
            if (value[0] == '/' || strncmp(value, "sdmc:", 5) == 0) {
                snprintf(path, sizeof(path), "%s", value);
            } else {
                snprintf(path, sizeof(path), DATA_DIR "/%s", value);
            }
            fclose(file);
            return path;
        }
    }
    fclose(file);
    return DEFAULT_SCRIPT;
}

static void LoadScript(const HeartgoldSettings *settings)
{
    const char *path = ScriptPath();
    FILE *file;
    long size;
    char *text;

    (void)settings;
    file = fopen(path, "rb");
    if (file == NULL) {
        NitroPort_Log("no input script at %s (using the console's controls)", path);
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return;
    }
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 65536) {
        fclose(file);
        NitroPort_Log("%s is empty or too large", path);
        return;
    }
    text = malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(file);
        return;
    }
    if (fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return;
    }
    text[size] = '\0';
    fclose(file);

    if (HgssScript_Parse(text, &sScript) != 0) {
        NitroPort_Log("%s has a malformed input line", path);
        free(text);
        return;
    }
    free(text);
    sScriptLoaded = true;
    NitroPort_Log("replaying %s: %d inputs, end frame %ld", path, sScript.count, sScript.end);
}

static void GameThread(void *arg)
{
    static ERRF_ExceptionData exceptionData;
    static uint8_t exceptionStack[8192] __attribute__((aligned(16)));

    A0ThreadRunning("game");

    sGameThreadAlive = true;
    threadOnException(HgssOnException, exceptionStack + sizeof(exceptionStack), &exceptionData);
    Heartgold_Run((const HeartgoldSettings *)arg);
    /* Reaching here means NitroMain returned, which the game does not do in
     * normal play. Say so: this flag, not libctru's thread state, is what the
     * main thread trusts (see WaitForGame). */
    NitroPort_Log("the game's thread is returning (frame %lu)", sFrame);
    sGameThreadAlive = false;
}

/* APT hooks: HOME and sleep pause, wake resumes. */
/* A drawing thread is still working on a frame. */
static bool AnyDrawerBusy(void)
{
    int i;

    for (i = 0; i < sDrawerCount; i++) {
        if (__atomic_load_n(&sDrawers[i].busy, __ATOMIC_ACQUIRE)) {
            return true;
        }
    }
    return false;
}

static void AptHook(APT_HookType hook, void *param)
{
    (void)param;
    switch (hook) {
    case APTHOOK_ONSUSPEND:
    case APTHOOK_ONSLEEP:
    {
        u64 start = svcGetSystemTick();
        bool settled;

        if (hook == APTHOOK_ONSUSPEND) {
            sAptHome = true;
        } else {
            sAptAsleep = true;
        }
        sPaused = true;
        __atomic_store_n(&sSuspended, true, __ATOMIC_SEQ_CST);

        /* Busy-first order: a display frame that passed its own check
         * before this store either sees sSuspended or we see sComposing.
         * Wait for it to finish before dropping the presentation queue, so
         * no compose can submit to the GPU after APT takes it. */
        while (__atomic_load_n(&sComposing, __ATOMIC_SEQ_CST) &&
               svcGetSystemTick() - start < SYSCLOCK_ARM11) {
            svcSleepThread(1000000LL);
        }
        NitroPort_ComposeHold(1);
        while ((AnyDrawerBusy() || NitroPort_ComposeGpuBusy()) &&
               svcGetSystemTick() - start < SYSCLOCK_ARM11) {
            svcSleepThread(1000000LL);
        }
        settled = !__atomic_load_n(&sComposing, __ATOMIC_SEQ_CST) &&
                  !AnyDrawerBusy() && !NitroPort_ComposeGpuBusy();
        NitroPort_Log("[apt] paused hook=%d settled=%d composing=%d drawers=%d gpu=%d waited=%llu ms",
                      (int)hook, settled,
                      __atomic_load_n(&sComposing, __ATOMIC_SEQ_CST),
                      AnyDrawerBusy(), NitroPort_ComposeGpuBusy(),
                      (unsigned long long)(svcGetSystemTick() - start) * 1000ULL / SYSCLOCK_ARM11);
        break;
    }
    case APTHOOK_ONRESTORE:
    case APTHOOK_ONWAKEUP:
        if (hook == APTHOOK_ONRESTORE) {
            sAptHome = false;
        } else {
            sAptAsleep = false;
        }
        /* Still in the other suspend source: do not touch the display. */
        if (sQuit || sAptHome || sAptAsleep) {
            return;
        }
        /* citro3d's own APT hook runs after ours (hooks are LIFO). Defer
         * the real resume until aptMainLoop has returned so its PICA
         * state is restored before the display thread composes again. */
        __atomic_store_n(&sResumePending, true, __ATOMIC_RELEASE);
        NitroPort_Log("[apt] restore hook %d: resume pending", (int)hook);
        break;
    case APTHOOK_ONEXIT:
        sQuit = true;
        break;
    default:
        break;
    }
}

/* Wait for the game's thread; the HOME menu's close ends the wait. */
static void WaitForGame(Thread thread)
{
    unsigned long quiet = 0;
    bool premature = false;

    /* APT belongs to the game thread: NitroPort_OnFrame calls aptMainLoop
     * once a frame, which is what libctru documents ("call this at the
     * beginning of every frame"). Calling it here as well is that function's
     * misuse, and a poll 100 ms after start returned false while the game was
     * still loading overlays: the program stopped at frame 0 with the game
     * thread alive (docs/progress/phase-5.md).
     *
     * The thread's own flag decides when it has ended, not threadJoin():
     * libctru returns success as soon as its Thread.finished flag is set, and
     * the hardware log showed that happening while the game was still inside
     * NitroMain (a game-thread line followed it). */
    while (!sQuit && sGameThreadAlive) {
        if (R_SUCCEEDED(threadJoin(thread, 50 * 1000000LL)) && sGameThreadAlive) {
            if (!premature) {
                premature = true;
                NitroPort_Log("threadJoin says the game's thread ended, but it is still running");
            }
            svcSleepThread(20 * 1000000LL);   /* (do not spin on a stale join) */
        }
        if (sFrame == 0 && ++quiet == 200) {
            NitroPort_Log("still starting: no frame after 10 s");
        }
    }
    if (!sGameThreadAlive) {
        NitroPort_Log("the game's thread ended at frame %lu", sFrame);
    } else {
        NitroPort_Log("closing at frame %lu", sFrame);
    }
    if (R_SUCCEEDED(threadJoin(thread, 5000 * 1000000LL))) {
        threadFree(thread);
    } else {
        /* Freeing a thread that is still running frees the stack it is
         * executing on, and the process then dies somewhere unrelated. That
         * is what crashed on hardware: the loop above saw sGameThreadAlive
         * false before the new thread had run its first instruction, treated
         * "not started" as "ended", and freed it after a 2 s join while the
         * game was at frame 110 loading overlays. Leave it instead; the
         * caller ends the process on sThreadsStuck. */
        sThreadsStuck = true;
        NitroPort_Log("the game's thread is still running; not freeing it");
    }
}

/* The error screen when the pack is missing or rejected. */
static void PakErrorScreen(const char *error)
{
    NitroPort_Log("Can't start: %s.", error);
    consoleInit(GFX_BOTTOM, NULL);
    consoleClear();
    printf("\n " HEARTGOLD_APP_NAME "\n\n Can't start: %s.\n\n"
           " Put " HEARTGOLD_PAK_NAME " and manifest.json\n"
           " (made with the builder from your own\n"
           " cartridge) in:\n\n  " DATA_DIR "/\n\n"
           " See " DATA_DIR "/log.txt for the details.\n\n"
           " Press START to exit.\n", error);
    if (!sAudio) {
        printf("\n Sound is unavailable (no dspfirm.cdc);\n the game would run silent.\n");
    }
    while (aptMainLoop() && !sQuit) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        svcSleepThread(100 * 1000000LL);
    }
}

/* The remaster is mandatory: there is no 1:1 display to fall back to. */
static void DisplayErrorScreen(void)
{
    consoleInit(GFX_BOTTOM, NULL);
    consoleClear();
    printf("\n " HEARTGOLD_APP_NAME "\n\n"
           " The 3D display could not start.\n\n"
           " See " DATA_DIR "/log.txt for the error.\n\n"
           " Press START to exit.\n");
    while (aptMainLoop() && !sQuit) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        svcSleepThread(100 * 1000000LL);
    }
}

/* Past logs are never overwritten: before truncating log.txt, rename a
 * non-empty previous log to a timestamped archive
 * (log-YYYYMMDD-HHMMSS-<build>.txt, with -2/-3 on collision). Empty files
 * are left alone so the emulator probe's truncate-then-boot leaves no
 * empty archives behind. Failures are ignored: a missing archive must
 * never stop the game. Returns true and stores the archive's basename in
 * outBase when a rename happened. */
static bool ArchivePreviousLog(char *outBase, size_t outSize)
{
    struct stat st;
    time_t now;
    struct tm *tm;
    char stamp[32];
    char safeBuild[128];
    char archive[256];
    size_t i;
    int n;

    if (outBase != NULL && outSize > 0) {
        outBase[0] = '\0';
    }
    if (stat(LOG_PATH, &st) != 0 || st.st_size <= 0) {
        return false;
    }
    now = time(NULL);
    tm = localtime(&now);
    if (tm != NULL) {
        snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        snprintf(stamp, sizeof(stamp), "tick-%llu",
                 (unsigned long long)svcGetSystemTick());
    }
    /* FAT-safe build tag: keep [A-Za-z0-9-], flatten anything else. */
    for (i = 0; i + 1 < sizeof(safeBuild) && HGSS_BUILD_ID[i] != '\0'; i++) {
        char c = HGSS_BUILD_ID[i];
        safeBuild[i] = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                       (c >= '0' && c <= '9') || c == '-' ? c : '-';
    }
    safeBuild[i] = '\0';
    if (safeBuild[0] == '\0') {
        snprintf(safeBuild, sizeof(safeBuild), "unknown");
    }
    snprintf(archive, sizeof(archive), DATA_DIR "/log-%s-%s.txt",
             stamp, safeBuild);
    if (stat(archive, &st) == 0) {
        for (n = 2; n < 100; n++) {
            char sized[256];
            snprintf(sized, sizeof(sized), DATA_DIR "/log-%s-%s-%d.txt",
                     stamp, safeBuild, n);
            if (stat(sized, &st) != 0) {
                snprintf(archive, sizeof(archive), "%s", sized);
                break;
            }
        }
    }
    if (rename(LOG_PATH, archive) != 0) {
        return false;
    }
    if (outBase != NULL && outSize > 0) {
        const char *base = strrchr(archive, '/');
        snprintf(outBase, outSize, "%s", base != NULL ? base + 1 : archive);
    }
    return true;
}

int main(int argc, char **argv)
{
    HeartgoldSettings settings;
    const char *error;
    aptHookCookie hook;
    Thread thread;
    char prevLogBase[160];

    (void)argc;
    (void)argv;

    /* New 3DS: 804 MHz and the L2 cache, as Platinum's shell does. Without
     * this the CPU stays at the Old-3DS clock with the L2 off, and the port's
     * software 2D compositor -- memory-bound -- pays for it directly: the
     * first hardware run measured 65-97 ms a frame (13 Hz) with this missing.
     * Both calls are no-ops on an Old 3DS, which stays at 268 MHz. */
    osSetSpeedupEnable(true);
    if (R_SUCCEEDED(ptmSysmInit())) {
        PTMSYSM_ConfigureNew3DSCPU(3);
        ptmSysmExit();
    }

    gfxInitDefault();
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    /* No console: the screens belong to the game. NitroPort_Log writes to
     * stderr, and with a console up every log line was drawn over the bottom
     * screen (and, once the per-engine skip was added, never repainted). The
     * console is brought up only for the "can't start" screen. */

    mkdir("sdmc:/3ds", 0777);
    mkdir(DATA_DIR, 0777);
    prevLogBase[0] = '\0';
    ArchivePreviousLog(prevLogBase, sizeof(prevLogBase));
    LightLock_Init(&sLogLock);
    sLogLockReady = true;
    sLog = fopen(LOG_PATH, "w");
    if (sLog != NULL) {
        setvbuf(sLog, sLogBuf, _IOFBF, sizeof(sLogBuf));
        sLogLastFlush = svcGetSystemTick();
    }

    NitroPort_Log(HEARTGOLD_APP_NAME " starting (build %s)", HGSS_BUILD_ID);
    if (prevLogBase[0] != '\0') {
        NitroPort_Log("previous log preserved as %s", prevLogBase);
    }
    /* Alternate configs for fast A/B: START held here reads port_A.ini,
     * SELECT reads port_B.ini. Logged, so any log traces to its file. */
    SelectIniPath();
#ifdef HGSS_A0_DIAG
    NitroPort_Log("[a0] instrumentation revision halfA-reasons-geometry-idle-v2");
#endif
#ifdef HGSS_GX3D_INDEXED
    NitroPort_Log("[gx3d] indexed eligibility revision scene-outline-buffers-reuse-probe-v2");
#endif
    {
        bool isNew3ds = false;

        APT_CheckNew3DS(&isNew3ds);
        sNew3ds = isNew3ds;
        NitroPort_Log("[perf] %s: %s", isNew3ds ? "New 3DS" : "Old 3DS",
                      isNew3ds ? "804 MHz + L2 requested"
                               : "268 MHz, the speedup is New-3DS only");
    }

    /* Before the game's thread: the cap and the startup display options
     * are fixed for the whole run. */
    ReadFrameCap();
    ReadDisplayOptions();
    if (!sNew3ds) {
        /* The Old 3DS profile is fixed at launch: no runtime filter cycle
         * (its presets would reallocate layer sets and 2-4x texture memory
         * mid-run). Both engines are offered every tick. */
        NitroPort_Log("[perf] Old 3DS profile: launch-time filter only, lower 2D offered every tick, o3dsched %s",
                      sIniSched ? "display-first" : "default");
    }

    /* NDSP only when the console has dspfirm.cdc; otherwise stay silent and
     * say so. Never let this stop the game. */
    if (!sNew3ds) {
        /* Before the audio thread: on an Old 3DS it runs on core 1, and a
         * core-1 thread created before the share is requested fails and
         * falls back to core 0 (log-o3ds-soulsilver-8's "on core 0").
         * Idempotent with the display block's call below. */
        RequestCore1Share();
    }
#if defined(HGSS_A0_DIAG) || !defined(HGSS_PROFILE_OFF)
    if (!sNew3ds) A0IdleStart();
#endif
    if (R_SUCCEEDED(ndspInit())) {
        sAudio = true;
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspSetMasterVol(1.0f);
        NitroPort_Log("NDSP started");
        AudioStart();
    } else {
        NitroPort_Log("no dspfirm.cdc: running silent");
    }

    error = Heartgold_OpenPak(PAK_PATH, MANIFEST_PATH);
    if (error != NULL) {
        PakErrorScreen(error);
    } else {
        ReadProfile(&settings);
        settings.savePath = SAVE_PATH;
        LoadScript(&settings);

        /* The remaster is the display: no classic 1:1 fallback. Compose
         * runs at 400x240/320x240; the 3D is drawn at the screen's resolution
         * and the lower screen is its 256x192 at 1.25x. */
        memset(&sCompose, 0, sizeof(sCompose));
        sCompose.wide = sIniWide;
        sCompose.filter = sIniFilter;
        /* An Old 3DS asks for 1 texel a pixel outright: 2 is arithmetically
         * impossible there (13,824 KB of layer sets against a 15.7 MB heap
         * before this build), and the failed attempt is what made the memory
         * ladder drop the lower menu and the weather BG. */
        sCompose.texScale = sIniTexScale != 0 ? sIniTexScale : (sNew3ds ? 0 : 1);
        sCompose.stereo = sIniStereo;
        sCompose.stereoStrength = 0.02f;
        sCompose.convergence = 0.0f;
        sCompose.edges = 0;
        sCompose.edges3d = 0;
        /* Smooth 3D is the 2x render + average: keep it on the New 3DS.
         * An Old 3DS draws the 3D at the screen's own resolution, which is
         * still native; the 2D upscale is unchanged. */
        sCompose.smooth3d = sNew3ds ? 2 : 0;
        sCompose.flipV = 0;
        sCompose.filter3d = 0;
        sCompose.shadows = 0;
        sCompose.shadowDebug = 0;
        sCompose.grade = 0;
        sCompose.sunFixed = 0;
        sCompose.sunElevation = 55.0f;
        sCompose.sunAzimuth = 32.0f;

        {
            s32 priority = 0x30;
            bool displayReady;

            svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
            /* (the sun shadows' vertex room: PicaInit allocates it, and HGSS
             * never turns them on. Without this it is 1,438 KB of linear
             * memory the 3D texture cache never sees.) */
            /* (before any core-1 thread exists: the display thread goes there
             * on an Old 3DS, and its init must not run under the system's
             * default share) */
            if (!sNew3ds) {
                RequestCore1Share();
            }
            /* (the sun shadows' vertex room: PicaInit allocates it, and HGSS
             * never turns them on. Without this it is 1,438 KB of linear
             * memory the 3D texture cache never sees.) */
            NitroPort_PicaSetShadowRoom(sCompose.shadows);
            {
                /* An Old 3DS has core 0 three times oversubscribed by the
                 * game, audio, drawer and display; put the display on core 1
                 * (which geometry already asks a share of) and below the
                 * drawer so a publish finishes first. A New 3DS keeps the
                 * display on core 0 at the old priority. `displaycore` in
                 * port.ini overrides only the core, for A/B runs. */
                int displayCore = sNew3ds ? 0 : 1;
                s32 displayPriority = sNew3ds ? priority + 1 : priority + 2;

                if (sIniDisplayCore >= 0) {
                    displayCore = sIniDisplayCore;
                }
                displayReady = GpuStart(displayPriority, displayCore);
            }
            if (displayReady) {
                displayReady = DrawersStart(priority, sNew3ds);
            }
            if (displayReady) {
                NitroPort_SetWideScreen(32);
                /* Before the first scene is built: the per-frame call in
                 * OnFrame follows screen swaps, but the first scene must
                 * not be made with the DS's narrower view. */
                NitroPort_Gx3dSetWideView(320);
                GpuStartGeometry(priority);
            } else {
                NitroPort_Log("[compose] the display did not start");
                /* Stop whatever did start before the console takes the
                 * lower screen; the display thread may still be presenting. */
                DrawersStop();
                GpuStop();
                DisplayErrorScreen();
            }
            if (displayReady) {
                aptHook(&hook, AptHook, NULL);
                /* Set before the thread exists: WaitForGame's loop treats a
                 * false flag as "the game ended", and a new thread has not
                 * necessarily run its first instruction by the time this
                 * returns. That race freed a live thread's stack. */
                sGameThreadAlive = true;
                thread = threadCreate(GameThread, &settings, GAME_STACK_SIZE, 0x30, -2, false);
                if (thread == NULL) {
                    sGameThreadAlive = false;
                    NitroPort_Log("Can't start the game's thread.");
                } else {
                    NitroPort_Log("running the game (thread stack %d KiB)", GAME_STACK_SIZE / 1024);
                    WaitForGame(thread);
                }
                if (sExiting) {
                    /* START+SELECT's exit owns the teardown on the game
                     * thread (bounded stops, then the process ends). Parking
                     * here keeps this thread from running a second teardown
                     * over it if HOME arrives mid-exit. */
                    NitroPort_Log("[quit] exit already running on the game thread; waiting");
                    for (;;) {
                        svcSleepThread(100 * 1000000LL);
                    }
                }
                /* Each step is logged: a close that hangs shows where. */
                NitroPort_Log("[quit] closing");
                aptUnhook(&hook);
                NitroPort_Log("[quit] APT unhooked");
                DrawersStop();
                NitroPort_Log("[quit] drawing threads stopped");
                GpuStopGeometry();
                NitroPort_Log("[quit] geometry stopped");
                GpuStop();
                NitroPort_Log("[quit] display stopped");
                NitroPort_Log("[gx3d] %lu 3D images, %lu scenes drawn",
                              (unsigned long)NitroPort_Gx3dImageCount(), sGpuScenes);
            }
        }
    }

    if (sAudio) {
        AudioStop();
        NitroPort_Log("[quit] sound stopped");
        NitroPort_Log("audio: %lu dropped frames of %lu",
                      (unsigned long)ndspGetDroppedFrames(),
                      (unsigned long)ndspGetFrameCount());
    }
#if defined(HGSS_A0_DIAG) || !defined(HGSS_PROFILE_OFF)
    A0IdleStop();
#endif
    NitroPort_Log("stopping (frame %lu)", sFrame);
    NitroPort_CardFlush();
    if (sGpuStopFailed || sThreadsStuck) {        /* A thread is still inside citro3d/libctru/NDSP: tearing the runtime
         * down around it crashes on the way out, so end the process (the log
         * above already says which thread and why). */
        NitroPort_Log("[quit] a thread is still running; ending the process");
        if (sLog != NULL) {
            fflush(sLog);
            fclose(sLog);
            sLog = NULL;
        }
        svcExitProcess();
    }
    if (sLog != NULL) {
        fflush(sLog);
        fclose(sLog);
        sLog = NULL;
    }
    if (sAudio) {
        ndspExit();
    }
    gfxExit();
    return 0;
}
