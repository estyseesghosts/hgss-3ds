/*
 * sdat_render: a measurement tool, not part of the game. It plays one
 * sequence of a retail SDAT through the port's audio stack and writes what
 * came out: a 16-bit stereo WAV and (built with NITRO_SOUND_STATS) a JSON of
 * mixer diagnostics.
 *
 *   sdat_render <sdat> --list
 *   sdat_render <sdat> --seq <id|name> [--seconds N] [--player-volume V]
 *               [--master V] --wav out.wav [--stats out.json]
 *
 * Everything from NNS_SndInit down is the production code: the upstream
 * NitroSystem sound archive and player, HGSS's ARM7 sound driver (sub/lib/src)
 * through port_snd.c, and the port's mixer (port_sound_chip.c). The ARM9 calls
 * are the ones the game makes (sound_playback.c's Sound_StartSeq ->
 * NNS_SndArcPlayerStartSeqEx with the sequence's own player, bank and
 * priority; NNS_SndMain once per 60 Hz frame, from the game's sound update).
 *
 * Two deliberate differences from the running game, both about where bytes
 * come from, not about the audio:
 *   - the archive is NNS_SndArcInitOnMemory over the file's bytes, not
 *     NNS_SndArcInit over the FS. The game's loaders then copy the sequence
 *     and bank into the sound heap; here they keep the file's own bytes.
 *     Checked at start: no wave archive is single-load (those read the
 *     file again, through the FS), so the FS is never reached;
 *   - the game's per-frame sound state (fades, cry and chatot timers, the
 *     GB-sounds mode, heap levels) is not run: only the calls above are.
 *
 * Built by 3ds_port/nitro (`make sdat_render`, or tools/hgss_nitro.py
 * sdat-render). Host32 only: the driver stores host pointers in its 32-bit
 * SAD registers, so a 64-bit build would truncate them.
 */
#include <nitro.h>
#include <nitro_port.h>
#include <nitro_port_sound_chip.h>
#include <nnsys/snd_arc_internal.h>
#include <nnsys/snd/main.h>
#include <nnsys/snd/heap.h>
#include <nnsys/snd/player.h>
#include <nnsys/snd/sndarc_player.h>

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern void NitroPort_SoundPumpReplies(void);
extern void NitroPort_SoundRender(int16_t *stereo, size_t frames);

/* The game's sound heap (include/sound.h: SND_HEAP_SIZE). */
#define HEAP_BYTES 0xBEAE0u
#define GAME_FPS 60u
#define WINDOW_FRAMES NITRO_SOUND_RATE
#define FULL_SCALE 32767.0
#define CLICK_JUMP 4096

#define EXIT_USAGE 1
#define EXIT_INPUT 2
#define EXIT_UNSUPPORTED 3
#define EXIT_START 4

enum { FMT_PCM8, FMT_PCM16, FMT_ADPCM, FMT_PSG, FMT_NOISE, FMT_OTHER, FMT_COUNT };
static const char *const kFormatName[FMT_COUNT] = {
    "PCM8", "PCM16", "ADPCM", "PSG", "noise", "other"
};

#ifdef NITRO_SOUND_STATS
enum { CAUSE_START, CAUSE_STOP, CAUSE_STOP_SILENT, CAUSE_END, CAUSE_LOOP,
       CAUSE_OTHER, CAUSE_COUNT };
static const char *const kCauseName[CAUSE_COUNT] = {
    "note_start", "stop_kill_volume_gt0", "stop_kill_volume0", "natural_end",
    "loop_wrap", "unattributed"
};

/* One window of a run (a second) or the whole run. */
typedef struct Level {
    uint64_t frames;
    int32_t preMax;          /* max |pre-shift| over L and R */
    double preSquares;       /* sum of pre-shift L^2 + R^2 */
    int32_t postMax;         /* max |post-shift|, before saturate() */
    double postSquares;
    uint64_t over32767;      /* frames with a pre-shift value past 32767 */
    uint64_t over65534;      /* frames with a pre-shift value past 65534 */
    uint64_t satSamples;     /* L/R samples that saturate() clips */
    uint32_t activeMax;      /* channels that made a sample in the frame */
    uint64_t activeSum;
    uint32_t audibleMax;     /* the same, with volume above 0 */
    uint64_t audibleSum;
    int32_t outMax;          /* final int16 output, max |sample| */
    double outSquares;
} Level;

typedef struct Diag {
    Level overall;
    Level *windows;
    size_t windowCount;
    uint64_t clicks[CAUSE_COUNT][FMT_COUNT];
    uint64_t starts[FMT_COUNT];
    uint64_t activeFrames[FMT_COUNT];
    uint64_t chanFrames[NITRO_SOUND_CHANNEL_COUNT][FMT_COUNT];
    uint64_t chanStarts[NITRO_SOUND_CHANNEL_COUNT];
    uint64_t lastActiveFrame;
    uint64_t events[4];              /* frame-level event counts: start, loop, end, kill */
    /* Notes (a channel's run from its start event): how each one stopped. */
    uint64_t runs, killed, ended, restartedOpen, aliveAtEnd;
    uint64_t longestRun, aliveLongest;       /* frames */
    int runOpen[NITRO_SOUND_CHANNEL_COUNT];
    uint64_t runStart[NITRO_SOUND_CHANNEL_COUNT];
    uint64_t volHist[8];             /* active channel-frames by ARM7 volume / 16 */
    uint64_t shiftHist[4];           /* active channel-frames by shift: 0, 1, 2, 4 */
    int anyActive;
    /* the previous frame, for the click detector */
    int32_t prevTap[NITRO_SOUND_CHANNEL_COUNT];
    uint8_t prevFormat[NITRO_SOUND_CHANNEL_COUNT];
    uint8_t prevVolume[NITRO_SOUND_CHANNEL_COUNT];
    uint8_t prevEvent[NITRO_SOUND_CHANNEL_COUNT];
} Diag;
#endif

typedef struct Options {
    const char *sdatPath;
    int list;
    const char *seqArg;
    unsigned seconds;
    int hasPlayerVolume;
    unsigned playerVolume;
    int hasFader;
    unsigned fader;
    int hasMaster;
    unsigned master;
    const char *wavPath;
    const char *statsPath;
} Options;

static union { uint64_t align; uint8_t bytes[HEAP_BYTES]; } sHeap;
static NNSSndArc sArc;
static NNSSndHandle sHandle;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static int fail(int code, const char *message, const char *detail)
{
    fprintf(stderr, "sdat_render: %s%s%s\n", message, detail ? ": " : "",
            detail ? detail : "");
    return code;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: sdat_render <sdat> --list\n"
            "       sdat_render <sdat> --seq <id|name> [--seconds N] "
            "[--player-volume V] [--master V] --wav out.wav [--stats out.json]\n");
}

/* The SDAT's symbol block: a table (count, then offsets) of sequence names,
 * offsets from the start of the block; 0 for none. */
static const char *seq_name(int seqNo)
{
    const uint8_t *block = (const uint8_t *)sArc.symbol;
    const SndArcOffsetTable *table;
    uint32_t offset;

    if (!block || !sArc.symbol->seqOffset || seqNo < 0) return NULL;
    table = (const SndArcOffsetTable *)(block + sArc.symbol->seqOffset);
    if ((uint32_t)seqNo >= table->count) return NULL;
    offset = table->offsets[seqNo];
    return offset ? (const char *)(block + offset) : NULL;
}

static uint32_t seq_count(void)
{
    const uint8_t *block = (const uint8_t *)sArc.symbol;
    if (!block || !sArc.symbol->seqOffset) return 0;
    return ((const SndArcOffsetTable *)(block + sArc.symbol->seqOffset))->count;
}

static int seq_by_name(const char *name)
{
    uint32_t i, count = seq_count();
    for (i = 0; i < count; ++i) {
        const char *s = seq_name((int)i);
        if (s && strcmp(s, name) == 0) return (int)i;
    }
    return -1;
}

/* True when any wave archive the bank loads one wave at a time (those read
 * their waves from the file, through the FS, as they play). */
static int any_single_load(void)
{
    const NNSSndArcInfo *info = sArc.info;
    const SndArcOffsetTable *table;
    uint32_t i;

    if (!info || !info->waveArcOffset) return 0;
    table = (const SndArcOffsetTable *)((const uint8_t *)info + info->waveArcOffset);
    for (i = 0; i < table->count; ++i) {
        const SndArcWaveArcInfo *wave;
        if (!table->offsets[i]) continue;
        wave = (const SndArcWaveArcInfo *)((const uint8_t *)info + table->offsets[i]);
        if (wave->flags & SND_ARC_WAVEARC_SINGLE_LOAD) return 1;
    }
    return 0;
}

static int load_sdat(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long length;
    uint8_t *buffer;

    if (!f) return fail(EXIT_INPUT, "cannot open", path);
    if (fseek(f, 0, SEEK_END) != 0 || (length = ftell(f)) <= 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return fail(EXIT_INPUT, "cannot size", path);
    }
    buffer = malloc((size_t)length);
    if (!buffer || fread(buffer, 1, (size_t)length, f) != (size_t)length) {
        fclose(f);
        free(buffer);
        return fail(EXIT_INPUT, "cannot read", path);
    }
    fclose(f);
    if (length < 16 || memcmp(buffer, "SDAT", 4) != 0) {
        free(buffer);
        return fail(EXIT_INPUT, "not an SDAT file", path);
    }
    *data = buffer;
    *size = (size_t)length;
    return 0;
}

static void boot_sound(void)
{
    NitroPortFirmware firmware;

    memset(&firmware, 0, sizeof(firmware));
    NitroPort_BootFirmware(&firmware);
    NitroPort_InitArm7Services();
    OS_Init();
    (void)OS_EnableIrq();
    (void)OS_EnableInterrupts();
    NNS_SndInit();
}

static void print_list(void)
{
    uint32_t i, count = seq_count();
    printf("# id name volume player bank channelPrio playerPrio\n");
    for (i = 0; i < count; ++i) {
        const NNSSndSeqParam *param = NNS_SndArcGetSeqParam((int)i);
        const char *name = seq_name((int)i);
        if (!param) continue;
        printf("%u %s %u %u %u %u %u\n", i, name ? name : "-", param->volume,
               param->playerNo, param->bankNo, param->channelPrio,
               param->playerPrio);
    }
}

#ifdef NITRO_SOUND_STATS
static int format_of(unsigned channel, unsigned format)
{
    switch (format) {
    case 0: return FMT_PCM8;
    case 1: return FMT_PCM16;
    case 2: return FMT_ADPCM;
    default:
        if (channel >= 14u) return FMT_NOISE;
        if (channel >= 8u) return FMT_PSG;
        return FMT_OTHER;   /* format 3 on channels 0-7 makes no sound */
    }
}

static int32_t iabs32(int32_t v)
{
    return v < 0 ? -v : v;
}

static void level_frame(Level *lv, int32_t preL, int32_t preR, int32_t postL,
                        int32_t postR, uint32_t active, uint32_t audible)
{
    int32_t preMax = iabs32(preL) > iabs32(preR) ? iabs32(preL) : iabs32(preR);
    int32_t postMax = iabs32(postL) > iabs32(postR) ? iabs32(postL) : iabs32(postR);

    lv->frames++;
    if (preMax > lv->preMax) lv->preMax = preMax;
    lv->preSquares += (double)preL * preL + (double)preR * preR;
    if (preMax > 32767) lv->over32767++;
    if (preMax > 65534) lv->over65534++;
    if (postMax > lv->postMax) lv->postMax = postMax;
    lv->postSquares += (double)postL * postL + (double)postR * postR;
    if (postL > 32767 || postL < -32768) lv->satSamples++;
    if (postR > 32767 || postR < -32768) lv->satSamples++;
    if (active > lv->activeMax) lv->activeMax = active;
    lv->activeSum += active;
    if (audible > lv->audibleMax) lv->audibleMax = audible;
    lv->audibleSum += audible;
}

static void on_frame(void *arg, const NitroSoundFrame *f)
{
    Diag *d = arg;
    size_t w = f->index / WINDOW_FRAMES;
    uint32_t active = 0, audible = 0;
    unsigned ch;

    for (ch = 0; ch < NITRO_SOUND_CHANNEL_COUNT; ++ch) {
        int on = f->tap[ch] != NITRO_SOUND_TAP_OFF;
        int prevOn = d->prevTap[ch] != NITRO_SOUND_TAP_OFF;
        int32_t cur = on ? f->tap[ch] : 0;
        int32_t prev = prevOn ? d->prevTap[ch] : 0;
        int32_t jump = iabs32(cur - prev);
        int fmt = format_of(ch, on ? f->format[ch] : d->prevFormat[ch]);

        if (on) {
            active++;
            if (f->volume[ch] > 0) audible++;
            d->activeFrames[fmt]++;
            d->chanFrames[ch][fmt]++;
            d->anyActive = 1;
            d->lastActiveFrame = f->index;
            d->volHist[f->volume[ch] / 16u]++;
            d->shiftHist[f->shift[ch] == 4u ? 3u : f->shift[ch] & 3u]++;
        }
        /* Note runs: a start opens one; a kill (driver stop while playing) or the
         * end of the sound closes it. A start on an open run is a restart. */
        if ((f->event[ch] & (NITRO_SOUND_EV_KILL | NITRO_SOUND_EV_END)) && d->runOpen[ch]) {
            uint64_t length = f->index - d->runStart[ch] + 1u;
            if (f->event[ch] & NITRO_SOUND_EV_KILL) d->killed++;
            else d->ended++;
            if (length > d->longestRun) d->longestRun = length;
            d->runOpen[ch] = 0;
        }
        if (f->event[ch] & NITRO_SOUND_EV_START) {
            if (d->runOpen[ch]) d->restartedOpen++;
            d->runOpen[ch] = 1;
            d->runStart[ch] = f->index;
            d->runs++;
        }
        if (f->event[ch] & NITRO_SOUND_EV_LOOP) d->events[1]++;
        if (f->event[ch] & NITRO_SOUND_EV_END) d->events[2]++;
        if (f->event[ch] & NITRO_SOUND_EV_KILL) d->events[3]++;
        if (f->event[ch] & NITRO_SOUND_EV_START) {
            d->events[0]++;
            d->starts[fmt]++;
            d->chanStarts[ch]++;
        }
        /* A jump past CLICK_JUMP between two frames, by the event that
         * caused it: a note starting, the channel stopped while playing
         * (volume above 0), the sound ending (as the mixer ends it), or a
         * loop wrapping (the event on the frame before, which is what the
         * wrap changes). Anything else is the waveform itself (PSG and noise
         * edges, or a sample's own steep edge). */
        if (jump > CLICK_JUMP) {
            int cause;
            if (f->event[ch] & NITRO_SOUND_EV_START) {
                cause = CAUSE_START;
            } else if (prevOn && !on) {
                if (f->event[ch] & NITRO_SOUND_EV_KILL)
                    cause = d->prevVolume[ch] > 0 ? CAUSE_STOP : CAUSE_STOP_SILENT;
                else if (d->prevEvent[ch] & NITRO_SOUND_EV_END)
                    cause = CAUSE_END;
                else
                    cause = CAUSE_OTHER;
            } else if (prevOn && on) {
                cause = (d->prevEvent[ch] & NITRO_SOUND_EV_LOOP) ? CAUSE_LOOP
                                                                 : CAUSE_OTHER;
            } else {
                cause = CAUSE_OTHER;
            }
            d->clicks[cause][fmt]++;
        }
        d->prevTap[ch] = on ? f->tap[ch] : NITRO_SOUND_TAP_OFF;
        d->prevFormat[ch] = (uint8_t)f->format[ch];
        d->prevVolume[ch] = (uint8_t)f->volume[ch];
        d->prevEvent[ch] = f->event[ch];
    }
    level_frame(&d->overall, f->preL, f->preR, f->postL, f->postR, active, audible);
    if (w < d->windowCount)
        level_frame(&d->windows[w], f->preL, f->preR, f->postL, f->postR, active, audible);
}

static void level_output(Level *lv, const int16_t *pcm, uint64_t frames)
{
    uint64_t i;
    for (i = 0; i < frames; ++i) {
        int32_t l = iabs32(pcm[2 * i]), r = iabs32(pcm[2 * i + 1]);
        if (l > lv->outMax) lv->outMax = l;
        if (r > lv->outMax) lv->outMax = r;
        lv->outSquares += (double)pcm[2 * i] * pcm[2 * i] +
                          (double)pcm[2 * i + 1] * pcm[2 * i + 1];
    }
}

/* Runs still open at the end of the render are alive (not stopped). */
static void finish_runs(Diag *d, uint64_t frames)
{
    unsigned ch;
    for (ch = 0; ch < NITRO_SOUND_CHANNEL_COUNT; ++ch) {
        if (!d->runOpen[ch]) continue;
        uint64_t length = frames - d->runStart[ch];
        d->aliveAtEnd++;
        if (length > d->aliveLongest) d->aliveLongest = length;
        if (length > d->longestRun) d->longestRun = length;
    }
}

static double rel(double v)
{
    return v / FULL_SCALE;
}

static double rms_of(double squares, uint64_t frames)
{
    return frames ? sqrt(squares / (2.0 * (double)frames)) : 0.0;
}

static void json_level(FILE *out, const Level *lv, const char *indent)
{
    double n = (double)lv->frames;
    fprintf(out,
            "%s\"preMaxRel\": %.4f, \"preRmsRel\": %.5f, \"over32767\": %" PRIu64
            ", \"over65534\": %" PRIu64 ",\n"
            "%s\"postMaxRel\": %.4f, \"postRmsRel\": %.5f, \"satSamples\": %" PRIu64 ",\n"
            "%s\"outMaxRel\": %.4f, \"outRmsRel\": %.5f,\n"
            "%s\"activeMax\": %u, \"activeMean\": %.3f, \"audibleMax\": %u, \"audibleMean\": %.3f",
            indent, rel(lv->preMax), rel(sqrt(lv->preSquares / (2.0 * (n ? n : 1)))),
            lv->over32767, lv->over65534,
            indent, rel(lv->postMax), rel(sqrt(lv->postSquares / (2.0 * (n ? n : 1)))),
            lv->satSamples,
            indent, rel(lv->outMax), rel(sqrt(lv->outSquares / (2.0 * (n ? n : 1)))),
            indent, lv->activeMax, n ? (double)lv->activeSum / n : 0.0,
            lv->audibleMax, n ? (double)lv->audibleSum / n : 0.0);
}

static void json_formats(FILE *out, const char *name, const uint64_t values[FMT_COUNT])
{
    int f;
    fprintf(out, "\"%s\": {", name);
    for (f = 0; f < FMT_COUNT; ++f)
        fprintf(out, "%s\"%s\": %" PRIu64, f ? ", " : "", kFormatName[f], values[f]);
    fprintf(out, "}");
}
#endif

static int write_wav(const char *path, const int16_t *pcm, size_t frames)
{
    FILE *f = fopen(path, "wb");
    uint8_t header[44];
    uint32_t dataBytes = (uint32_t)(frames * 4u);
    size_t i;

    if (!f) return fail(EXIT_INPUT, "cannot create", path);
    memcpy(header, "RIFF", 4);
    header[4] = (uint8_t)(36u + dataBytes); header[5] = (uint8_t)((36u + dataBytes) >> 8);
    header[6] = (uint8_t)((36u + dataBytes) >> 16); header[7] = (uint8_t)((36u + dataBytes) >> 24);
    memcpy(header + 8, "WAVEfmt ", 8);
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
    header[20] = 1; header[21] = 0;                 /* PCM */
    header[22] = 2; header[23] = 0;                 /* stereo */
    header[24] = 0; header[25] = 0x80; header[26] = 0; header[27] = 0;   /* 32768 Hz */
    header[28] = 0; header[29] = 0x00; header[30] = 0x02; header[31] = 0; /* 131072 B/s */
    header[32] = 4; header[33] = 0;                 /* block align */
    header[34] = 16; header[35] = 0;                /* bits */
    memcpy(header + 36, "data", 4);
    header[40] = (uint8_t)dataBytes; header[41] = (uint8_t)(dataBytes >> 8);
    header[42] = (uint8_t)(dataBytes >> 16); header[43] = (uint8_t)(dataBytes >> 24);
    if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return fail(EXIT_INPUT, "cannot write", path);
    }
    for (i = 0; i < frames * 2u; ++i) {
        uint8_t bytes[2] = { (uint8_t)((uint16_t)pcm[i]), (uint8_t)((uint16_t)pcm[i] >> 8) };
        if (fwrite(bytes, 1, 2, f) != 2) {
            fclose(f);
            return fail(EXIT_INPUT, "cannot write", path);
        }
    }
    return fclose(f) == 0 ? 0 : fail(EXIT_INPUT, "cannot write", path);
}

/* NitroSystem's volume->decibel table (hgss/compat/src/nns_volume_table.c, linked
 * as the game links it). Read with a runtime index, as snd_player.c does. */
extern const int16_t _0210D750[];

/* Debug (read-only, --fader): the fader's table lookup, the sequence player's
 * decibel sum as NNSi_SndPlayerMain builds it (CombineDecibels: fade + (player +
 * (init + ext)), clamped to 16 bits, then sent with SND_SetPlayerVolume), and the
 * ARM7 channel volumes from the mixer's registers (ctrl bits 0-6). */
static void print_fader_state(int fader)
{
    const volatile int16_t *table = _0210D750;
    NNSSndSeqPlayer *player = SND_HANDLE_PLAYER(&sHandle);
    unsigned ch;

    printf("fader debug: requested %d, table index %d, _0210D750[index] = %d\n",
           fader, fader, (int)table[fader]);
    printf("fader debug: _0210D750[127] = %d, [128] = %d, [129] = %d (the linked table: 256 DS-exact entries; a shorter table reads past its end)\n",
           (int)table[127], (int)table[128], (int)table[129]);
    if (!player) {
        printf("fader debug: no sequence player on the handle\n");
        return;
    }
    {
        int playerDb = table[player->playerInfo->volume];
        int initDb = table[player->initVolume];
        int extDb = table[player->extVolume];
        int faderNow = NNSi_SndFaderGet(&player->fader) >> 8;
        int combined = table[faderNow] + (playerDb + (initDb + extDb));
        if (combined < -32768) combined = -32768;
        if (combined > 32767) combined = 32767;
        printf("fader debug: fader now %d; initVolume %u extVolume %u playerVolume %u\n",
               faderNow, player->initVolume, player->extVolume, player->playerInfo->volume);
        printf("fader debug: CombineDecibels (mirrored) = %d; last sent to ARM7 = %d\n",
               combined, (int)player->volume);
    }
    for (ch = 0; ch < NITRO_SOUND_CHANNEL_COUNT; ++ch) {
        unsigned base = 0x400u + ch * 16u;
        uint32_t ctrl = (uint32_t)NitroPort_SoundRegisters[base] |
                        ((uint32_t)NitroPort_SoundRegisters[base + 1] << 8) |
                        ((uint32_t)NitroPort_SoundRegisters[base + 2] << 16) |
                        ((uint32_t)NitroPort_SoundRegisters[base + 3] << 24);
        if (ctrl & 0x80000000u)
            printf("fader debug: ARM7 channel %2u volume %3u pan %3u\n", ch,
                   ctrl & 127u, (ctrl >> 16) & 127u);
    }
}

static int parse_args(int argc, char **argv, Options *o)
{
    int i;
    memset(o, 0, sizeof(*o));
    o->seconds = 60;
    if (argc < 2) return EXIT_USAGE;
    o->sdatPath = argv[1];
    for (i = 2; i < argc; ++i) {
        const char *arg = argv[i];
        int more = i + 1 < argc;
        if (strcmp(arg, "--list") == 0) {
            o->list = 1;
        } else if (strcmp(arg, "--seq") == 0 && more) {
            o->seqArg = argv[++i];
        } else if (strcmp(arg, "--seconds") == 0 && more) {
            o->seconds = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(arg, "--player-volume") == 0 && more) {
            o->hasPlayerVolume = 1;
            o->playerVolume = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(arg, "--fader") == 0 && more) {
            o->hasFader = 1;
            o->fader = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(arg, "--master") == 0 && more) {
            o->hasMaster = 1;
            o->master = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(arg, "--wav") == 0 && more) {
            o->wavPath = argv[++i];
        } else if (strcmp(arg, "--stats") == 0 && more) {
            o->statsPath = argv[++i];
        } else {
            return EXIT_USAGE;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    Options o;
    uint8_t *sdat = NULL;
    size_t sdatSize = 0;
    int seqNo, rc, status_playing;
    const NNSSndSeqParam *param;
    const char *name;
    NNSSndHeapHandle heap;
    size_t totalFrames, done;
    unsigned chunk;
    int16_t *pcm = NULL;
    uint64_t costNs = 0;
    size_t faderDebugAt = 0;
#ifdef NITRO_SOUND_STATS
    Diag *diag = NULL;
#endif

    rc = parse_args(argc, argv, &o);
    if (rc) {
        usage();
        return rc;
    }
    if (o.list) {
        if (load_sdat(o.sdatPath, &sdat, &sdatSize)) return EXIT_INPUT;
        NNS_SndArcInitOnMemory(&sArc, sdat);
        print_list();
        return 0;
    }
    if (!o.seqArg || !o.wavPath) {
        usage();
        return EXIT_USAGE;
    }
    if (o.seconds == 0 || o.seconds > 3600u)
        return fail(EXIT_INPUT, "--seconds must be 1..3600", NULL);
    if ((o.hasMaster && o.master > 127u) || (o.hasPlayerVolume && o.playerVolume > 127u))
        return fail(EXIT_INPUT, "volumes are 0..127", NULL);
#ifndef NITRO_SOUND_STATS
    if (o.statsPath)
        return fail(EXIT_USAGE, "--stats needs the sdat_render build (NITRO_SOUND_STATS)", NULL);
#endif
    if (load_sdat(o.sdatPath, &sdat, &sdatSize)) return EXIT_INPUT;
    NNS_SndArcInitOnMemory(&sArc, sdat);
    if (any_single_load())
        return fail(EXIT_UNSUPPORTED, "a wave archive loads one wave at a time (FS read)", NULL);

    if (o.seqArg[0] >= '0' && o.seqArg[0] <= '9') {
        seqNo = (int)strtol(o.seqArg, NULL, 10);
    } else {
        seqNo = seq_by_name(o.seqArg);
    }
    param = seqNo >= 0 ? NNS_SndArcGetSeqParam(seqNo) : NULL;
    if (!param) return fail(EXIT_INPUT, "no such sequence", o.seqArg);
    name = seq_name(seqNo);

    boot_sound();
    heap = NNS_SndHeapCreate(sHeap.bytes, HEAP_BYTES);
    if (!heap || !NNS_SndArcPlayerSetup(heap))
        return fail(EXIT_START, "sound heap or player setup failed", NULL);
    NNS_SndHandleInit(&sHandle);
    if (o.hasMaster) NNS_SndSetMasterVolume((int)o.master);

    totalFrames = (size_t)o.seconds * NITRO_SOUND_RATE;
    pcm = malloc(totalFrames * 2u * sizeof(int16_t));
    if (!pcm) return fail(EXIT_START, "out of memory for the output", NULL);

#ifdef NITRO_SOUND_STATS
    if (o.statsPath) {
        diag = calloc(1, sizeof(*diag));
        if (!diag) return fail(EXIT_START, "out of memory for the diagnostics", NULL);
        diag->windowCount = o.seconds;
        diag->windows = calloc(o.seconds, sizeof(Level));
        if (!diag->windows) return fail(EXIT_START, "out of memory for the diagnostics", NULL);
        NitroSound_SetFrameHook(on_frame, diag);
    }
#endif

    if (!NNS_SndArcPlayerStartSeqEx(&sHandle, -1, -1, -1, seqNo))
        return fail(EXIT_START, "the sequence did not start", o.seqArg);
    if (o.hasPlayerVolume) NNS_SndPlayerSetPlayerVolume((int)param->playerNo, (u8)o.playerVolume);
    /* GF_SndHandleMoveVolume(handle, V, 0) on the sequence's handle, once it has
     * its player (StartSeq allocates the player at once). The game's wrapper adds
     * the GB-sounds volume for the field handle; nothing here needs that. */
    if (o.hasFader) NNS_SndPlayerMoveVolume(&sHandle, (int)o.fader, 0);
    faderDebugAt = o.hasFader ? NITRO_SOUND_RATE : 0;

    /* One 60 Hz game frame per NNS_SndMain and per block of samples; the
     * block boundaries follow the game's frame rate exactly (integer). */
    done = 0;
    for (chunk = 0; done < totalFrames; ++chunk) {
        size_t end = (size_t)(((uint64_t)chunk + 1u) * NITRO_SOUND_RATE / GAME_FPS);
        size_t n;
        uint64_t t0;

        if (end > totalFrames) end = totalFrames;
        n = end - done;
        NNS_SndMain();
        NitroPort_SoundPumpReplies();
        t0 = now_ns();
        NitroPort_SoundRender(pcm + 2u * done, n);
        costNs += now_ns() - t0;
        done = end;
        if (faderDebugAt && done >= faderDebugAt) {
            print_fader_state((int)o.fader);
            faderDebugAt = 0;
        }
    }
    /* After the last block, one NNS_SndMain brings the ARM9 status up to date. */
    NNS_SndMain();
    /* The driver's status bit is for the ARM7 player the sequence got, which is
     * the NitroSystem player's playerNo (not the SDAT's playerNo: the BGM above
     * reads status 0x1 with SDAT player 7). NNSi_SndPlayerMain frees the player
     * when that bit clears, so the handle holds none once the sequence stops. */
    {
        NNSSndSeqPlayer *endPlayer = SND_HANDLE_PLAYER(&sHandle);
        status_playing = endPlayer != NULL &&
                         (SND_GetPlayerStatus() & (1u << endPlayer->playerNo)) != 0;
    }

    /* Output statistics: what leaves the mixer, per window and overall. */
#ifdef NITRO_SOUND_STATS
    if (diag) {
        size_t w;
        for (w = 0; w < diag->windowCount; ++w) {
            size_t first = w * WINDOW_FRAMES;
            size_t count = totalFrames - first < WINDOW_FRAMES ? totalFrames - first : WINDOW_FRAMES;
            level_output(&diag->windows[w], pcm + 2u * first, count);
        }
        level_output(&diag->overall, pcm, totalFrames);
        finish_runs(diag, totalFrames);
    }
#endif

    if (write_wav(o.wavPath, pcm, totalFrames)) return EXIT_INPUT;

#ifdef NITRO_SOUND_STATS
    if (diag && o.statsPath) {
        FILE *out = fopen(o.statsPath, "w");
        size_t w;
        int c;
        if (!out) return fail(EXIT_INPUT, "cannot create", o.statsPath);
        fprintf(out, "{\n  \"sdat\": \"%s\",\n", o.sdatPath);
        fprintf(out, "  \"sequence\": {\"id\": %d, \"name\": \"%s\", \"sdatVolume\": %u, "
                "\"player\": %u, \"bank\": %u, \"channelPrio\": %u, \"playerPrio\": %u},\n",
                seqNo, name ? name : "-", param->volume, param->playerNo, param->bankNo,
                param->channelPrio, param->playerPrio);
        fprintf(out, "  \"seconds\": %u, \"frames\": %zu, \"rate\": %u,\n",
                o.seconds, totalFrames, NITRO_SOUND_RATE);
        if (o.hasPlayerVolume)
            fprintf(out, "  \"masterVolume\": %d, \"playerVolume\": %u, \"outputShift\": 1,\n",
                    o.hasMaster ? (int)o.master : 127, o.playerVolume);
        else
            fprintf(out, "  \"masterVolume\": %d, \"playerVolume\": null, \"outputShift\": 1,\n",
                    o.hasMaster ? (int)o.master : 127);
        fprintf(out, "  \"playingAtEnd\": %s, \"lastActiveFrame\": %" PRIu64 ",\n",
                status_playing ? "true" : "false",
                diag->anyActive ? diag->lastActiveFrame : 0u);
        if (o.hasFader) fprintf(out, "  \"fader\": %u,\n", o.fader);
        else fprintf(out, "  \"fader\": null,\n");
        fprintf(out, "  \"overall\": {\n");
        json_level(out, &diag->overall, "    ");
        fprintf(out, "\n  },\n  \"windows\": [\n");
        for (w = 0; w < diag->windowCount; ++w) {
            fprintf(out, "    {\"t\": %zu,\n", w);
            json_level(out, &diag->windows[w], "     ");
            fprintf(out, "}%s\n", w + 1 < diag->windowCount ? "," : "");
        }
        fprintf(out, "  ],\n  \"clicks\": {\n");
        for (c = 0; c < CAUSE_COUNT; ++c) {
            fprintf(out, "    ");
            json_formats(out, kCauseName[c], diag->clicks[c]);
            fprintf(out, "%s\n", c + 1 < CAUSE_COUNT ? "," : "");
        }
        fprintf(out, "  },\n  ");
        json_formats(out, "starts", diag->starts);
        fprintf(out, ",\n  ");
        json_formats(out, "activeChannelFrames", diag->activeFrames);
        fprintf(out, ",\n  \"events\": {\"start\": %" PRIu64 ", \"loop\": %" PRIu64
                ", \"end\": %" PRIu64 ", \"kill\": %" PRIu64 "}",
                diag->events[0], diag->events[1], diag->events[2], diag->events[3]);
        fprintf(out, ",\n  \"notes\": {\"runs\": %" PRIu64 ", \"stoppedByKill\": %" PRIu64
                ", \"endedNaturally\": %" PRIu64 ", \"restartedWhileOpen\": %" PRIu64
                ", \"aliveAtEnd\": %" PRIu64 ", \"longestSec\": %.2f, \"aliveLongestSec\": %.2f}",
                diag->runs, diag->killed, diag->ended, diag->restartedOpen, diag->aliveAtEnd,
                (double)diag->longestRun / NITRO_SOUND_RATE, (double)diag->aliveLongest / NITRO_SOUND_RATE);
        fprintf(out, ",\n  \"armVolumeHist16\": [");
        {
            int b;
            for (b = 0; b < 8; ++b) fprintf(out, "%s%" PRIu64, b ? ", " : "", diag->volHist[b]);
        }
        fprintf(out, "],\n  \"armShiftHist\": {\"0\": %" PRIu64 ", \"1\": %" PRIu64
                ", \"2\": %" PRIu64 ", \"4\": %" PRIu64 "}",
                diag->shiftHist[0], diag->shiftHist[1], diag->shiftHist[2], diag->shiftHist[3]);
        fprintf(out, ",\n  \"perChannel\": [\n");
        for (c = 0; c < NITRO_SOUND_CHANNEL_COUNT; ++c) {
            fprintf(out, "    {\"channel\": %d, \"starts\": %" PRIu64 ", ", c, diag->chanStarts[c]);
            json_formats(out, "frames", diag->chanFrames[c]);
            fprintf(out, "}%s\n", c + 1 < NITRO_SOUND_CHANNEL_COUNT ? "," : "");
        }
        fprintf(out, "  ],\n  \"renderNsPerSecond\": %.0f\n}\n",
                (double)costNs / o.seconds);
        if (fclose(out) != 0) return fail(EXIT_INPUT, "cannot write", o.statsPath);
    }
#endif

    printf("sdat_render: %s (%d) %u s, %s, render %.0f ns per second\n",
           name ? name : "-", seqNo, o.seconds,
           status_playing ? "still playing at the end" : "stopped",
           (double)costNs / o.seconds);
    free(pcm);
    free(sdat);
    return 0;
}
