/*
 * The game's start, shared by the PC runner (main_pc.c) and, later, the
 * 3DS shell: what the DS firmware and the cartridge loader did before
 * HeartGold/SoulSilver's NitroMain, then NitroMain itself.
 *
 * Plain C types only: the 3DS program includes libctru's headers, which
 * cannot be mixed with NitroSDK's. heartgold.c is the side that talks to
 * the SDK.
 *
 * A program also provides the port's hooks (see nitro_port.h), with these
 * plain-C equivalents of their SDK types:
 *   void NitroPort_Log(const char *fmt, ...);        every log line
 *   void NitroPort_OnFrame(void);                   once a frame: show it,
 *                                                   read the buttons
 *   void NitroPort_Terminate(void);                 the game stopped itself
 *   void NitroPort_ResetSystem(uint32_t parameter); the game asked to restart
 */

#ifndef HEARTGOLD_APP_H
#define HEARTGOLD_APP_H

#include <stdint.h>

/* The data pack's build ID: the builder must be given the same one
 * (python builder/pack.py data_pack/ -o heartgold3ds.pak --build-id ...).
 * It names how the pack's contents are laid out, so a program only opens
 * packs it can read. These packs hold the ROM's NitroFS files unchanged. */
#define HEARTGOLD_PAK_BUILD_ID "heartgold-nitrofs-1"

/* The cartridge game code at header +0x0C: IPKE is HeartGold (USA),
 * IPGE is SoulSilver (USA). The game reads this header at boot (the
 * developer/debug checks and the save's game identity), and
 * builder/verify_rom.py names the same codes. A SoulSilver build
 * changes this, the build ID and the save name. */
#define HEARTGOLD_ROM_GAME_CODE "IPKE"

/* The save's file name under the port's data directory. HeartGold and
 * SoulSilver use separate folders so their saves can never be mixed. */
#define HEARTGOLD_SAVE_NAME "heartgold.sav"

/* The user's settings, as a DS's firmware would hand them to the game.
 * Heartgold_DefaultSettings fills in neutral values; a program may replace
 * them (the 3DS one uses the console's profile). */
typedef struct HeartgoldSettings {
    uint16_t nickname[11];   /* UTF-16, 0-terminated */
    uint8_t language;        /* 0 Japanese, 1 English, 2 French, 3 German,
                                4 Italian, 5 Spanish */
    uint8_t birthMonth;
    uint8_t birthDay;
    uint8_t favoriteColor;   /* 0-15 */
    const char *savePath;    /* the save chip's contents: a raw 512 KB file,
                                as emulators keep them (made if missing) */
} HeartgoldSettings;

void Heartgold_DefaultSettings(HeartgoldSettings *settings);

/* Open the data pack and its manifest. Returns NULL, or what is wrong in
 * words. */
const char *Heartgold_OpenPak(const char *pakPath, const char *manifestPath);

/* Set up the DS's state as the firmware leaves it, register the overlays,
 * start the ARM7 services, and run the game (NitroMain, which never
 * returns). The data pack must be open. */
void Heartgold_Run(const HeartgoldSettings *settings) __attribute__((noreturn));

/* A self-check of the overlay table (no data pack needed): register the
 * table, then load every overlay twice through the SDK's FS_LoadOverlay,
 * corrupting its .data and .bss between loads, and verify that each load
 * restores the initial .data and clears .bss. Returns the number of
 * failures; the summary and the failing overlays go to NitroPort_Log. */
int Heartgold_CheckOverlays(void);

#endif
