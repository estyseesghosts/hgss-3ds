/*
 * The game on a PC, for the Phase 2 link and, later, for finding out how
 * far the game gets.
 *
 *   pokeheartgold-pc [heartgold3ds.pak] [manifest.json] [--save FILE]
 *                   [--frames N]
 *
 * Runs the same game code as the future 3DS program, with the same data
 * pack. The save is a raw 512 KB file, as emulators keep them: --save FILE,
 * or heartgold.sav (made if missing). Everything the port logs goes to
 * stderr. With --frames N the runner stops after N frames.
 *
 * This is deliberately smaller than 3ds_port/game/app/main_pc.c (the
 * Platinum runner): it exists to give the link a main and the port its
 * hooks, not to render anything yet.
 */

#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nitro.h>
#include <nitro_port.h>

#include "heartgold.h"

static int sFrame;
static int sFrameLimit;

void NitroPort_Log(const char *fmt, ...)
{
    va_list args;

    fprintf(stderr, "[%d] ", sFrame);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

void NitroPort_OnFrame(void)
{
    sFrame++;
    if (sFrameLimit > 0 && sFrame >= sFrameLimit) {
        NitroPort_Log("frame limit reached");
        NitroPort_Terminate();
    }
}

void NitroPort_Terminate(void)
{
    exit(0);
}

void NitroPort_ResetSystem(u32 parameter)
{
    NitroPort_Log("[heartgold] reset requested (%lu)", (unsigned long)parameter);
    exit((int)parameter);
}

int main(int argc, char **argv)
{
    HeartgoldSettings settings;
    const char *pak = NULL;
    const char *manifest = NULL;
    const char *save = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            save = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            sFrameLimit = (int)strtol(argv[++i], NULL, 0);
        } else if (pak == NULL) {
            pak = argv[i];
        } else if (manifest == NULL) {
            manifest = argv[i];
        } else {
            fprintf(stderr, "pokeheartgold-pc: unexpected argument %s\n", argv[i]);
            return 2;
        }
    }

    Heartgold_DefaultSettings(&settings);
    settings.savePath = save != NULL ? save : HEARTGOLD_SAVE_NAME;

    if (pak != NULL) {
        const char *error = Heartgold_OpenPak(pak, manifest);
        if (error != NULL) {
            fprintf(stderr, "pokeheartgold-pc: %s\n", error);
            return 2;
        }
    }

    Heartgold_Run(&settings);
    return 0;
}
