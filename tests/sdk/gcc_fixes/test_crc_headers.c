/* Host test: nitro/math/crc.h and MATH_crc.h in one translation unit (both are included by the
 * port's gx3d sources and by nitro/math.h). Under SDK_PORT_3DS the flat MATH_crc.h takes its
 * structs, typedefs and MATH_CRC16InitTable from the public header; this checks the two agree
 * and that the table-driven CRC through them matches a bit-by-bit reference.
 * Synthetic input only ("123456789", the CRC catalogue's check string). MATH_crc.c is linked in. */
#include <nitro/types.h>
#include "MATH_crc.h"
#include <nitro/math/crc.h>
#include <nitro.h>

int printf(const char *, ...); /* lib/include shadows the system headers; see tests/sdk/irq_handler */

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* CRC-16/CCITT-FALSE, bit by bit: poly 0x1021, init 0xffff, no reflection, no final xor. */
static u16 reference_crc16_ccitt(const u8 *p, u32 n) {
    u16 crc = 0xffff;
    u32 i;
    int b;
    for (i = 0; i < n; i++) {
        crc ^= (u16)((u16)p[i] << 8);
        for (b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (u16)((crc << 1) ^ MATH_CRC16_CCITT_POLY) : (u16)(crc << 1);
        }
    }
    return crc;
}

int main(void) {
    static const char check[] = "123456789";
    const u8 *msg = (const u8 *)check;
    const u32 n = sizeof(check) - 1;
    MATHCRC16Table table;
    const struct MATHCRC16Table *tp = &table;
    MATHCRC16Context ctx;
    u16 ref = reference_crc16_ccitt(msg, n);

    CHECK(sizeof(struct MATHCRC8Table) == 256, "MATHCRC8Table is 256 bytes (%u)", (unsigned)sizeof(struct MATHCRC8Table));
    CHECK(sizeof(struct MATHCRC16Table) == 512, "MATHCRC16Table is 512 bytes (%u)", (unsigned)sizeof(struct MATHCRC16Table));
    CHECK(sizeof(MATHCRC32Table) == 1024, "MATHCRC32Table is 1024 bytes (%u)", (unsigned)sizeof(MATHCRC32Table));
    CHECK(ref == 0x29B1, "reference CRC-16/CCITT-FALSE of 123456789 is 0x29B1 (got %04x)", (unsigned)ref);

    MATH_CRC16InitTable(&table);
    CHECK(table.table[1] == 0x1021, "table[1] is the poly (%04x)", (unsigned)table.table[1]);
    CHECK(MATH_CalcCRC16CCITT(tp, msg, n) == ref, "MATH_CalcCRC16CCITT through the flat struct tag");

    MATH_CRC16Init(&ctx);
    MATH_CRC16Update(&table, &ctx, msg, 4);
    MATH_CRC16Update(&table, &ctx, msg + 4, n - 4);
    CHECK(MATH_CRC16GetHash(&ctx) == ref, "incremental MATH_CRC16Update gives the same CRC (%04x)", (unsigned)MATH_CRC16GetHash(&ctx));

    printf("gcc_fixes crc headers: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
