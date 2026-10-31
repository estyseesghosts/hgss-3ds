"""Host test for hgss/compat/src/msl_runtime.c (the MSL helper ABI).

The decompilation declares the MSL float/64-bit helpers with bit-pattern
arguments and returns (u32/s32/s64), because on the DS a float is an integer
register. The test is compiled against those exact declarations -- not the
convenient typed forms -- so it would fail if the implementation used a float
return on a host where floats travel in a different register/stack slot.

Synthetic values only; no game data and no bootstrapped checkout.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "hgss" / "compat" / "src" / "msl_runtime.c"
GCC = os.environ.get("HGSS_HOST_CC", "gcc-10")

TEST_C = r"""
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The ABI the game declares (wifi_plaza_instrument.c, pokewalker_ir.c,
 * plaza_game_mime_jr_top.c, pokeathlon_*.c, vct_audio.c). */
extern uint32_t _fadd(uint32_t, uint32_t);
extern uint32_t _fdiv(uint32_t, uint32_t);
extern uint32_t _fflt(int32_t);
extern int32_t _ffix(uint32_t);
extern uint32_t _f_lltof(int64_t);
extern uint32_t _f_ulltof(uint64_t);
extern int64_t _ll_mul(int64_t, int64_t);
extern uint64_t _ll_udiv(uint64_t, uint64_t);
extern int32_t _s32_div_f(int32_t, int32_t);
extern int strnicmp(const char *, const char *);
extern unsigned char __lower_mapC[];

static int checks, failures;

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

static void check_bits(uint32_t got, uint32_t want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL: %s: got 0x%08x, want 0x%08x\n", what, got, want);
    }
}

int main(void)
{
    /* 0.5f + 3.0f = 3.5f */
    check_bits(_fadd(0x3F000000u, 0x40400000u), 0x40600000u, "_fadd 0.5+3");
    /* 5.0f / 2.0f = 2.5f */
    check_bits(_fdiv(0x40A00000u, 0x40000000u), 0x40200000u, "_fdiv 5/2");

    /* int -> float, positive and negative */
    check_bits(_fflt(245760), 0x48700000u, "_fflt 245760");
    check_bits(_fflt(-7), 0xC0E00000u, "_fflt -7");
    check_bits(_fflt(0), 0x00000000u, "_fflt 0");

    /* float -> int truncates toward zero */
    check(_ffix(0x40600000u) == 3, "_ffix 3.5 -> 3");
    check(_ffix(0xC0600000u) == -3, "_ffix -3.5 -> -3");
    check(_ffix(0x3FC00000u) == 1, "_ffix 1.5 -> 1");
    check(_ffix(0xBF800000u) == -1, "_ffix -1.0 -> -1");
    check(_ffix(0x7FC00000u) == 0, "_ffix NaN -> 0");

    /* 64-bit integer -> float */
    check_bits(_f_lltof((int64_t)1 << 40), 0x53800000u, "_f_lltof 2^40");
    check_bits(_f_lltof(-1), 0xBF800000u, "_f_lltof -1");
    check_bits(_f_ulltof(0xFFFFFFFFFFFFFFFFu), 0x5F800000u, "_f_ulltof 2^64");
    check_bits(_f_ulltof(0x8000000000000000u), 0x5F000000u, "_f_ulltof 2^63");

    /* 64x64 -> 64 wrapping multiply */
    check(_ll_mul(-3, 7) == -21, "_ll_mul -3*7");
    check(_ll_mul(0x7FFFFFFFFFFFFFFFLL, 2) == -2, "_ll_mul wraps");
    check(_ll_mul(0, 0x123456789ABCDEFLL) == 0, "_ll_mul zero");

    /* unsigned 64/64 */
    check(_ll_udiv(100, 7) == 14, "_ll_udiv 100/7");
    check(_ll_udiv((uint64_t)1 << 63, (uint64_t)1 << 32) == ((uint64_t)1 << 31),
          "_ll_udiv 2^63/2^32");
    check(_ll_udiv(7, 0) == 0, "_ll_udiv zero divisor is documented 0");

    /* signed 32/32 truncates toward zero */
    check(_s32_div_f(-7, 2) == -3, "_s32_div_f -7/2");
    check(_s32_div_f(7, -2) == -3, "_s32_div_f 7/-2");
    check(_s32_div_f(-8, 2) == -4, "_s32_div_f -8/2");
    check(_s32_div_f(-2147483647 - 1, -1) == (-2147483647 - 1),
          "_s32_div_f INT_MIN/-1 wraps");
    check(_s32_div_f(5, 0) == 0, "_s32_div_f zero divisor is documented 0");

    /* MSL strnicmp is a full two-argument case-insensitive compare */
    check(strnicmp("AbC", "abc") == 0, "strnicmp equal ignoring case");
    check(strnicmp("abc", "abd") == -1, "strnicmp less");
    check(strnicmp("abd", "abc") == 1, "strnicmp greater");
    check(strnicmp("", "") == 0, "strnicmp empty");
    check(strnicmp("a", "ab") == -1, "strnicmp prefix");
    check(strnicmp("A", "a") == 0, "strnicmp single letter");

    /* ASCII-only lower-case table; high bytes pass through */
    check(__lower_mapC['A'] == 'a', "lower A");
    check(__lower_mapC['Z'] == 'z', "lower Z");
    check(__lower_mapC['@'] == '@', "lower @ identity");
    check(__lower_mapC[0x80] == 0x80, "lower high byte identity");
    check(__lower_mapC[0] == 0, "lower NUL identity");

    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
"""


class MslRuntimeTests(unittest.TestCase):
    def test_abi_through_the_declared_prototypes(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            test_c = tmp / "test_msl_runtime.c"
            test_c.write_text(TEST_C)
            program = tmp / "test_msl_runtime"
            compile_cmd = [GCC, "-m32", "-std=gnu99", "-Wall", "-Wextra",
                           "-O2", str(SOURCE), str(test_c), "-o", str(program)]
            proc = subprocess.run(compile_cmd, capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0,
                             f"compile failed:\n{proc.stdout}\n{proc.stderr}")
            self.assertNotIn("warning:", proc.stderr)
            run = subprocess.run([str(program)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r"\d+ checks, 0 failed")


if __name__ == "__main__":
    unittest.main()
