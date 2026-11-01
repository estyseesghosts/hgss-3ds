/*
 * The game on a 3DS: the program shell around Heartgold_Run.
 *
 * This is the HeartGold/SoulSilver counterpart of 3ds_port/game/app/main_3ds.c
 * (Platinum's runner), cut down to the Phase 2 infrastructure: libctru and
 * gfx bring up the console, the console's profile fills in the DS firmware
 * settings, the data pack is opened, and the game runs on a thread of its
 * own while the main thread waits for it. Drawing, sound, the remaster and
 * the geometry thread are later phases; the port hooks below are enough to
 * link and to keep a frame clock going.
 *
 * Like 3ds_port/game/app/main_3ds.c, this file includes only libctru and
 * heartgold.h: libctru's headers cannot be mixed with the NitroSDK headers
 * that heartgold.c uses. The port hooks are plain C and do not need any
 * SDK type.
 *
 * Needs the player's data pack on the SD card:
 *   sdmc:/3ds/pokeheartgold/heartgold3ds.pak
 *   sdmc:/3ds/pokeheartgold/manifest.json
 * keeps the save in sdmc:/3ds/pokeheartgold/heartgold.sav (a raw 512 KiB
 * save, as emulators keep them), and writes the log to
 * sdmc:/3ds/pokeheartgold/log.txt.
 */

#include <3ds.h>
#include <citro3d.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "heartgold.h"

#define DATA_DIR "sdmc:/3ds/pokeheartgold"
#define PAK_PATH DATA_DIR "/heartgold3ds.pak"
#define MANIFEST_PATH DATA_DIR "/manifest.json"
#define SAVE_PATH DATA_DIR "/heartgold.sav"
#define LOG_PATH DATA_DIR "/log.txt"

#define GAME_STACK_SIZE (512 * 1024)

/* The port's hooks (see nitro_port.h). Declared here rather than included:
 * nitro_port.h pulls in NitroSDK's types, which cannot live next to
 * libctru's. These are exactly the plain-C prototypes heartgold.c and
 * main_pc.c use. */
void NitroPort_Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void NitroPort_SetButtons(uint16_t held, int lidClosed);
void NitroPort_CardFlush(void);

/* PAD_BUTTON_* / PAD_KEY_* bits (NitroSDK's pad.h, repeated as plain
 * numbers: the header cannot be included here). */
#define DS_BUTTON_A      0x0001
#define DS_BUTTON_B      0x0002
#define DS_BUTTON_SELECT 0x0004
#define DS_BUTTON_START  0x0008
#define DS_KEY_RIGHT     0x0010
#define DS_KEY_LEFT      0x0020
#define DS_KEY_UP        0x0040
#define DS_KEY_DOWN      0x0080
#define DS_BUTTON_R      0x0100
#define DS_BUTTON_L      0x0200
#define DS_BUTTON_X      0x0400
#define DS_BUTTON_Y      0x0800

static FILE *sLog;
static unsigned long sFrame;
static volatile bool sQuit;

/* The 3DS's buttons, as the DS's PAD bits. */
static uint16_t DsButtons(uint32_t held)
{
    uint16_t out = 0;

    if (held & KEY_A)      out |= DS_BUTTON_A;
    if (held & KEY_B)      out |= DS_BUTTON_B;
    if (held & KEY_SELECT) out |= DS_BUTTON_SELECT;
    if (held & KEY_START)  out |= DS_BUTTON_START;
    if (held & KEY_DRIGHT) out |= DS_KEY_RIGHT;
    if (held & KEY_DLEFT)  out |= DS_KEY_LEFT;
    if (held & KEY_DUP)    out |= DS_KEY_UP;
    if (held & KEY_DDOWN)  out |= DS_KEY_DOWN;
    if (held & KEY_R)      out |= DS_BUTTON_R;
    if (held & KEY_L)      out |= DS_BUTTON_L;
    if (held & KEY_X)      out |= DS_BUTTON_X;
    if (held & KEY_Y)      out |= DS_BUTTON_Y;
    return out;
}

/* ------------------------------------------------------------------ hooks */

void NitroPort_Log(const char *fmt, ...)
{
    va_list args;

    fprintf(stderr, "[%lu] ", sFrame);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);

    if (sLog != NULL) {
        fprintf(sLog, "[%lu] ", sFrame);
        va_start(args, fmt);
        vfprintf(sLog, fmt, args);
        va_end(args);
        fputc('\n', sLog);
    }
}

/* Once a frame, from the game's idle path: keep the HOME menu serviced and
 * hand the buttons to the port. */
void NitroPort_OnFrame(void)
{
    uint32_t held;

    sFrame++;
    if (!aptMainLoop()) {
        sQuit = true;
    }
    hidScanInput();
    held = hidKeysHeld();
    NitroPort_SetButtons(DsButtons(held), 0);

    if (sFrame % 600 == 0) {
        NitroPort_Log("frame %lu", sFrame);
    }
}

/* The game stopped itself. */
void NitroPort_Terminate(void)
{
    NitroPort_Log("the game stopped");
    sQuit = true;
    threadExit(0);
}

/* The game asked the DS to restart. A real restart reopens the data pack
 * and starts the game again; until that exists, save what is flushed and
 * end the program (as the DS's power-off would). */
void NitroPort_ResetSystem(u32 parameter)
{
    NitroPort_Log("reset requested (%lu): stopping", (unsigned long)parameter);
    sQuit = true;
    NitroPort_CardFlush();
    svcExitProcess();
}

/* ------------------------------------------------------------- starting */

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
    /* The DS's language codes are the 3DS's first six. */
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

static void GameThread(void *arg)
{
    Heartgold_Run((const HeartgoldSettings *)arg);
}

/* Wait for the game's thread; the HOME menu's close ends the wait. */
static void WaitForGame(Thread thread)
{
    while (!sQuit) {
        if (R_SUCCEEDED(threadJoin(thread, 100 * 1000000LL))) {
            threadFree(thread);
            break;
        }
        /* The thread is still running (a 100 ms timeout, not an error):
         * keep the HOME menu serviced from here too. */
        if (!aptMainLoop()) {
            sQuit = true;
        }
    }
}

int main(int argc, char **argv)
{
    HeartgoldSettings settings;
    const char *error;

    (void)argc;
    (void)argv;
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    consoleInit(GFX_BOTTOM, NULL);

    mkdir("sdmc:/3ds", 0777);
    mkdir(DATA_DIR, 0777);
    sLog = fopen(LOG_PATH, "w");

    NitroPort_Log("pokeheartgold-3ds starting");

    error = Heartgold_OpenPak(PAK_PATH, MANIFEST_PATH);
    if (error != NULL) {
        NitroPort_Log("Can't start: %s.", error);
        NitroPort_Log("Put heartgold3ds.pak and manifest.json (made with the builder "
                      "from your own cartridge) in " DATA_DIR "/.");
        /* The plan asks for an on-screen message, not only log.txt: the
         * console owns the bottom screen (consoleInit in main). */
        consoleClear();
        printf("\n pokeheartgold-3ds\n\n Can't start: %s.\n\n"
               " Put heartgold3ds.pak and manifest.json\n"
               " (made with the builder from your own\n"
               " cartridge) in:\n\n  " DATA_DIR "/\n\n"
               " See " DATA_DIR "/log.txt for the details.\n\n"
               " Press START to exit.\n", error);
        while (aptMainLoop() && !sQuit) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) {
                break;
            }
            svcSleepThread(100 * 1000000LL);
        }
    } else {
        ReadProfile(&settings);
        settings.savePath = SAVE_PATH;

        Thread thread = threadCreate(GameThread, &settings, GAME_STACK_SIZE, 0x30, -2, false);
        if (thread == NULL) {
            NitroPort_Log("Can't start the game's thread.");
        } else {
            NitroPort_Log("running the game");
            WaitForGame(thread);
        }
    }

    NitroPort_Log("stopping (frame %lu)", sFrame);
    NitroPort_CardFlush();
    if (sLog != NULL) {
        fflush(sLog);
        fclose(sLog);
        sLog = NULL;
    }
    C3D_Fini();
    gfxExit();
    return 0;
}
