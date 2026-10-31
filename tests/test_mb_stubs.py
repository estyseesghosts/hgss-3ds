"""Host test for hgss/game/stubs/mb_stubs.c.

The DS download-play (multiboot) calls have no PC/3DS equivalent. Each stub
must answer the documented "unavailable" value and must never touch the
buffers or callbacks the caller passes. Synthetic data only.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "hgss" / "game" / "stubs" / "mb_stubs.c"
GCC = os.environ.get("HGSS_HOST_CC", "gcc-10")

TEST_C = r"""
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct FSFile FSFile;
extern int MB_Init(void *, void *, uint32_t, uint32_t, uint32_t);
extern void MB_End(void);
extern void MB_SetParentCommParam(uint16_t, uint16_t);
extern void MB_CommSetParentStateCallback(void *);
extern int MB_StartParentFromIdle(uint32_t);
extern uint32_t MB_GetSegmentLength(FSFile *);
extern int MB_ReadSegment(FSFile *, void *, uint32_t);
extern int MB_RegisterFile(const void *, void *);
extern int MB_CommResponseRequest(uint16_t, uint32_t);
extern void MB_DisconnectChild(uint16_t);
extern int MB_CommIsBootable(uint16_t);
extern void *MB_CommGetChildUser(uint16_t);

static int checks, failures;

static void check(int condition, const char *what)
{
    checks++;
    if (!condition) {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

static void callback(void)
{
    check(0, "state callback must never be called");
}

int main(void)
{
    unsigned char buffer[16];
    long page = sysconf(_SC_PAGESIZE);
    unsigned char *guard = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    memset(buffer, 0x5A, sizeof buffer);

    /* A protected page as every pointer argument: a stub that reads or writes
     * any of them faults instead of quietly returning a value. */
    check(mprotect(guard, (size_t)page, PROT_NONE) == 0, "mprotect guard");

    check(MB_Init(guard, guard, 1, 2, 3) != 0, "MB_Init reports failure");
    MB_End();
    MB_SetParentCommParam(0x100, 1);
    MB_CommSetParentStateCallback((void *)&callback);
    check(MB_StartParentFromIdle(0) != 0, "MB_StartParentFromIdle fails");
    check(MB_GetSegmentLength((FSFile *)guard) == 0, "MB_GetSegmentLength 0");
    check(MB_ReadSegment((FSFile *)guard, buffer, sizeof buffer) == 0,
          "MB_ReadSegment FALSE");
    check(MB_ReadSegment((FSFile *)guard, guard, 0x10000) == 0,
          "MB_ReadSegment FALSE with guard buffer");
    check(MB_RegisterFile(guard, guard) == 0, "MB_RegisterFile FALSE");
    check(MB_CommResponseRequest(1, 0) == 0, "MB_CommResponseRequest FALSE");
    MB_DisconnectChild(1);
    check(MB_CommIsBootable(1) == 0, "MB_CommIsBootable FALSE");
    check(MB_CommGetChildUser(1) == NULL, "MB_CommGetChildUser NULL");

    /* The buffer was never written. */
    {
        int i, untouched = 1;
        for (i = 0; i < (int)sizeof buffer; i++) {
            if (buffer[i] != 0x5A) {
                untouched = 0;
            }
        }
        check(untouched, "MB_ReadSegment does not write its buffer");
    }

    mprotect(guard, (size_t)page, PROT_READ | PROT_WRITE);
    munmap(guard, (size_t)page);

    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
"""


class MbStubTests(unittest.TestCase):
    def test_documented_failure_values_and_no_memory_access(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            test_c = tmp / "test_mb_stubs.c"
            test_c.write_text(TEST_C)
            program = tmp / "test_mb_stubs"
            compile_cmd = [GCC, "-m32", "-std=gnu99", "-Wall", "-Wextra",
                           "-O2", str(SOURCE), str(test_c), "-o", str(program)]
            proc = subprocess.run(compile_cmd, capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0,
                             f"compile failed:\n{proc.stdout}\n{proc.stderr}")
            run = subprocess.run([str(program)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r"\d+ checks, 0 failed")


if __name__ == "__main__":
    unittest.main()
