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

/* Everything version-specific is selected at compile time by the game
 * define the decomp's own build uses (config.h): `HEARTGOLD` or
 * `SOULSILVER`. tools/hgss_game.py --game adds it to every game and app
 * object, so one tree builds either program. The two programs have separate
 * folders, packs and saves, so a save can never be read by the other game. */
#if defined(SOULSILVER)
/* The data pack's build ID, checked by Heartgold_OpenPak at boot. The
 * builder must be given the same one (scripts/prepare_game.py does). */
#define HEARTGOLD_PAK_BUILD_ID "soulsilver-nitrofs-1"
/* The cartridge game code at header +0x0C: IPKE is HeartGold (USA), IPGE
 * is SoulSilver (USA). The game reads this header at boot (the
 * developer/debug checks and the save's game identity), and
 * builder/verify_rom.py names the same codes. */
#define HEARTGOLD_ROM_GAME_CODE "IPGE"
#define HEARTGOLD_ROM_TITLE "POKEMON SS"
#define HEARTGOLD_SAVE_NAME "soulsilver.sav"
#define HEARTGOLD_DATA_DIR "sdmc:/3ds/pokeheartgold-ss"
#define HEARTGOLD_PAK_NAME "soulsilver3ds.pak"
#define HEARTGOLD_APP_NAME "pokesoulsilver-3ds"
#define HEARTGOLD_LOG_TAG "soulsilver"
#else
#define HEARTGOLD_PAK_BUILD_ID "heartgold-nitrofs-1"
#define HEARTGOLD_ROM_GAME_CODE "IPKE"
#define HEARTGOLD_ROM_TITLE "POKEMON HG"
#define HEARTGOLD_SAVE_NAME "heartgold.sav"
#define HEARTGOLD_DATA_DIR "sdmc:/3ds/pokeheartgold"
#define HEARTGOLD_PAK_NAME "heartgold3ds.pak"
#define HEARTGOLD_APP_NAME "pokeheartgold-3ds"
#define HEARTGOLD_LOG_TAG "heartgold"
#endif

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
