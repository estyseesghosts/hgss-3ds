/* Host test: CARDiCommandArg in lib/NitroSDK/include/CARD_common.h (the flat
 * header the SDK's own card sources, e.g. CARD_backup.c's p->cmd->dest, use)
 * under SDK_PORT_3DS. Same union as the public header: dst aliases dest at
 * offset 16 with sizeof unchanged. Without the macro dst does not exist.
 * Synthetic data only; no game or save data. */
#include "CARD_common.h"

int printf(const char *, ...); /* lib/include shadows the system headers; see tests/sdk/irq_handler */

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define OFFSETOF(type, member) ((unsigned)(&((const volatile type *)0)->member))

int main(void)
{
    CARDiCommandArg cmd;
    unsigned char *b = (unsigned char *)&cmd;
    unsigned i;

    CHECK(sizeof(CARDiCommandArg) == 96, "flat CARDiCommandArg is 96 bytes (%u)", (unsigned)sizeof(CARDiCommandArg));
    CHECK(OFFSETOF(CARDiCommandArg, dest) == 16, "dest at 16 (%u)", OFFSETOF(CARDiCommandArg, dest));
    CHECK(OFFSETOF(CARDiCommandArg, dst) == 16, "dst at 16 (%u)", OFFSETOF(CARDiCommandArg, dst));

    for (i = 0; i < sizeof(cmd); i++) {
        b[i] = 0;
    }
    cmd.dest = 0x9ABCDEF0;
    CHECK(cmd.dst == 0x9ABCDEF0, "dst reads back dest (0x%08x)", (unsigned)cmd.dst);

    printf("gcc_fixes card_flat: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
