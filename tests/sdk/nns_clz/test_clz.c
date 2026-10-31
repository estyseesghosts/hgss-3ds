/* Host check for the GCC CountLeadingZeros helpers in the NitroSystem g2d sources.
 *
 * run.sh extracts each helper, unchanged, from its patched source and includes it here as
 * CLZ_SNIPPET. The snippet must compile with -DSDK_PORT_3DS; that selects the GCC branch and
 * never the MWCC asm. Expected values follow the ARM CLZ instruction: a zero input gives 32.
 */
#include <stdio.h>

typedef unsigned int u32;

#include CLZ_SNIPPET

static int failures = 0;

#define CHECK_EQ(actual, expected, what)                                                      \
    do {                                                                                      \
        u32 got_ = (u32)(actual);                                                             \
        u32 want_ = (u32)(expected);                                                          \
        if (got_ != want_) {                                                                  \
            printf("FAIL %s: %s -> %u, want %u\n", CLZ_NAME, what, got_, want_);              \
            failures++;                                                                       \
        }                                                                                     \
    } while (0)

/* Reference: the position of the highest set bit counted from the top, 32 for zero. */
static u32 ref_clz(u32 x) {
    u32 n = 0;
    if (x == 0) {
        return 32;
    }
    while (!(x & 0x80000000u)) {
        x <<= 1;
        n++;
    }
    return n;
}

int main(void) {
    u32 i;
    u32 seed = 12345;

    CHECK_EQ(CountLeadingZeros(0u), 32, "0");
    CHECK_EQ(CountLeadingZeros(1u), 31, "1");
    CHECK_EQ(CountLeadingZeros(0x80000000u), 0, "0x80000000");
    CHECK_EQ(CountLeadingZeros(0xFFFFFFFFu), 0, "0xFFFFFFFF");
    CHECK_EQ(CountLeadingZeros(3u), 30, "3");
    CHECK_EQ(CountLeadingZeros(0x7FFFFFFFu), 1, "0x7FFFFFFF");

    for (i = 0; i < 32; i++) {
        u32 bit = 1u << i;
        char what[32];
        snprintf(what, sizeof(what), "single bit %u", (unsigned)i);
        CHECK_EQ(CountLeadingZeros(bit), 31 - i, what);
    }

    /* A fixed pseudo-random sweep against the reference loop. */
    for (i = 0; i < 100000; i++) {
        seed = seed * 1103515245u + 12345u;
        CHECK_EQ(CountLeadingZeros(seed), ref_clz(seed), "random value");
    }

    if (failures) {
        printf("%s: %d failure(s)\n", CLZ_NAME, failures);
        return 1;
    }
    printf("%s: ok\n", CLZ_NAME);
    return 0;
}
