/* Host test: NVRAMConfigOwnerInfo / NVRAMConfigOption in lib/include/nitro/spi/userInfo.h
 * under SDK_PORT_3DS. The port (3ds_port/nitro/src/port_boot.c) spells bit 0 of the owner byte
 * favoriteColor and bit 11 of the option halfword input_favoriteColor; the SDK spells them
 * favouriteColour / input_favouriteColour. Under the macro both spellings name the same bits
 * (the union wraps the whole bitfield run), so this writes through one spelling and reads back
 * through the other, and checks the struct sizes and the neighbouring fields did not move.
 * Synthetic data only; no game or save data. */
#include <nitro/types.h>
#include <nitro/spi/userInfo.h>

int printf(const char *, ...); /* lib/include shadows the system headers; see tests/sdk/irq_handler */

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define OFFSETOF(type, member) ((unsigned)(&((const volatile type *)0)->member))

static void zero(void *p, unsigned n)
{
    unsigned char *b = p;
    unsigned i;
    for (i = 0; i < n; i++) {
        b[i] = 0;
    }
}

int main(void)
{
    NVRAMConfigOwnerInfo owner;
    NVRAMConfigOption option;

    CHECK(sizeof(NVRAMConfigOwnerInfo) == 0x50, "NVRAMConfigOwnerInfo is 0x50 bytes (%u)", (unsigned)sizeof(NVRAMConfigOwnerInfo));
    CHECK(sizeof(NVRAMConfigOption) == 0x0c, "NVRAMConfigOption is 0x0c bytes (%u)", (unsigned)sizeof(NVRAMConfigOption));
    CHECK(sizeof(NVRAMConfigData) == 0x70, "NVRAMConfigData is 0x70 bytes (%u)", (unsigned)sizeof(NVRAMConfigData));
    CHECK(sizeof(NVRAMConfig) == 0x74, "NVRAMConfig is 0x74 bytes (%u)", (unsigned)sizeof(NVRAMConfig));
    CHECK(OFFSETOF(NVRAMConfigOwnerInfo, birthday) == 1, "owner.birthday at 1 (%u)", OFFSETOF(NVRAMConfigOwnerInfo, birthday));
    CHECK(OFFSETOF(NVRAMConfigOption, timeZone) == 2, "option.timeZone at 2 (%u)", OFFSETOF(NVRAMConfigOption, timeZone));
    CHECK(OFFSETOF(NVRAMConfigOption, rtcOffset) == 4, "option.rtcOffset at 4 (%u)", OFFSETOF(NVRAMConfigOption, rtcOffset));

    /* The two spellings alias the same 4 bits; the neighbouring rsv bits are untouched. */
    zero(&owner, sizeof(owner));
    owner.favoriteColor = 0xA;
    CHECK(owner.favouriteColour == 0xA, "favouriteColour reads back favoriteColor (%u)", (unsigned)owner.favouriteColour);
    CHECK(owner.rsv == 0, "owner.rsv undisturbed (%u)", (unsigned)owner.rsv);
    owner.favouriteColour = 3;
    CHECK(owner.favoriteColor == 3, "favoriteColor reads back favouriteColour (%u)", (unsigned)owner.favoriteColor);

    /* Bit 11 aliases; the rest of the run (language..rsv, input_tp..input_nickname) is intact. */
    zero(&option, sizeof(option));
    option.input_favoriteColor = 1;
    CHECK(option.input_favouriteColour == 1, "input_favouriteColour reads back input_favoriteColor (%u)", (unsigned)option.input_favouriteColour);
    CHECK(option.input_tp == 0, "input_tp undisturbed (%u)", (unsigned)option.input_tp);
    CHECK(option.language == 0, "language undisturbed (%u)", (unsigned)option.language);
    option.input_favouriteColour = 0;
    CHECK(option.input_favoriteColor == 0, "input_favoriteColor reads back input_favouriteColour (%u)", (unsigned)option.input_favoriteColor);
    option.language = 5;
    option.input_tp = 1;
    option.input_nickname = 1;
    CHECK(option.language == 5, "language round-trips (%u)", (unsigned)option.language);
    CHECK(option.input_tp == 1, "input_tp round-trips (%u)", (unsigned)option.input_tp);
    CHECK(option.input_nickname == 1, "input_nickname round-trips (%u)", (unsigned)option.input_nickname);
    CHECK(option.input_favoriteColor == 0, "bit 11 still clear (%u)", (unsigned)option.input_favoriteColor);

    printf("gcc_fixes userinfo: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
