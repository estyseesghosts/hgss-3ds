/*
 * The game on a 3DS: the Phase 5 shell around Heartgold_Run.
 *
 * Adapted from 3ds_port/game/app/main_3ds.c (Platinum's shell), cut down to
 * the part HGSS needs now: centered 1:1 display, input, APT, a crash log,
 * optional NDSP and the PC run's scripted input. The remaster, wide and
 * lower-menu paths are off.
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
 * them), and writes log.txt there.
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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "3ds_input.h"
#include "heartgold.h"
/* The scene type and the geometry thread's ops. Unlike nitro_port.h this
 * header is standalone (stdint/stddef only), so it can sit next to libctru's. */
#include <nitro_port_gx3d.h>

#define DATA_DIR HEARTGOLD_DATA_DIR
#define PAK_PATH DATA_DIR "/" HEARTGOLD_PAK_NAME
#define MANIFEST_PATH DATA_DIR "/manifest.json"
#define SAVE_PATH DATA_DIR "/" HEARTGOLD_SAVE_NAME
#define LOG_PATH DATA_DIR "/log.txt"
#define INI_PATH DATA_DIR "/port.ini"
#define DEFAULT_SCRIPT DATA_DIR "/script.txt"

/* libctru puts the Thread block at the base of the same allocation the stack
 * grows down into, so an overflow here corrupts the very struct that
 * threadJoin reads. The DS game's stack is its own, but the port's frames ride
 * on this one; 1 MiB costs nothing (the heap has tens of MB) and takes the
 * "did we silently overwrite Thread.finished?" question off the table. */
#define GAME_STACK_SIZE (1024 * 1024)
#define LOG_RING 64
#define LOG_LINE 220

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
void NitroPort_RenderScreens(uint16_t *top, uint16_t *bottom);
/* Per engine: draw only when that engine's inputs changed (returns 1 when
 * drawn). BOOL is int on NitroSDK; the type is spelled out here because the
 * SDK header cannot be included next to libctru's. */
int NitroPort_Render2DIfChanged(int engine, uint16_t *out);
void NitroPort_Render2D(int engine, uint16_t *out);
int NitroPort_EngineAOnTop(void);
void NitroPort_SoundRender(int16_t *stereo, size_t frames);
void NitroPort_SoundPumpReplies(void);

/* The GPU half of the port, from 3ds_port/game/app/port_gx3d_pica.c and
 * port_pica_abi.c (compiled into the program only). NitroPort_PicaRender
 * draws a scene into an off-screen PICA target and hands the RGBA8 image to
 * the port's image API; the next 2D composite converts it. */
int NitroPort_PicaInit(void);
int NitroPort_PicaRender(const NitroGx3dScene *scene, unsigned *dropped);
void NitroPort_PicaExit(void);

static FILE *sLog;
static LightLock sLogLock;
static bool sLogLockReady;
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
static int sFrameCap = DEFAULT_FPS;
static u64 sNextFrameTick;

/* The last log lines, for the exception log. */
static char sRing[LOG_RING][LOG_LINE];
static int sRingCount;
static int sRingNext;

/* 15-bit DS colour -> RGB565. */
static uint16_t Convert(uint16_t c)
{
    u16 r = c & 31;
    u16 g = (c >> 5) & 31;
    u16 b = (c >> 10) & 31;

    return (u16)((r << 11) | (((g << 1) | (g >> 4)) << 5) | b);
}

/* The DS picture centered 1:1 on one 3DS screen. The framebuffer is stored
 * rotated: screen pixel (x, y) is at fb[x * 240 + (239 - y)]. */
static void Blit(const uint16_t *src, uint16_t *fb, int ox, int oy, int height)
{
    int x, y;

    for (y = 0; y < 192 && oy + y < height; y++) {
        for (x = 0; x < 256; x++) {
            fb[(ox + x) * 240 + (239 - (oy + y))] = Convert(src[y * 256 + x]);
        }
    }
}

/* ------------------------------------------------------------------ hooks */

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
        fflush(sLog);
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

/* Draw both screens from the port's 2D renderer, skipping an engine whose
 * inputs did not change: NitroPort_Render2DIfChanged composites that engine
 * only when its registers, palettes, OAM or VRAM moved, and leaves the
 * buffer alone otherwise, so a still screen costs nothing.
 *
 * The swap stays unconditional on purpose. gfxSwapBuffers waits for the
 * display's refresh, and that wait is the only clock the emulated DS has --
 * the port raises a V-blank once per OnFrame -- so skipping it would make the
 * game run fast instead of saving time. Only the composite and the blit are
 * skipped. */
static void DrawFrame(void)
{
    static uint16_t top[256 * 192], bottom[256 * 192];
    u16 width = 0, height = 0;
    uint8_t *topFb;
    uint8_t *bottomFb;
    int aOnTop = NitroPort_EngineAOnTop();

    /* Engine A is the only engine that can show the 3D layer, and its
     * picture changes whenever a new 3D image is published -- which the port
     * only does lazily, so nothing outside it can detect the change in
     * advance. Trying to skip A therefore blacked out the 3D while the 2D
     * text kept drawing (see docs/progress/phase-5.md). A is composed every
     * frame, exactly as NitroPort_RenderScreens does.
     *
     * Engine B is pure 2D: whatever it does not change costs nothing, and
     * that is where the savings come from. */
    NitroPort_Render2D(0, aOnTop ? top : bottom);
    (void)NitroPort_Render2DIfChanged(1, aOnTop ? bottom : top);

    /* Both screens are blitted every frame, always. The framebuffer is
     * double buffered: skipping the blit of an unchanged screen leaves the
     * other buffer showing an older picture, and the display alternates
     * between them -- that is exactly the flicker the intro showed. Only the
     * composite may be skipped; the copy is what makes each buffer whole. */
    topFb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &width, &height);
    if (topFb != NULL) {
        Blit(top, (uint16_t *)topFb, (400 - 256) / 2, (240 - 192) / 2, 240);
    }
    bottomFb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &width, &height);
    if (bottomFb != NULL) {
        Blit(bottom, (uint16_t *)bottomFb, (320 - 256) / 2, (240 - 192) / 2, 240);
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
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

static Thread sGeoThread;
static LightEvent sGeoWorkerEvent, sGeoProducerEvent;
static volatile int sGeoRunning;

/* The port calls this at each SWAP_BUFFERS (port_gx3d.c's weak default is a
 * no-op). It is how the GPU thread learns a scene is ready. */
void NitroPort_Gx3dScenePublished(void)
{
    if (sGpuWakeReady) {
        LightEvent_Signal(&sGpuWake);
    }
}

static void GpuMain(void *arg)
{
    uint32_t seen = 0;

    (void)arg;
    if (!NitroPort_PicaInit()) {
        NitroPort_Log("[gx3d] 3D setup failed; the screens carry on without the 3D layer");
        sGpuRunning = false;
        LightEvent_Signal(&sGpuStarted);
        threadExit(0);
    }
    NitroPort_Log("[gx3d] PICA ready (classic path)");
    LightEvent_Signal(&sGpuStarted);
    while (sGpuRunning) {
        LightEvent_Wait(&sGpuWake);
        if (!sGpuRunning) {
            break;
        }
        /* Drain every scene that is waiting; a newer one replaces an
         * unacquired older one inside the port. */
        for (;;) {
            const NitroGx3dScene *scene;
            unsigned dropped = 0;
            int drawn;

            scene = NitroPort_Gx3dAcquireScene(&seen);
            if (scene == NULL) {
                break;
            }
            drawn = NitroPort_PicaRender(scene, &dropped);
            NitroPort_Gx3dReleaseScene(scene);
            if (drawn >= 0) {
                sGpuScenes++;
                /* Convert here rather than making the game's frame do it:
                 * the 2D composite then only reads a published image. */
                (void)NitroPort_Gx3dConvertPending();
            }
            if (!sGpuRunning) {
                break;
            }
        }
    }
    NitroPort_PicaExit();
    threadExit(0);
}

static bool GpuStart(s32 priority)
{
    LightEvent_Init(&sGpuWake, RESET_ONESHOT);
    LightEvent_Init(&sGpuStarted, RESET_ONESHOT);
    sGpuRunning = true;
    sGpuWakeReady = true;
    /* Core 2 on a New 3DS: the game thread's core is busy compositing both
     * screens in software for most of a frame, and a GPU thread there only
     * runs in the V-blank wait -- the 3D's render then adds to the frame
     * instead of overlapping the 2D. Core 2 also carries the geometry thread
     * (above this one). An Old 3DS has no core 2, so the game's core it is. */
    sGpuThread = threadCreate(GpuMain, NULL, 128 * 1024, priority + 1,
                              sNew3ds ? 2 : -2, false);
    if (sGpuThread == NULL && sNew3ds) {
        sGpuThread = threadCreate(GpuMain, NULL, 128 * 1024, priority + 1, -2, false);
    }
    if (sGpuThread == NULL) {
        sGpuRunning = false;
        sGpuWakeReady = false;
        NitroPort_Log("[gx3d] couldn't start the 3D thread; staying 2D");
        return false;
    }
    /* PicaInit runs inside the thread: on failure it says so and this wait
     * returns with the flag clear, so the shell can stay 2D. */
    LightEvent_Wait(&sGpuStarted);
    if (!sGpuRunning) {
        threadJoin(sGpuThread, 1000 * 1000000LL);
        threadFree(sGpuThread);
        sGpuThread = NULL;
        sGpuWakeReady = false;
        return false;
    }
    NitroPort_Log("[gx3d] display on core %d", sNew3ds ? 2 : 0);
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
    NitroPort_Gx3dThreadMain(&sGeoRunning);
    threadExit(0);
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
    sGeoThread = threadCreate(GeoMain, NULL, 64 * 1024, priority - 1,
                              sNew3ds ? 2 : -2, false);
    if (sGeoThread == NULL && sNew3ds) {
        /* Core 2 is the New 3DS's extra core; if the kernel will not give it
         * (an Old 3DS in a New 3DS's clothes, no 804 MHz mode), the game's
         * core still builds scenes correctly, just not off its thread. */
        sGeoThread = threadCreate(GeoMain, NULL, 64 * 1024, priority - 1, -2, false);
    }
    if (sGeoThread == NULL) {
        NitroPort_Gx3dStopThread();
        sGeoRunning = 0;
        NitroPort_Log("[gx3d] no geometry thread; the game's thread builds scenes");
        return false;
    }
    NitroPort_Log("[gx3d] geometry on its own thread (core %d)", sNew3ds ? 2 : 0);
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

/* Once a frame, from the game's idle path: keep the HOME menu serviced, feed
 * the buttons/touch and draw. */
static unsigned long long TicksToUs(u64 ticks)
{
    return (unsigned long long)ticks * 1000000ULL / SYSCLOCK_ARM11;
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
    if (now < sNextFrameTick) {
        /* svcSleepThread takes nanoseconds; the grid is in ARM11 ticks. */
        u64 ns = (sNextFrameTick - now) * 1000000000ULL / SYSCLOCK_ARM11;
        svcSleepThread((s64)ns);
    } else if (now - sNextFrameTick > period) {
        sNextFrameTick = now + period;
        return;
    }
    sNextFrameTick += period;
}

void NitroPort_OnFrame(void)
{
    u64 frameStart = svcGetSystemTick();
    u64 drawStart;
    unsigned long long totalUs;
    unsigned long long drawUs;
    uint32_t held;
    circlePosition circle;
    uint16_t buttons = 0;
    int touchX = 0, touchY = 0;
    int touchDown = 0;
    touchPosition touch;

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
    if (sPaused) {
        svcSleepThread(16 * 1000000LL);
        return;
    }

    /* The geometry thread has everything the game wrote this frame; a
     * SWAP_BUFFERS in it is the scene the GPU thread will draw. Both shells
     * flush at the same point. */
    NitroPort_Gx3dFlush();

    /* The ARM7's alarm replies (sound ticks and the sequencer) are delivered
     * on this thread, as the PC shell does. */
    NitroPort_SoundPumpReplies();

    hidScanInput();
    held = hidKeysHeld();
    hidCircleRead(&circle);
    buttons = DsButtons(held, circle);
    hidTouchRead(&touch);
    if (held & KEY_TOUCH) {
        touchDown = 1;
        touchX = touch.px;
        touchY = touch.py;
    }

    if (sScriptLoaded) {
        buttons |= HgssScript_HeldButtons(&sScript, (long)sFrame);
        if (HgssScript_TouchAt(&sScript, (long)sFrame, &touchX, &touchY)) {
            touchDown = 1;
        }
    }

    NitroPort_SetButtons(buttons, 0);
    NitroPort_SetTouch(touchDown, (uint16_t)touchX, (uint16_t)touchY);
    drawStart = svcGetSystemTick();
    DrawFrame();
    drawUs = TicksToUs(svcGetSystemTick() - drawStart);
    totalUs = sHaveLastFrameTick ? TicksToUs(frameStart - sLastFrameTick) : 0;
    sLastFrameTick = frameStart;
    sHaveLastFrameTick = true;

    if (sFrame % 600 == 0) {
        unsigned long long logicUs = totalUs > drawUs ? totalUs - drawUs : 0;
        NitroPort_Log("frame %lu", sFrame);
        NitroPort_Log("FRAMETIME frame=%lu logic_us=%llu draw_us=%llu audio_us=0 total_us=%llu",
                      sFrame, logicUs, drawUs, totalUs);
        NitroPort_Log("3D images: %lu (scenes drawn: %lu)",
                      (unsigned long)NitroPort_Gx3dImageCount(), sGpuScenes);
        if (sScriptLoaded && sScript.end > 0 && (long)sFrame > sScript.end) {
            NitroPort_Log("script ended at frame %ld", sScript.end);
            sScriptLoaded = false;
        }
    }

    /* Last, after both blits and the swap: this is what spaces frame starts
     * when the display work finishes early. */
    WaitForFrameCap(frameStart);
}

/* The game stopped itself. */
void NitroPort_Terminate(void)
{
    NitroPort_Log("the game stopped");
    sQuit = true;
    threadExit(0);
}

/* The game asked the DS to restart. A real restart reopens the data pack and
 * starts the game again; until that exists, save what is flushed and end. */
void NitroPort_ResetSystem(u32 parameter)
{
    NitroPort_Log("reset requested (%lu): stopping", (unsigned long)parameter);
    sQuit = true;
    NitroPort_CardFlush();
    svcExitProcess();
}

/* ------------------------------------------------------------------- crash */

static void HgssOnException(ERRF_ExceptionInfo *info, CpuRegisters *regs)
{
    int i;

    if (sLog != NULL) {
        fprintf(sLog, "=== exception (type %d) pc %08lx lr %08lx cpsr %08lx ===\n",
                (int)info->type, (unsigned long)regs->pc, (unsigned long)regs->lr,
                (unsigned long)regs->cpsr);
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
        NitroPort_SoundRender(sAudioData + (size_t)slot * AUDIO_FRAMES * 2u,
                              AUDIO_FRAMES);
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
    sAudioThread = threadCreate(AudioThread, NULL, AUDIO_THREAD_STACK,
                                AUDIO_THREAD_PRIORITY, -2, false);
    if (sAudioThread == NULL) {
        sAudioRunning = false;
        NitroPort_Log("audio: can't start its thread; staying silent");
        return;
    }
    NitroPort_Log("audio: mixer %d Hz, DSP %d Hz (no resampling), %d blocks of %d frames",
                  AUDIO_RATE, (int)(NDSP_SAMPLE_RATE + 0.5),
                  AUDIO_BLOCKS, AUDIO_FRAMES);
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

/* One `key=value` from port.ini. Returns 1 and copies the trimmed value when
 * the key is present and not empty; 0 when the file or the key is missing.
 * The existing ScriptPath() keeps its own lenient reader; this one is for the
 * numeric settings that need an exact key. */
static int IniValue(const char *key, char *value, size_t size)
{
    FILE *file = fopen(INI_PATH, "rb");
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

/* `script=PATH` in port.ini, or the default script path. */
static const char *ScriptPath(void)
{
    static char path[256];
    FILE *file = fopen(INI_PATH, "rb");
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
static void AptHook(APT_HookType hook, void *param)
{
    (void)param;
    switch (hook) {
    case APTHOOK_ONSUSPEND:
    case APTHOOK_ONSLEEP:
        sPaused = true;
        NitroPort_Log("paused (APT hook %d)", (int)hook);
        break;
    case APTHOOK_ONRESTORE:
    case APTHOOK_ONWAKEUP:
        sPaused = false;
        NitroPort_Log("resumed (APT hook %d)", (int)hook);
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
    threadJoin(thread, 2000 * 1000000LL);
    threadFree(thread);
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

int main(int argc, char **argv)
{
    HeartgoldSettings settings;
    const char *error;
    aptHookCookie hook;
    Thread thread;

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
    LightLock_Init(&sLogLock);
    sLogLockReady = true;
    sLog = fopen(LOG_PATH, "w");

    NitroPort_Log(HEARTGOLD_APP_NAME " starting");
    {
        bool isNew3ds = false;

        APT_CheckNew3DS(&isNew3ds);
        sNew3ds = isNew3ds;
        NitroPort_Log("[perf] %s: %s", isNew3ds ? "New 3DS" : "Old 3DS",
                      isNew3ds ? "804 MHz + L2 requested"
                               : "268 MHz, the speedup is New-3DS only");
    }

    /* Before the game's thread: the cap is fixed for the whole run. */
    ReadFrameCap();

    /* NDSP only when the console has dspfirm.cdc; otherwise stay silent and
     * say so. Never let this stop the game. */
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

        /* Platinum's architecture: the GPU thread draws the 3D off the game's
         * thread, and the geometry thread keeps the vertices off it as well.
         * Either failing is logged and leaves today's 2D-only behaviour. */
        {
            s32 priority = 0x30;

            svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
            if (GpuStart(priority)) {
                GpuStartGeometry(priority);
            }
        }

        aptHook(&hook, AptHook, NULL);
        thread = threadCreate(GameThread, &settings, GAME_STACK_SIZE, 0x30, -2, false);
        if (thread == NULL) {
            NitroPort_Log("Can't start the game's thread.");
        } else {
            NitroPort_Log("running the game (thread stack %d KiB)", GAME_STACK_SIZE / 1024);
            WaitForGame(thread);
        }
        /* Each step is logged: a close that hangs shows where. */
        NitroPort_Log("[quit] closing");
        aptUnhook(&hook);
        NitroPort_Log("[quit] APT unhooked");
        /* Geometry first (it may still be finishing a scene for the GPU),
         * then the GPU. Both are no-ops if they never started. */
        GpuStopGeometry();
        NitroPort_Log("[quit] geometry stopped");
        GpuStop();
        NitroPort_Log("[quit] display stopped");
        NitroPort_Log("[gx3d] %lu 3D images, %lu scenes drawn",
                      (unsigned long)NitroPort_Gx3dImageCount(), sGpuScenes);
    }

    if (sAudio) {
        AudioStop();
        NitroPort_Log("[quit] sound stopped");
        NitroPort_Log("audio: %lu dropped frames of %lu",
                      (unsigned long)ndspGetDroppedFrames(),
                      (unsigned long)ndspGetFrameCount());
    }
    NitroPort_Log("stopping (frame %lu)", sFrame);
    NitroPort_CardFlush();
    if (sGpuStopFailed || sThreadsStuck) {
        /* A thread is still inside citro3d/libctru/NDSP: tearing the runtime
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
