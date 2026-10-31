/* Host test: PXI FIFO tag values in lib/include/nitro/pxi/fifo_shared.h under SDK_PORT_3DS.
 * The port's virtual ARM7 (3ds_port/nitro/src/pxi.c) names PXI_FIFO_TAG_CTRDG_PHI and sizes its
 * tables by PXI_MAX_FIFO_TAG. The tag must follow PXI_FIFO_TAG_CTRDG_Ex and be 17. */
#include <nitro/pxi/fifo_shared.h>

int printf(const char *, ...); /* lib/include shadows the system headers; see tests/sdk/irq_handler */

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    CHECK(PXI_FIFO_TAG_CTRDG_Ex == 16, "PXI_FIFO_TAG_CTRDG_Ex is 16 (%d)", (int)PXI_FIFO_TAG_CTRDG_Ex);
    CHECK(PXI_FIFO_TAG_CTRDG_PHI == 17, "PXI_FIFO_TAG_CTRDG_PHI is 17 (%d)", (int)PXI_FIFO_TAG_CTRDG_PHI);
    CHECK(PXI_MAX_FIFO_TAG == 32, "PXI_MAX_FIFO_TAG is 32 (%d)", (int)PXI_MAX_FIFO_TAG);
    printf("gcc_fixes pxi tags: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
