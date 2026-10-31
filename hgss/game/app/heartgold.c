/*
 * The game's start: see heartgold.h.
 *
 * The analogue of 3ds_port/game/app/platinum.c for HeartGold/SoulSilver.
 * It does what a DS's firmware and cartridge loader did before the game's
 * NitroMain: write the cartridge header and firmware settings the SDK reads,
 * hand the port the overlay table, and start the ARM7 services. Booting the
 * game is Phase 4+: this file exists so the link has a start.
 */

#include "heartgold.h"

#include <nitro.h>
#include <nitro_port.h>
#include <nitro_port_fs.h>
#include <stdlib.h>
#include <string.h>

extern const NitroPortOverlay gHeartgoldOverlays[];   /* gen/overlay_table.c */
extern const u32 gHeartgoldOverlayCount;

void NitroMain(void);

/* The MAC address the game sees. The game stores it in the save and
 * compares it on loading (a different DS): one fixed address keeps a save
 * "from this DS" on every console. 00:09:BF is one of Nintendo's prefixes. */
static const u8 sMacAddress[6] = { 0x00, 0x09, 0xBF, 0x48, 0x47, 0x53 };

/* DS cartridge header fields, at the offsets the hardware uses. The game
 * reads the game and maker codes at boot; the rest of a real header
 * (offsets into the cartridge) means nothing here: files come from the
 * data pack. HeartGold US is IPKE (SoulSilver is IPGE); the maker code is
 * the same "01" in both. */
#define ROM_HEADER_SIZE   0x200
#define ROM_TITLE_OFFSET  0x00
#define ROM_GAME_CODE     0x0C
#define ROM_MAKER_CODE    0x10
#define ROM_DEVICE_SIZE   0x14
#define ROM_VERSION       0x1E

void Heartgold_DefaultSettings(HeartgoldSettings *settings)
{
    static const char name[] = "Player";
    u32 i;

    memset(settings, 0, sizeof(*settings));
    for (i = 0; name[i] != '\0'; i++) {
        settings->nickname[i] = (u16)name[i];
    }
    settings->language = NVRAM_CONFIG_LANG_ENGLISH;
    settings->birthMonth = 1;
    settings->birthDay = 1;
    settings->favoriteColor = 0;
}

const char *Heartgold_OpenPak(const char *pakPath, const char *manifestPath)
{
    switch (NitroPort_OpenPak(pakPath, manifestPath, HEARTGOLD_PAK_BUILD_ID)) {
    case PAK_OK:
        return NULL;
    case PAK_ERR_IO:
        return "can't read the data pack or its manifest.json";
    case PAK_ERR_FORMAT:
        return "the data pack or manifest.json is damaged, or they don't belong together";
    case PAK_ERR_VERSION:
        return "the data pack was made by a different version of the builder";
    case PAK_ERR_BUILD_ID:
        return "the data pack has the wrong build ID; pack it again with --build-id "
               HEARTGOLD_PAK_BUILD_ID;
    case PAK_ERR_ROM_SHA1:
        return "the data pack was made from a ROM this version doesn't support";
    case PAK_ERR_MEMORY:
        return "out of memory opening the data pack";
    default:
        return "couldn't open the data pack";
    }
}

int Heartgold_CheckOverlays(void)
{
    static const u8 sDataFill[2] = { 0xA5, 0x3C };
    static const u8 sBssFill[2] = { 0x5A, 0xC3 };
    int failures = 0;
    int withInit = 0;
    u32 id;
    NitroPortOverlay *quiet;

    NitroPort_SetOverlays(gHeartgoldOverlays, gHeartgoldOverlayCount);
    FS_Init(FS_DMA_NOT_USE);

    /* Every overlay's static initializers are handed to the SDK by
     * NitroPort_OverlayInfo. Count them (some games use one per application
     * overlay) and verify FS_LoadOverlayInfo sees the same table. */
    for (id = 0; id < gHeartgoldOverlayCount; id++) {
        const NitroPortOverlay *o = &gHeartgoldOverlays[id];
        void (**f)(void);
        FSOverlayInfo info;
        BOOL any = FALSE;

        for (f = o->sinitStart; f < o->sinitEnd; f++) {
            any = any || *f != NULL;
        }
        withInit += any;
        if (!FS_LoadOverlayInfo(&info, MI_PROCESSOR_ARM9, id)
            || (any && (info.header.sinit_init != o->sinitStart
                        || info.header.sinit_init_end != o->sinitEnd))) {
            NitroPort_Log("[check] overlay %lu (%s): static initializers not passed on",
                          (unsigned long)id, o->name);
            failures++;
        }
    }

    /* The load checks run without the initializers: they expect the game to
     * be running (an application overlay's may reach into game state). */
    quiet = malloc(gHeartgoldOverlayCount * sizeof(*quiet));
    if (quiet == NULL) {
        NitroPort_Log("[check] out of memory");
        return 1;
    }
    memcpy(quiet, gHeartgoldOverlays, gHeartgoldOverlayCount * sizeof(*quiet));
    for (id = 0; id < gHeartgoldOverlayCount; id++) {
        quiet[id].sinitStart = quiet[id].sinitEnd = NULL;
    }
    NitroPort_SetOverlays(quiet, gHeartgoldOverlayCount);

    for (id = 0; id < gHeartgoldOverlayCount; id++) {
        const NitroPortOverlay *o = &gHeartgoldOverlays[id];
        u32 dataSize = (u32)(o->dataEnd - o->dataStart);
        u32 bssSize = (u32)(o->bssEnd - o->bssStart);
        u8 *initial = malloc(dataSize ? dataSize : 1);
        BOOL ok = TRUE;
        int pass;

        if (initial == NULL) {
            NitroPort_Log("[check] out of memory for overlay %s", o->name);
            failures++;
            continue;
        }
        if (dataSize != 0) {
            memcpy(initial, o->dataStart, dataSize);
        }

        /* Load it twice, with different garbage in its memory each time:
         * every load must restore .data and clear .bss. */
        for (pass = 0; pass < 2; pass++) {
            if (dataSize != 0) {
                memset(o->dataStart, sDataFill[pass], dataSize);
            }
            if (bssSize != 0) {
                memset(o->bssStart, sBssFill[pass], bssSize);
            }
            if (!FS_LoadOverlay(MI_PROCESSOR_ARM9, id)) {
                NitroPort_Log("[check] overlay %lu (%s): didn't load (pass %d)",
                              (unsigned long)id, o->name, pass + 1);
                failures++;
                ok = FALSE;
                continue;
            }
            if (dataSize != 0 && memcmp(o->dataStart, initial, dataSize) != 0) {
                ok = FALSE;
            }
            for (u32 i = 0; i < bssSize; i++) {
                if (o->bssStart[i] != 0) {
                    ok = FALSE;
                    break;
                }
            }
            if (!NitroPort_IsOverlayLoaded(id)) {
                ok = FALSE;
            }
            if (!FS_UnloadOverlay(MI_PROCESSOR_ARM9, id)
                || NitroPort_IsOverlayLoaded(id)) {
                ok = FALSE;
            }
        }
        if (!ok) {
            NitroPort_Log("[check] overlay %lu (%s): data not reset on loading",
                          (unsigned long)id, o->name);
            failures++;
        }
        free(initial);
    }
    free(quiet);

    NitroPort_Log("[check] %lu overlays (%d with static initializers), %d failed",
                  (unsigned long)gHeartgoldOverlayCount, withInit, failures);
    return failures;
}

void Heartgold_Run(const HeartgoldSettings *settings)
{
    u8 header[ROM_HEADER_SIZE];
    NitroPortFirmware firmware;

    memset(header, 0, sizeof(header));
    memcpy(header + ROM_TITLE_OFFSET, "POKEMON HG", 10);
    memcpy(header + ROM_GAME_CODE, HEARTGOLD_ROM_GAME_CODE,
           sizeof HEARTGOLD_ROM_GAME_CODE - 1);
    memcpy(header + ROM_MAKER_CODE, "01", 2);
    header[ROM_DEVICE_SIZE] = 10;   /* 128 MB */
    header[ROM_VERSION] = 0;

    memset(&firmware, 0, sizeof(firmware));
    firmware.romHeader = header;
    firmware.romHeaderSize = sizeof(header);
    firmware.nickname = settings->nickname;
    firmware.language = settings->language;
    firmware.birthMonth = settings->birthMonth;
    firmware.birthDay = settings->birthDay;
    firmware.favoriteColor = settings->favoriteColor;
    memcpy(firmware.macAddress, sMacAddress, sizeof(sMacAddress));
    firmware.rtcOffset = 0;
    NitroPort_BootFirmware(&firmware);

    NitroPort_SetOverlays(gHeartgoldOverlays, gHeartgoldOverlayCount);

    /* The ARM7's services: the save chip, the clock, the touch screen and
     * power management (sound, microphone and wireless aren't done yet). */
    NitroPort_SetSavePath(settings->savePath);
    NitroPort_InitArm7Services();
    NitroPort_Log("[heartgold] save file: %s",
                  settings->savePath != NULL ? settings->savePath : "(none)");

    NitroPort_Log("[heartgold] starting the game");
    NitroMain();
    NitroPort_Log("[heartgold] NitroMain returned");
    OS_Terminate();
    for (;;) {
    }
}
