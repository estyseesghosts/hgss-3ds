/* Host test: HGSS_MATH_KEEP_HGSS_CRC16_INIT keeps MATH_CRC16InitTable as the
 * table GF_CalcCRC16 uses.
 *
 * The port's compat nitro/math/crc.h maps MATH_CRC16InitTable to ntrtwl's
 * reflected table (the one MATH_CalcCRC16 updates with), while HGSS's
 * src/math_util.c builds that table and then updates it through
 * MATH_CalcCRC16CCITT. That pair is not a CRC at all: saves from the early
 * 3DS builds had footers the game and emulators rejected. The escape hatch
 * must build the CCITT table. Synthetic input only ("123456789", the CRC
 * catalogue's check string). MATH_crc.c is linked in. */
#define HGSS_MATH_KEEP_HGSS_CRC16_INIT
#include <nitro/types.h>
#include <nitro/math/crc.h>

int printf(const char *, ...); /* lib/include shadows the system headers; see tests/sdk/irq_handler */

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    static const char check[] = "123456789";
    const u8 *msg = (const u8 *)check;
    const u32 n = sizeof(check) - 1;
    MATHCRC16Table table;

    MATH_CRC16InitTable(&table);
    CHECK(table.table[1] == 0x1021, "table[1] is the CCITT poly (%04x)", (unsigned)table.table[1]);
    CHECK(MATH_CalcCRC16CCITT(&table, msg, n) == 0x29B1,
          "MATH_CalcCRC16CCITT gives 0x29B1 (%04x)", (unsigned)MATH_CalcCRC16CCITT(&table, msg, n));

    printf("gcc_fixes hgss crc table: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
