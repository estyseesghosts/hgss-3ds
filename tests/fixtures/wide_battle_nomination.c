/* Battle and field both nominate GPU backgrounds. The renderer validates
 * each candidate; the battle flag must not suppress nomination. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include WIDE_SOURCE

/* Stubs: nomination reads none of these (overlay/picture state only
 * matters to the picture/cell paths, which this test never calls). */
uint32_t NitroPort_OverlaySerial(void)
{
    return 0;
}

int NitroPort_OverlayLoadedNamed(const char *name)
{
    (void)name;
    return 0;
}

int NitroPort_LowerMenuPanelShowsUpper(void)
{
    return 0;
}

static unsigned checks, failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            failures++;                                                                            \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    /* Off by default: the field keeps its takes. */
    CHECK(NitroPort_BattleActive() == 0);
    CHECK(NitroPort_NominateGpuBgs() == 0xE);
    /* Battle uses the same nomination as the field. */
    NitroPort_SetBattleActive(1);
    CHECK(NitroPort_BattleActive() != 0);
    CHECK(NitroPort_NominateGpuBgs() == 0xE);
    /* After the battle: takes resume without any other call. */
    NitroPort_SetBattleActive(0);
    CHECK(NitroPort_BattleActive() == 0);
    CHECK(NitroPort_NominateGpuBgs() == 0xE);
    /* Any nonzero input sets the flag; there is no counter to drain. */
    NitroPort_SetBattleActive(42);
    CHECK(NitroPort_BattleActive() != 0);
    CHECK(NitroPort_NominateGpuBgs() == 0xE);
    NitroPort_SetBattleActive(0);
    CHECK(NitroPort_NominateGpuBgs() == 0xE);
    printf("wide-battle: %u checks, %u failed\n", checks, failures);
    return failures != 0;
}
