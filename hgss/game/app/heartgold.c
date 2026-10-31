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
 * data pack. HeartGold US is IPGE; the maker code is the same "01". */
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

void Heartgold_Run(const HeartgoldSettings *settings)
{
    u8 header[ROM_HEADER_SIZE];
    NitroPortFirmware firmware;

    memset(header, 0, sizeof(header));
    memcpy(header + ROM_TITLE_OFFSET, "POKEMON HG", 10);
    memcpy(header + ROM_GAME_CODE, "IPGE", 4);
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
