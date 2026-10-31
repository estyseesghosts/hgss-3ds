/* Host test: CARDiCommandArg in lib/include/nitro/card/common.h (the public
 * header 3ds_port/nitro/src/port_card.c uses) under SDK_PORT_3DS.
 * The port spells the destination word dst (ntrtwl's name); the SDK and the
 * DS game spell it dest. Under the macro both names address the same word
 * (union { u32 dest; u32 dst; }), so this writes through one spelling and
 * reads back through the other, and checks the size and the neighbouring
 * words did not move. Without the macro dst does not exist.
 * Synthetic data only; no game or save data. */
#include <nitro/card/common.h>

int printf(const char *, ...); /* lib/include shadows the system headers; see tests/sdk/irq_handler */

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define OFFSETOF(type, member) ((unsigned)(&((const volatile type *)0)->member))

int main(void)
{
    CARDiCommandArg cmd;
    unsigned char *b = (unsigned char *)&cmd;
    unsigned i;

    CHECK(sizeof(CARDiCommandArg) == 96, "CARDiCommandArg is 96 bytes (%u)", (unsigned)sizeof(CARDiCommandArg));
    CHECK(OFFSETOF(CARDiCommandArg, src) == 12, "src at 12 (%u)", OFFSETOF(CARDiCommandArg, src));
    CHECK(OFFSETOF(CARDiCommandArg, dest) == 16, "dest at 16 (%u)", OFFSETOF(CARDiCommandArg, dest));
    CHECK(OFFSETOF(CARDiCommandArg, dst) == 16, "dst at 16 (%u)", OFFSETOF(CARDiCommandArg, dst));
    CHECK(OFFSETOF(CARDiCommandArg, len) == 20, "len at 20 (%u)", OFFSETOF(CARDiCommandArg, len));

    /* The two spellings alias the same word; neighbours are untouched. */
    for (i = 0; i < sizeof(cmd); i++) {
        b[i] = 0;
    }
    cmd.dst = 0x12345678;
    CHECK(cmd.dest == 0x12345678, "dest reads back dst (0x%08x)", (unsigned)cmd.dest);
    CHECK(cmd.src == 0, "src undisturbed (%u)", (unsigned)cmd.src);
    CHECK(cmd.len == 0, "len undisturbed (%u)", (unsigned)cmd.len);
    cmd.dest = 0;
    CHECK(cmd.dst == 0, "dst reads back dest (%u)", (unsigned)cmd.dst);
    cmd.src = 0xAAAAAAAA;
    cmd.len = 0x55555555;
    CHECK(cmd.dst == 0, "neighbours do not leak into dst (%u)", (unsigned)cmd.dst);

    printf("gcc_fixes card: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
