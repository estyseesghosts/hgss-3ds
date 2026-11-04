/*
 * The game on a PC: the Phase 4 runner.
 *
 *   pokeheartgold-pc [heartgold3ds.pak] [manifest.json] [options]
 *   pokeheartgold-pc --check-overlays
 *
 * Runs the same game code as the 3DS program, with the same data pack. The
 * save is a raw 512 KB file, as emulators keep them: --save FILE, or
 * heartgold.sav (made if missing). Everything the port logs goes to stderr.
 *
 * Diagnostics and scripting (the counterpart of Platinum's runner):
 *   --frames N              stop after N frames
 *   --stop-at-missing-file  stop at the first file the pack lacks
 *   --stall-seconds N       report a stall after N seconds without a frame
 *                           (default 5; 0 disables)
 *   --screenshots DIR       write both screens as PNGs (upper above lower)
 *   --screenshot-every N    one every N frames (default 60)
 *   --press FRAME:BTNS,...  hold buttons for 6 frames; e.g. 8700:A,9000:START
 *   --touch FRAME:X:Y,...   touch the lower screen for 6 frames
 *   --wav FILE              record the virtual ARM7's audio to a WAV
 *   --dump-3d FRAME         write the newest finished 3D frame as a PNG
 *   --trace-pxi             log every PXI tag sent and answered
 *   --trace-irq             log every interrupt raised and waited on
 *   --events FILE           append machine-readable JSON events (for
 *                           tools/boot_smoke.py and tools/golden/run.py):
 *                           missing_file, crash, stall, frame_limit
 *
 * A crash prints the fault address, the program counter and a backtrace.
 * --check-overlays loads every overlay twice through the SDK and verifies
 * that each load restores its initial data and clears its bss; it needs no
 * data pack. Exit status is 0 only if every overlay passes.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <nitro.h>
#include <nitro_port.h>
#include <nitro_port_gx3d.h>

#include "heartgold.h"

/* port_snd.c has no public header; Platinum declares these the same way. */
void NitroPort_SoundRender(int16_t *stereo, size_t frames);
void NitroPort_SoundPumpReplies(void);

/* The trace wrappers live in pc_trace.c (host build only). */
void NitroPort_SetTrace(BOOL pxi, BOOL irq);

/* --------------------------------------------------------------- options */

static volatile int sFrame;
static int sFrameLimit = -1;
static int sStopAtMissingFile;
static int sStallSeconds = 5;
static const char *sScreenshotDir;
static long sScreenshotEvery = 60;
static long sDump3DFrame = -1;
static const char *sEventsPath;
static FILE *sEvents;
static int sInSignal;
static int sFrametime;
static uint64_t sLastFrameNs;
static int sHaveLastFrameNs;

#define MAX_PRESSES 4096
static struct {
    long frame;
    uint16_t buttons;
} sPresses[MAX_PRESSES];
static int sPressCount;

#define MAX_TOUCHES 4096
static struct {
    long frame;
    int x, y;
} sTouches[MAX_TOUCHES];
static int sTouchCount;

#define PRESS_FRAMES 6

static void Event(const char *fmt, ...)
{
    va_list args;

    if (sEvents == NULL) {
        return;
    }
    fputc('{', sEvents);
    va_start(args, fmt);
    vfprintf(sEvents, fmt, args);
    va_end(args);
    fputs("}\n", sEvents);
    fflush(sEvents);
}

/* A JSON string literal, for the event log. */
static void EventString(const char *key, const char *value)
{
    const unsigned char *p;

    if (sEvents == NULL) {
        return;
    }
    fprintf(sEvents, "\"%s\":\"", key);
    for (p = (const unsigned char *)value; p != NULL && *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', sEvents);
            fputc(*p, sEvents);
        } else if (*p < 0x20) {
            fprintf(sEvents, "\\u%04x", *p);
        } else {
            fputc(*p, sEvents);
        }
    }
    fputc('"', sEvents);
}

/* ------------------------------------------------------------ screenshots */

static uint32_t sCrcTable[256];

static uint32_t Crc(uint32_t crc, const unsigned char *p, size_t n)
{
    size_t i;

    if (sCrcTable[1] == 0) {
        uint32_t c, k;

        for (k = 0; k < 256; k++) {
            c = k;
            for (i = 0; i < 8; i++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            sCrcTable[k] = c;
        }
    }
    crc = ~crc;
    for (i = 0; i < n; i++) {
        crc = sCrcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

static void Put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void Chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len)
{
    unsigned char head[8], tail[4];
    uint32_t crc;

    Put32(head, len);
    memcpy(head + 4, type, 4);
    crc = Crc(0, head + 4, 4);
    crc = Crc(crc, data, len);
    Put32(tail, crc);
    fwrite(head, 1, 8, f);
    fwrite(data, 1, len, f);
    fwrite(tail, 1, 4, f);
}

/* A PNG of RGB rows (each a filter byte, 0, then 3 bytes a pixel; freed
 * here), uncompressed (deflate stored blocks), so no image library is
 * needed. */
static int WritePngRows(const char *path, unsigned char *rows, int w, int h)
{
    size_t raw = (size_t)h * (size_t)(1 + w * 3);
    size_t blocks = (raw + 65534) / 65535;
    size_t zlen = 2 + raw + blocks * 5 + 4;
    unsigned char *z = malloc(zlen);
    unsigned char ihdr[13];
    uint32_t a = 1, b = 0;
    size_t i, pos = 0, done = 0;
    FILE *f;

    if (rows == NULL || z == NULL) {
        free(rows);
        free(z);
        return -1;
    }
    z[pos++] = 0x78;
    z[pos++] = 0x01;
    while (done < raw) {
        size_t n = raw - done < 65535 ? raw - done : 65535;

        z[pos++] = done + n == raw ? 1 : 0;
        z[pos++] = (unsigned char)n;
        z[pos++] = (unsigned char)(n >> 8);
        z[pos++] = (unsigned char)~n;
        z[pos++] = (unsigned char)(~n >> 8);
        memcpy(z + pos, rows + done, n);
        pos += n;
        done += n;
    }
    for (i = 0; i < raw; i++) {
        a = (a + rows[i]) % 65521;
        b = (b + a) % 65521;
    }
    Put32(z + pos, (b << 16) | a);
    pos += 4;

    f = fopen(path, "wb");
    if (f == NULL) {
        free(rows);
        free(z);
        return -1;
    }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    Put32(ihdr, (uint32_t)w);
    Put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;    /* bits per channel */
    ihdr[9] = 2;    /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    Chunk(f, "IHDR", ihdr, 13);
    Chunk(f, "IDAT", z, (uint32_t)pos);
    Chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(rows);
    free(z);
    return 0;
}

/* The DS's 15-bit colour (bits 0-4 red, 5-9 green, 10-14 blue), expanded to
 * 8 bits per channel. */
static int WritePng(const char *path, const uint16_t *pixels, int w, int h)
{
    unsigned char *rows = malloc((size_t)h * (size_t)(1 + w * 3));
    int x, y;

    if (rows == NULL) {
        return -1;
    }
    for (y = 0; y < h; y++) {
        unsigned char *r = rows + (size_t)y * (size_t)(1 + w * 3);

        *r++ = 0;   /* filter: none */
        for (x = 0; x < w; x++) {
            uint16_t c = pixels[y * w + x];

            *r++ = (unsigned char)(((c & 31) << 3) | ((c & 31) >> 2));
            *r++ = (unsigned char)((((c >> 5) & 31) << 3) | (((c >> 5) & 31) >> 2));
            *r++ = (unsigned char)((((c >> 10) & 31) << 3) | (((c >> 10) & 31) >> 2));
        }
    }
    return WritePngRows(path, rows, w, h);
}

/* --render-3ds: draw every frame the way the 3DS shell does, so a local run
 * can reproduce what the console shows. The screenshots then come from these
 * buffers instead of a fresh render at capture time. */
static int sRenderLike3ds;
static uint16_t sBoth[256 * 384];

static void DrawLike3ds(void)
{
    static uint16_t top[256 * 192], bottom[256 * 192];
    int aOnTop = NitroPort_EngineAOnTop();

    NitroPort_Render2D(0, aOnTop ? top : bottom);
    (void)NitroPort_Render2DIfChanged(1, aOnTop ? bottom : top);
    memcpy(sBoth, top, sizeof top);
    memcpy(sBoth + 256 * 192, bottom, sizeof bottom);
}

static void Screenshot(void)
{
    static uint16_t both[256 * 384];
    char path[1024];

    if (sScreenshotDir == NULL) {
        return;
    }
    if (sRenderLike3ds) {
        memcpy(both, sBoth, sizeof both);
    } else {
        NitroPort_RenderScreens(both, both + 256 * 192);
    }
    snprintf(path, sizeof(path), "%s/frame_%07d.png", sScreenshotDir, sFrame);
    if (WritePng(path, both, 256, 384) != 0) {
        NitroPort_Log("[pc] can't write %s", path);
    }
}

/* ------------------------------------------------------------------- input */

static const struct {
    const char *name;
    uint16_t bit;
} sButtonNames[] = {
    { "A", PAD_BUTTON_A }, { "B", PAD_BUTTON_B },
    { "X", PAD_BUTTON_X }, { "Y", PAD_BUTTON_Y },
    { "L", PAD_BUTTON_L }, { "R", PAD_BUTTON_R },
    { "START", PAD_BUTTON_START }, { "SELECT", PAD_BUTTON_SELECT },
    { "UP", PAD_KEY_UP }, { "DOWN", PAD_KEY_DOWN },
    { "LEFT", PAD_KEY_LEFT }, { "RIGHT", PAD_KEY_RIGHT },
};

/* "A+UP" -> button bits; 0 if a name isn't known. */
static uint16_t ParseButtonList(const char *p, size_t len)
{
    uint16_t buttons = 0;
    const char *end = p + len;

    while (p < end) {
        size_t part = (size_t)(end - p);
        const char *plus = memchr(p, '+', part);
        size_t k;
        int found = 0;

        if (plus != NULL) {
            part = (size_t)(plus - p);
        }
        for (k = 0; k < sizeof(sButtonNames) / sizeof(sButtonNames[0]); k++) {
            if (strlen(sButtonNames[k].name) == part && strncmp(p, sButtonNames[k].name, part) == 0) {
                buttons |= sButtonNames[k].bit;
                found = 1;
                break;
            }
        }
        if (!found) {
            return 0;
        }
        p += part;
        if (p < end) {
            p++;   /* the '+' */
        }
    }
    return buttons;
}

/* --press FRAME:BUTTON[+BUTTON],...: hold for PRESS_FRAMES frames. */
static int ParsePresses(const char *spec)
{
    const char *p = spec;

    while (*p != '\0') {
        char *end;
        long frame = strtol(p, &end, 10);
        const char *list;
        size_t len;

        if (end == p || *end != ':' || sPressCount >= MAX_PRESSES) {
            return -1;
        }
        p = end + 1;
        len = strcspn(p, ",");
        list = p;
        if (len == 0) {
            return -1;
        }
        sPresses[sPressCount].buttons = ParseButtonList(list, len);
        if (sPresses[sPressCount].buttons == 0) {
            return -1;
        }
        sPresses[sPressCount].frame = frame;
        sPressCount++;
        p += len;
        if (*p == ',') {
            p++;
        }
    }
    return 0;
}

static uint16_t HeldButtons(long frame)
{
    uint16_t held = 0;
    int i;

    for (i = 0; i < sPressCount; i++) {
        if (frame >= sPresses[i].frame && frame < sPresses[i].frame + PRESS_FRAMES) {
            held |= sPresses[i].buttons;
        }
    }
    return held;
}

/* --touch FRAME:X:Y,...: touch the lower screen at (X, Y) for PRESS_FRAMES. */
static int ParseTouches(const char *spec)
{
    const char *p = spec;

    while (*p != '\0') {
        char *end;
        long frame = strtol(p, &end, 10);
        long x, y;

        if (end == p || *end != ':' || sTouchCount >= MAX_TOUCHES) {
            return -1;
        }
        p = end + 1;
        x = strtol(p, &end, 10);
        if (end == p || *end != ':' || x < 0 || x > 255) {
            return -1;
        }
        p = end + 1;
        y = strtol(p, &end, 10);
        if (end == p || y < 0 || y > 191) {
            return -1;
        }
        p = end;
        sTouches[sTouchCount].frame = frame;
        sTouches[sTouchCount].x = (int)x;
        sTouches[sTouchCount].y = (int)y;
        sTouchCount++;
        if (*p == ',') {
            p++;
        } else if (*p != '\0') {
            return -1;
        }
    }
    return 0;
}

static void ApplyTouch(long frame)
{
    int i;

    for (i = 0; i < sTouchCount; i++) {
        if (frame >= sTouches[i].frame && frame < sTouches[i].frame + PRESS_FRAMES) {
            NitroPort_SetTouch(TRUE, (u16)sTouches[i].x, (u16)sTouches[i].y);
            return;
        }
    }
    NitroPort_SetTouch(FALSE, 0, 0);
}

/* --------------------------------------------------------------- audio/WAV */

#define WAV_SAMPLES 512u
static const char *sWavPath;
static FILE *sWav;
static pthread_t sSoundThread;
static volatile int sSoundRunning;
static int sSoundThreadCreated;
static uint32_t sWavBytes;
static uint64_t sWavNextFrameNs;

/* When recording WAV, the game must not race through its 60 Hz scenes faster
 * than the audio clock. This only paces the game; the sound driver still
 * runs from the audio thread. */
static void PaceGameForWav(void)
{
    struct timespec now;
    uint64_t current, remaining;
    struct timespec sleepFor;

    if (sWav == NULL) {
        return;
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    current = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
    if (!sWavNextFrameNs || current > sWavNextFrameNs + 100000000ull) {
        sWavNextFrameNs = current;
    } else {
        sWavNextFrameNs += 16666667ull;
    }
    if (current >= sWavNextFrameNs) {
        return;
    }
    remaining = sWavNextFrameNs - current;
    sleepFor.tv_sec = (time_t)(remaining / 1000000000ull);
    sleepFor.tv_nsec = (long)(remaining % 1000000000ull);
    nanosleep(&sleepFor, NULL);
}

static void WavU16(FILE *f, uint16_t n)
{
    fputc(n & 255u, f);
    fputc((n >> 8) & 255u, f);
}

static void WavU32(FILE *f, uint32_t n)
{
    WavU16(f, (uint16_t)n);
    WavU16(f, (uint16_t)(n >> 16));
}

static void WavHeader(FILE *f, uint32_t bytes)
{
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    WavU32(f, bytes + 36u);
    fwrite("WAVEfmt ", 1, 8, f);
    WavU32(f, 16);
    WavU16(f, 1);      /* PCM */
    WavU16(f, 2);      /* stereo */
    WavU32(f, 32768);
    WavU32(f, 32768u * 4u);
    WavU16(f, 4);
    WavU16(f, 16);
    fwrite("data", 1, 4, f);
    WavU32(f, bytes);
}

static void *SoundThread(void *unused)
{
    int16_t samples[WAV_SAMPLES * 2u];
    struct timespec period = { 0, 15625000 }; /* 512 / 32768 s */
    (void)unused;

    while (__atomic_load_n(&sSoundRunning, __ATOMIC_ACQUIRE)) {
        NitroPort_SoundRender(samples, WAV_SAMPLES);
        if (sWav != NULL && sWavBytes <= UINT32_MAX - WAV_SAMPLES * 4u) {
            if (fwrite(samples, sizeof(samples), 1, sWav) == 1) {
                sWavBytes += sizeof(samples);
            }
        }
        nanosleep(&period, NULL);
    }
    return NULL;
}

static void StartSound(void)
{
    if (sWavPath != NULL) {
        sWav = fopen(sWavPath, "wb");
        if (sWav != NULL) {
            WavHeader(sWav, 0);
        } else {
            fprintf(stderr, "[audio] can't create WAV file: %s\n", sWavPath);
        }
    }
    __atomic_store_n(&sSoundRunning, 1, __ATOMIC_RELEASE);
    if (pthread_create(&sSoundThread, NULL, SoundThread, NULL) == 0) {
        sSoundThreadCreated = 1;
    } else {
        sSoundRunning = 0;
        fprintf(stderr, "[audio] can't start the virtual ARM7 audio clock\n");
    }
}

static void StopSound(void)
{
    __atomic_store_n(&sSoundRunning, 0, __ATOMIC_RELEASE);
    if (sSoundThreadCreated) {
        pthread_join(sSoundThread, NULL);
        sSoundThreadCreated = 0;
    }
    if (sWav != NULL) {
        WavHeader(sWav, sWavBytes);
        fflush(sWav);
        fclose(sWav);
        sWav = NULL;
    }
}

/* ----------------------------------------------------------- port hooks */

void NitroPort_Log(const char *fmt, ...)
{
    va_list args;

    fprintf(stderr, "[%6d] ", sFrame);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

static uint64_t NowNs(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

void NitroPort_OnFrame(void)
{
    uint64_t frameStart = NowNs();
    uint64_t totalUs = 0;
    uint64_t drawUs;

    NitroPort_SoundPumpReplies(); /* deliver ARM7 alarms on the ARM9's thread */
    PaceGameForWav();
    sFrame++;
    if (sRenderLike3ds) {
        DrawLike3ds();
    }

    if (sDump3DFrame == sFrame) {
        char path[128];

        snprintf(path, sizeof(path), "3d_frame_%07d.png", (long)sFrame);
        if (NitroPort_Gx3dDumpLatest(path)) {
            NitroPort_Log("[gx3d] wrote reference frame %d to %s", sFrame, path);
        } else {
            NitroPort_Log("[gx3d] cannot dump frame %d (no image or file error)", sFrame);
        }
    }
    if (sScreenshotDir != NULL && sScreenshotEvery > 0 && sFrame % sScreenshotEvery == 0) {
        Screenshot();
    }
    if (sFrameLimit >= 0 && sFrame >= sFrameLimit) {
        Screenshot();
        NitroPort_Log("[pc] stopping after %d frames, as asked", sFrame);
        Event("\"event\":\"frame_limit\",\"frame\":%d", sFrame);
        StopSound();
        fflush(NULL);
        _exit(0);
    }

    NitroPort_SetButtons(HeldButtons(sFrame), FALSE);
    ApplyTouch(sFrame);
    drawUs = (NowNs() - frameStart) / 1000;
    if (sHaveLastFrameNs) {
        totalUs = (frameStart - sLastFrameNs) / 1000;
    }
    sLastFrameNs = frameStart;
    sHaveLastFrameNs = 1;
    if (sFrametime && sFrame % 60 == 0) {
        uint64_t logicUs = totalUs > drawUs ? totalUs - drawUs : 0;
        NitroPort_Log("FRAMETIME frame=%d logic_us=%llu draw_us=%llu audio_us=0 total_us=%llu",
                      sFrame, (unsigned long long)logicUs,
                      (unsigned long long)drawUs, (unsigned long long)totalUs);
    }
}

void NitroPort_Terminate(void)
{
    NitroPort_Log("[pc] the game stopped itself (OS_Terminate)");
    Event("\"event\":\"terminate\",\"frame\":%d", sFrame);
    fflush(stderr);
    _exit(3);
}

void NitroPort_ResetSystem(u32 parameter)
{
    NitroPort_Log("[pc] the game asked to restart (OS_ResetSystem %lu); stopping",
                  (unsigned long)parameter);
    NitroPort_CardFlush();
    Event("\"event\":\"reset\",\"frame\":%d,\"parameter\":%lu", sFrame,
          (unsigned long)parameter);
    fflush(NULL);
    _exit(4);
}

void NitroPort_FileMissing(const char *path)
{
    NitroPort_Log("[nitro] the game asked for a file that isn't in the data pack: %s", path);
    if (sEvents != NULL) {
        fputc('{', sEvents);
        EventString("event", "missing_file");
        fprintf(sEvents, ",\"frame\":%d,", sFrame);
        EventString("path", path);
        fputs("}\n", sEvents);
        fflush(sEvents);
    }
    if (sStopAtMissingFile) {
        NitroPort_Log("[pc] stopping there, as asked");
        fflush(stderr);
        _exit(5);
    }
}

/* ------------------------------------------------------- crash reporting */

/* Print return addresses as function, file:line (addr2line, which also
 * names static functions and inlined calls); raw symbols if that fails. */
static void PrintFrames(void *const *frames, int count)
{
    char command[64 * 24 + 128];
    size_t used = (size_t)snprintf(command, sizeof(command),
                                   "addr2line -f -i -p -e /proc/%d/exe", (int)getpid());
    int f;

    for (f = 0; f < count && used < sizeof(command) - 24; f++) {
        /* A return address points after the call: look up the call. */
        used += (size_t)snprintf(command + used, sizeof(command) - used, " %#lx",
                                 (unsigned long)((const char *)frames[f] - 1));
    }
    snprintf(command + used, sizeof(command) - used, " 1>&2");
    fflush(stderr);
    if (system(command) != 0) {
        backtrace_symbols_fd(frames, count, 2);
    }
}

static void *CrashPc(void *context)
{
#if defined(__i386__)
    return (void *)(uintptr_t)((ucontext_t *)context)->uc_mcontext.gregs[REG_EIP];
#elif defined(__x86_64__)
    return (void *)(uintptr_t)((ucontext_t *)context)->uc_mcontext.gregs[REG_RIP];
#else
    (void)context;
    return NULL;
#endif
}

static void OnCrash(int sig, siginfo_t *info, void *context)
{
    void *frames[64];
    void *pc = CrashPc(context);
    int n = 0;

    sInSignal = 1;
    if (sEvents != NULL) {
        fprintf(sEvents, "{\"event\":\"crash\",\"frame\":%d,\"signal\":%d,\"pc\":\"%p\",\"address\":\"%p\"}\n",
                sFrame, sig, pc, info != NULL ? info->si_addr : NULL);
        fflush(sEvents);
    }
    fprintf(stderr, "[%6d] [pc] crashed (signal %d). Fault address %p, pc %p\n",
            sFrame, sig, info != NULL ? info->si_addr : NULL, pc);
#if defined(__i386__) || defined(__x86_64__)
    /* Walk the frame-pointer chain. backtrace() itself faults under
     * qemu-user inside a signal handler, so collect raw return addresses and
     * let the caller resolve them with addr2line. */
    {
        void **fp = (void **)__builtin_frame_address(0);

        while (fp != NULL && n < 64) {
            uintptr_t next = (uintptr_t)fp[0];
            void *ret = fp[1];

            if (ret == NULL) {
                break;
            }
            frames[n++] = ret;
            if (next <= (uintptr_t)fp || (next - (uintptr_t)fp) > 0x100000) {
                break;
            }
            fp = (void **)next;
        }
    }
#else
    n = backtrace(frames, 64);
#endif
    if (n > 0) {
        int f;

        fprintf(stderr, "[pc] backtrace (resolve with addr2line -e %s):\n",
                "/proc/<pid>/exe");
        for (f = 0; f < n; f++) {
            fprintf(stderr, "  #%d %p\n", f, frames[f]);
        }
    }
    fflush(stderr);
    _exit(128 + sig);
}

/* Integer division by zero. The 3DS has no divide instruction: GCC calls a
 * library routine, which gives 0 for x / 0 and doesn't stop the program
 * (nor did the DS's). x86 traps instead. Some of Nintendo's code divides by
 * zero harmlessly, so here it does what it does on the 3DS: quotient 0,
 * remainder the dividend. Each place is logged once. Any other arithmetic
 * fault is a crash. */
static void OnArithmeticFault(int sig, siginfo_t *info, void *context)
{
#if defined(__i386__) || defined(__x86_64__)
    ucontext_t *uc = context;
#if defined(__x86_64__)
    greg_t *ip = &uc->uc_mcontext.gregs[REG_RIP];
    greg_t *ax = &uc->uc_mcontext.gregs[REG_RAX];
    greg_t *dx = &uc->uc_mcontext.gregs[REG_RDX];
#else
    greg_t *ip = &uc->uc_mcontext.gregs[REG_EIP];
    greg_t *ax = &uc->uc_mcontext.gregs[REG_EAX];
    greg_t *dx = &uc->uc_mcontext.gregs[REG_EDX];
#endif
    const unsigned char *code = (const unsigned char *)*ip;
    const unsigned char *p = code;
    static const unsigned char *logged[32];
    static int loggedCount;
    int wide, mod, rm, i, seen = 0;

    (void)sig;
    if (info->si_code != FPE_INTDIV) {
        OnCrash(sig, info, context);
    }
    /* DIV/IDIV r/m: [66] [REX] F6|F7 ModRM [SIB] [disp] */
    if (*p == 0x66) {
        p++;
    }
#if defined(__x86_64__)
    if ((*p & 0xF0) == 0x40) {
        p++;   /* REX prefix */
    }
#endif
    if ((*p != 0xF6 && *p != 0xF7) || ((p[1] >> 3) & 7) < 6) {
        OnCrash(sig, info, context);
    }
    wide = *p == 0xF7;
    p++;
    mod = *p >> 6;
    rm = *p & 7;
    p++;
    if (mod != 3 && rm == 4) {               /* SIB byte */
        int base = *p & 7;

        p++;
        if (mod == 0 && base == 5) {
            p += 4;
        }
    } else if (mod == 0 && rm == 5) {        /* [disp32] / [rip+disp32] */
        p += 4;
    }
    p += mod == 1 ? 1 : mod == 2 ? 4 : 0;

    for (i = 0; i < loggedCount; i++) {
        seen |= logged[i] == code;
    }
    if (!seen && loggedCount < 32) {
        void *where[1] = { (void *)(code + 1) };

        logged[loggedCount++] = code;
        NitroPort_Log("[pc] integer division by zero; continuing with 0, as on the 3DS, at:");
        PrintFrames(where, 1);
    }
    if (wide) {
        *dx = *ax;                           /* remainder: the dividend */
        *ax = 0;                             /* quotient: 0 */
    } else {
        *ax = (*ax & ~(greg_t)0xFFFF) | ((*ax & 0xFF) << 8);   /* AH = AL % 0, AL = 0 */
    }
    *ip = (greg_t)(uintptr_t)p;
#else
    (void)info;
    (void)context;
    OnCrash(sig, info, context);
#endif
}

/* ------------------------------------------------- where a stuck game is */

#define MAX_DUMPED_THREADS 64
#define MAX_FRAMES 40

static struct {
    pid_t tid;
    int count;
    void *frames[MAX_FRAMES];
} sDumps[MAX_DUMPED_THREADS];
static volatile int sDumpCount;

static void OnDumpSignal(int sig)
{
    int i = __atomic_fetch_add(&sDumpCount, 1, __ATOMIC_SEQ_CST);

    (void)sig;
    if (i < MAX_DUMPED_THREADS) {
        sDumps[i].tid = (pid_t)syscall(SYS_gettid);
        sDumps[i].count = backtrace(sDumps[i].frames, MAX_FRAMES);
    }
}

static void DumpThreads(void)
{
    pid_t self = (pid_t)syscall(SYS_gettid);
    DIR *dir = opendir("/proc/self/task");
    struct dirent *entry;
    int i, n;

    signal(SIGUSR1, OnDumpSignal);
    while (dir != NULL && (entry = readdir(dir)) != NULL) {
        pid_t tid = (pid_t)atoi(entry->d_name);

        if (tid > 0 && tid != self) {
            syscall(SYS_tgkill, getpid(), tid, SIGUSR1);
        }
    }
    if (dir != NULL) {
        closedir(dir);
    }
    usleep(300 * 1000);

    n = sDumpCount < MAX_DUMPED_THREADS ? sDumpCount : MAX_DUMPED_THREADS;
    for (i = 0; i < n; i++) {
        fprintf(stderr, "---- thread %d:\n", (int)sDumps[i].tid);
        /* Frames 0 and 1: the signal handler and the signal trampoline. */
        PrintFrames(sDumps[i].frames + 2, sDumps[i].count - 2);
    }
}

/* If frames stop coming, say where the game is and stop. */
static void *Watch(void *arg)
{
    long last = -1;
    int still = 0;

    (void)arg;
    for (;;) {
        long now;

        sleep(1);
        if (sInSignal) {
            continue;
        }
        now = (long)sFrame;
        still = now == last ? still + 1 : 0;
        last = now;
        if (sStallSeconds > 0 && still >= sStallSeconds) {
            NitroPort_Log("[pc] no frame for %d seconds: the game is stuck. Where each thread "
                          "is (the one not waiting in port_thread.c is the stuck one):",
                          sStallSeconds);
            Event("\"event\":\"stall\",\"frame\":%d,\"seconds\":%d", (int)now, sStallSeconds);
            DumpThreads();
            Screenshot();
            NitroPort_Log("[pc] stopping (stuck)");
            fflush(stderr);
            _exit(6);
        }
    }
    return NULL;
}

/* -------------------------------------------------------------------- main */

static void Usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [heartgold3ds.pak] [manifest.json] [--save FILE] [--frames N]\n"
            "       [--stop-at-missing-file] [--stall-seconds N] [--screenshots DIR]\n"
            "       [--screenshot-every N] [--press FRAME:BUTTONS,...]\n"
            "       [--touch FRAME:X:Y,...] [--wav FILE] [--dump-3d FRAME]\n"
            "       [--trace-pxi] [--trace-irq] [--frametime] [--events FILE]\n"
            "       %s --check-overlays\n",
            argv0, argv0);
}

int main(int argc, char **argv)
{
    HeartgoldSettings settings;
    const char *pak = NULL;
    const char *manifest = NULL;
    const char *save = NULL;
    int tracePxi = 0, traceIrq = 0;
    int i;

    if (argc == 2 && strcmp(argv[1], "--check-overlays") == 0) {
        return Heartgold_CheckOverlays() == 0 ? 0 : 1;
    }

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--save") == 0 && i + 1 < argc) {
            save = argv[++i];
        } else if (strcmp(arg, "--frames") == 0 && i + 1 < argc) {
            sFrameLimit = (int)strtol(argv[++i], NULL, 0);
        } else if (strcmp(arg, "--stop-at-missing-file") == 0) {
            sStopAtMissingFile = 1;
        } else if (strcmp(arg, "--stall-seconds") == 0 && i + 1 < argc) {
            sStallSeconds = (int)strtol(argv[++i], NULL, 0);
        } else if (strcmp(arg, "--screenshots") == 0 && i + 1 < argc) {
            sScreenshotDir = argv[++i];
        } else if (strcmp(arg, "--screenshot-every") == 0 && i + 1 < argc) {
            sScreenshotEvery = strtol(argv[++i], NULL, 0);
        } else if (strcmp(arg, "--press") == 0 && i + 1 < argc) {
            if (ParsePresses(argv[++i]) != 0) {
                fprintf(stderr, "--press: expected FRAME:BUTTON[+BUTTON],... e.g. 8700:A,9000:START\n");
                return 2;
            }
        } else if (strcmp(arg, "--touch") == 0 && i + 1 < argc) {
            if (ParseTouches(argv[++i]) != 0) {
                fprintf(stderr, "--touch: expected FRAME:X:Y,... (lower screen, X 0-255, Y 0-191)\n");
                return 2;
            }
        } else if (strcmp(arg, "--wav") == 0 && i + 1 < argc) {
            sWavPath = argv[++i];
        } else if (strcmp(arg, "--dump-3d") == 0 && i + 1 < argc) {
            char *end;
            long requested;

            errno = 0;
            requested = strtol(argv[++i], &end, 10);
            if (errno == ERANGE || *end != '\0' || requested < 1) {
                fprintf(stderr, "--dump-3d: expected a positive frame number\n");
                return 2;
            }
            sDump3DFrame = requested;
        } else if (strcmp(arg, "--trace-pxi") == 0) {
            tracePxi = 1;
        } else if (strcmp(arg, "--trace-irq") == 0) {
            traceIrq = 1;
        } else if (strcmp(arg, "--frametime") == 0) {
            sFrametime = 1;
        } else if (strcmp(arg, "--render-3ds") == 0) {
            sRenderLike3ds = 1;
        } else if (strcmp(arg, "--events") == 0 && i + 1 < argc) {
            sEventsPath = argv[++i];
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "pokeheartgold-pc: unknown option %s\n", arg);
            Usage(argv[0]);
            return 2;
        } else if (pak == NULL) {
            pak = arg;
        } else if (manifest == NULL) {
            manifest = arg;
        } else {
            fprintf(stderr, "pokeheartgold-pc: unexpected argument %s\n", arg);
            return 2;
        }
    }

    if (pak == NULL && manifest != NULL) {
        fprintf(stderr, "pokeheartgold-pc: give the pak and manifest together\n");
        return 2;
    }
    if (sEventsPath != NULL) {
        sEvents = fopen(sEventsPath, "a");
        if (sEvents == NULL) {
            fprintf(stderr, "pokeheartgold-pc: can't write %s: %s\n", sEventsPath, strerror(errno));
            return 2;
        }
    }
    if (sScreenshotDir != NULL && mkdir(sScreenshotDir, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "pokeheartgold-pc: can't make %s: %s\n", sScreenshotDir, strerror(errno));
        return 2;
    }

    {
        struct sigaction crash;

        memset(&crash, 0, sizeof(crash));
        crash.sa_sigaction = OnCrash;
        crash.sa_flags = SA_SIGINFO;
        sigemptyset(&crash.sa_mask);
        sigaction(SIGSEGV, &crash, NULL);
        sigaction(SIGBUS, &crash, NULL);
        sigaction(SIGILL, &crash, NULL);
        sigaction(SIGABRT, &crash, NULL);
    }
    {
        struct sigaction fault;

        memset(&fault, 0, sizeof(fault));
        fault.sa_sigaction = OnArithmeticFault;
        fault.sa_flags = SA_SIGINFO;
        sigaction(SIGFPE, &fault, NULL);
    }
    signal(SIGUSR1, OnDumpSignal);
    /* Loads libgcc's unwind tables now, not inside a signal handler. */
    {
        void *warm[2];

        (void)backtrace(warm, 2);
    }

    if (sStallSeconds > 0) {
        pthread_t watch;

        if (pthread_create(&watch, NULL, Watch, NULL) != 0) {
            fprintf(stderr, "pokeheartgold-pc: can't start the stall watch\n");
        } else {
            pthread_detach(watch);
        }
    }

    NitroPort_SetTrace(tracePxi ? TRUE : FALSE, traceIrq ? TRUE : FALSE);

    Heartgold_DefaultSettings(&settings);
    settings.savePath = save != NULL ? save : HEARTGOLD_SAVE_NAME;

    if (pak != NULL) {
        const char *error = Heartgold_OpenPak(pak, manifest);

        if (error != NULL) {
            fprintf(stderr, "pokeheartgold-pc: %s\n", error);
            Event("\"event\":\"pak_error\"");
            return 2;
        }
    }

    StartSound();
    Heartgold_Run(&settings);
    StopSound();
    return 0;
}
