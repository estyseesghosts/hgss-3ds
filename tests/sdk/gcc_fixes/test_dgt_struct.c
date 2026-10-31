/* Host test: struct DGTHash2Context in lib/NitroSDK/include/dgt.h under
 * SDK_PORT_3DS. 3ds_port/nitro/src/dgt.c works the ntrtwl fields (h0..h4,
 * Nl, Nh, num, data, dummy) while MATH_CalcSHA1 (via MATH_dgt.c) and the DS
 * game use the HGSS fields (Intermediate_Hash/Length_Low/...). Under the
 * macro both field sets live in one anonymous union, so this writes through
 * one spelling and reads back through the other, and checks the size is
 * unchanged (104 bytes, room for dgt.c's data[16] plus dummy[2]). Without
 * the macro the ntrtwl fields do not exist.
 * Synthetic data only; no game or save data. */
#include "dgt.h"

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
    struct DGTHash2Context ctx;

    CHECK(sizeof(struct DGTHash2Context) == 104, "DGTHash2Context is 104 bytes (%u)", (unsigned)sizeof(struct DGTHash2Context));
    CHECK(OFFSETOF(struct DGTHash2Context, h0) == 0, "h0 at 0 (%u)", OFFSETOF(struct DGTHash2Context, h0));
    CHECK(OFFSETOF(struct DGTHash2Context, Intermediate_Hash) == 0, "Intermediate_Hash at 0 (%u)", OFFSETOF(struct DGTHash2Context, Intermediate_Hash));
    CHECK(OFFSETOF(struct DGTHash2Context, Nl) == 20, "Nl at 20 (%u)", OFFSETOF(struct DGTHash2Context, Nl));
    CHECK(OFFSETOF(struct DGTHash2Context, Length_Low) == 20, "Length_Low at 20 (%u)", OFFSETOF(struct DGTHash2Context, Length_Low));
    CHECK(OFFSETOF(struct DGTHash2Context, Nh) == 24, "Nh at 24 (%u)", OFFSETOF(struct DGTHash2Context, Nh));
    CHECK(OFFSETOF(struct DGTHash2Context, Length_High) == 24, "Length_High at 24 (%u)", OFFSETOF(struct DGTHash2Context, Length_High));
    CHECK(OFFSETOF(struct DGTHash2Context, num) == 28, "num at 28 (%u)", OFFSETOF(struct DGTHash2Context, num));
    CHECK(OFFSETOF(struct DGTHash2Context, Message_Block_Index) == 28, "Message_Block_Index at 28 (%u)", OFFSETOF(struct DGTHash2Context, Message_Block_Index));
    CHECK(OFFSETOF(struct DGTHash2Context, data) == 32, "data at 32 (%u)", OFFSETOF(struct DGTHash2Context, data));
    CHECK(OFFSETOF(struct DGTHash2Context, Message_Block) == 32, "Message_Block at 32 (%u)", OFFSETOF(struct DGTHash2Context, Message_Block));
    CHECK(OFFSETOF(struct DGTHash2Context, dummy) == 96, "dummy at 96 (%u)", OFFSETOF(struct DGTHash2Context, dummy));
    CHECK(OFFSETOF(struct DGTHash2Context, Computed) == 96, "Computed at 96 (%u)", OFFSETOF(struct DGTHash2Context, Computed));
    CHECK(OFFSETOF(struct DGTHash2Context, Corrupted) == 100, "Corrupted at 100 (%u)", OFFSETOF(struct DGTHash2Context, Corrupted));

    /* Each pair aliases the same storage. */
    zero(&ctx, sizeof(ctx));
    ctx.h0 = 11;
    CHECK(ctx.Intermediate_Hash[0] == 11, "Intermediate_Hash[0] reads back h0 (%lu)", ctx.Intermediate_Hash[0]);
    ctx.Intermediate_Hash[4] = 22;
    CHECK(ctx.h4 == 22, "h4 reads back Intermediate_Hash[4] (%lu)", ctx.h4);
    ctx.Nl = 33;
    CHECK(ctx.Length_Low == 33, "Length_Low reads back Nl (%lu)", ctx.Length_Low);
    ctx.Length_High = 44;
    CHECK(ctx.Nh == 44, "Nh reads back Length_High (%lu)", ctx.Nh);
    ctx.num = 55;
    CHECK(ctx.Message_Block_Index == 55, "Message_Block_Index reads back num (%d)", ctx.Message_Block_Index);
    ctx.data[3] = 66;
    CHECK(ctx.Message_Block_Words[3] == 66, "Message_Block_Words[3] reads back data[3] (%lu)", ctx.Message_Block_Words[3]);
    ctx.Message_Block[7] = 77;
    CHECK(((unsigned char *)ctx.data)[7] == 77, "data byte 7 reads back Message_Block[7] (%u)", ((unsigned char *)ctx.data)[7]);
    ctx.dummy[1] = 88;
    CHECK(ctx.Corrupted == 88, "Corrupted reads back dummy[1] (%d)", ctx.Corrupted);
    ctx.Computed = 99;
    CHECK(ctx.dummy[0] == 99, "dummy[0] reads back Computed (%d)", ctx.dummy[0]);

    printf("gcc_fixes dgt_struct: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
