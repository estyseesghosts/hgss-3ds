"""Host test for hgss/game/app/cpp_runtime.c.

The lobby calls a few mangled C++ runtime names from C TUs. The port does not
link libstdc++, so this file supplies them. The test calls the exact mangled
symbols the game declares and checks the storage semantics; the abort path is
run as a child process so the test can observe the non-zero exit.

The two out-of-line destructors (_ZN5BytesD1Ev, _ZN5DequeD1Ev) are not in this
file: tools/object_aliases.py recovers them from the decomp's alias maps (see
the file header). The build run checks them with nm.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "hgss" / "game" / "app" / "cpp_runtime.c"
GCC = os.environ.get("HGSS_HOST_CC", "gcc-10")

SYMBOLS = ("_ZnamRKSt9nothrow_t", "_ZdaPv", "_ZSt7nothrow",
           "_ZN5BytesD1Ev", "_ZN5DequeD1Ev",
           "_ZNSt15__vector_commonILb1EE18throw_length_errorEv")

TEST_C = r"""
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void *_ZnamRKSt9nothrow_t(uint32_t size, const void *nothrow_tag);
extern void _ZdaPv(void *pointer);
extern char _ZSt7nothrow[];
extern void _ZN5BytesD1Ev(void *self);
extern void _ZN5DequeD1Ev(void *self);
extern void _ZNSt15__vector_commonILb1EE18throw_length_errorEv(void);

static int checks, failures;
static int vector_destroys, deque_destroys;

/* The lobby's C destroy routines the destructors forward to. */
void *LobbyCharVector_Destroy(void *vec)
{
    vector_destroys++;
    return vec;
}

void LobbyEventDeque_Destroy2(void *deque)
{
    deque_destroys++;
}

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

int main(int argc, char **argv)
{
    unsigned char *block;
    unsigned char *empty;
    int i, writable = 1;

    if (argc > 1 && strcmp(argv[1], "abort") == 0) {
        _ZNSt15__vector_commonILb1EE18throw_length_errorEv();
        return 0; /* the helper must not return */
    }

    block = (unsigned char *)_ZnamRKSt9nothrow_t(16, _ZSt7nothrow);
    check(block != NULL, "new[] returns a block");
    if (block != NULL) {
        for (i = 0; i < 16; i++) {
            block[i] = (unsigned char)i;
        }
        for (i = 0; i < 16; i++) {
            if (block[i] != (unsigned char)i) {
                writable = 0;
            }
        }
        check(writable, "new[] block is writable");
        _ZdaPv(block);
    }

    /* A zero-size request still gets a unique non-NULL pointer, and freeing
     * it is safe. */
    empty = (unsigned char *)_ZnamRKSt9nothrow_t(0, _ZSt7nothrow);
    check(empty != NULL, "new[](0) is non-NULL");
    _ZdaPv(empty);
    _ZdaPv(NULL);

    /* The tag object is one addressable byte. */
    _ZSt7nothrow[0] = _ZSt7nothrow[0];

    /* The destructors forward to the C destroy routines at offset 0. */
    _ZN5BytesD1Ev(&checks);
    _ZN5DequeD1Ev(&checks);
    check(vector_destroys == 1, "Bytes destructor calls the vector destroy");
    check(deque_destroys == 1, "Deque destructor calls the deque destroy");

    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
"""


class CppRuntimeTests(unittest.TestCase):
    def test_storage_symbols_and_throw_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            test_c = tmp / "test_cpp_runtime.c"
            test_c.write_text(TEST_C)
            obj = tmp / "cpp_runtime.o"
            program = tmp / "test_cpp_runtime"

            compile_obj = [GCC, "-m32", "-std=gnu99", "-Wall", "-Wextra",
                           "-O2", "-c", str(SOURCE), "-o", str(obj)]
            proc = subprocess.run(compile_obj, capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0,
                             f"compile failed:\n{proc.stdout}\n{proc.stderr}")
            self.assertNotIn("warning:", proc.stderr)

            # The exact mangled names must be global definitions, not
            # rewritten or hidden by a C++ compiler.
            nm = subprocess.run(["nm", "-g", "--defined-only", str(obj)],
                                capture_output=True, text=True, check=True)
            for symbol in SYMBOLS:
                self.assertRegex(nm.stdout, rf"(?m)^[0-9a-f]+ [TDBR] {symbol}$")

            link = [GCC, "-m32", str(obj), str(test_c), "-o", str(program)]
            proc = subprocess.run(link, capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0,
                             f"link failed:\n{proc.stdout}\n{proc.stderr}")

            run = subprocess.run([str(program)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r"\d+ checks, 0 failed")

            abort = subprocess.run([str(program), "abort"],
                                   capture_output=True, text=True)
            self.assertNotEqual(abort.returncode, 0,
                                "throw_length_error must abort")
            self.assertIn("throw_length_error", abort.stderr)


if __name__ == "__main__":
    unittest.main()
