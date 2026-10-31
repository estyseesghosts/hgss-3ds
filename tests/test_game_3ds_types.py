"""Synthetic regression for the game's SDK_PORT_3DS pointer-type fixes.

NitroSDK types s32/u32/fx32 as long; the game mixes them with int locals.
GCC 14 (devkitARM 16.1) rejects that with -Wincompatible-pointer-types, so
the 17xx patches retype the locals or the .c-local prototypes under
SDK_PORT_3DS. These tests compile tiny translation units with the same
diagnostic and check both the corrected and the pre-fix pattern.

No game data is needed: the headers below reproduce the SDK's type contract.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path

CC = "gcc"  # the image's default host compiler (gcc-13); any C99 compiler works
FLAGS = [
    "-std=gnu99",
    "-fsigned-char",
    "-fno-strict-aliasing",
    "-fgnu89-inline",
    "-Werror=incompatible-pointer-types",
    "-Werror=implicit-int",
    "-c",
]

# The NitroSDK type aliases on the 3DS: long, not int.
TYPES_H = """\
typedef signed long s32;
typedef unsigned long u32;
typedef signed long fx32;
typedef int BOOL;
#define FX32_ONE ((fx32)0x1000)
"""

# The helpers these fixes meet: out parameters typed with the SDK's words.
API_H = """\
#include "types.h"
void Course_GroundToCenter(s32 x, s32 y, s32 *outX, s32 *outY);
void Course_GetCollisionCircle(fx32 x, fx32 y, fx32 *outX, fx32 *outY, s32 *radius);
void Touch_GetHeldCoords(u32 *x, u32 *y);
void Text_GetPosition(int *x, int *y);
"""

# The pre-fix shape: int locals passed to s32/fx32 out parameters.
UNFIXED_C = """\
#include "api.h"
void f(void) {
    int x, y;
    Course_GroundToCenter(1, 2, &x, &y);
}
"""

# The fix pattern used by the patches: the callee's exact type under
# SDK_PORT_3DS, the original DS text under #else.
FIXED_LOCALS_C = """\
#include "api.h"
void ground(void) {
#ifdef SDK_PORT_3DS
    s32 x, y;
#else
    int x, y;
#endif
    Course_GroundToCenter(1, 2, &x, &y);
}
void collision(void) {
#ifdef SDK_PORT_3DS
    fx32 cx, cy;
    s32 radius;
#else
    int cx, cy, radius;
#endif
    Course_GetCollisionCircle(0, 0, &cx, &cy, &radius);
}
void touch(void) {
#ifdef SDK_PORT_3DS
    u32 x, y;
#else
    int x, y;
#endif
    Touch_GetHeldCoords(&x, &y);
}
"""

# A helper whose MWCC-only callers use int*, fixed by retyping the local
# prototype (pennant_capture_field.c) and the frame fields it fills.
FIXED_PROTOTYPE_C = """\
#include "types.h"
void Course_GroundToCenter(s32 x, s32 y, s32 *outX, s32 *outY);
typedef struct Frame {
#ifdef SDK_PORT_3DS
    s32 x, y;
#else
    int x, y;
#endif
} Frame;
#ifdef SDK_PORT_3DS
void Local_GroundToCenter(s32 x, s32 y, s32 *outX, s32 *outY);
#else
void Local_GroundToCenter(int x, int y, int *outX, int *outY);
#endif
void use(void) {
    Frame frame;
    Local_GroundToCenter(1, 2, &frame.x, &frame.y);
}
"""


def compile_tu(source, define_3ds, tmp):
    d = Path(tmp)
    (d / "types.h").write_text(TYPES_H)
    (d / "api.h").write_text(API_H)
    src = d / "tu.c"
    src.write_text(source)
    cmd = [CC, *FLAGS, "-I", str(d)]
    if define_3ds:
        cmd.append("-DSDK_PORT_3DS")
    cmd += [str(src), "-o", str(d / "tu.o")]
    return subprocess.run(cmd, capture_output=True, text=True)


class Game3dsTypeContractTest(unittest.TestCase):
    def test_unfixed_int_locals_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = compile_tu(UNFIXED_C, True, tmp)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("incompatible-pointer-types", r.stderr)
        self.assertIn("s32 *", r.stderr)

    def test_retyped_locals_compile(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = compile_tu(FIXED_LOCALS_C, True, tmp)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_retyped_local_prototype_and_fields_compile(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = compile_tu(FIXED_PROTOTYPE_C, True, tmp)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_guards_keep_the_ds_text_out_of_the_3ds_compile(self):
        # Without SDK_PORT_3DS the original int/int* text is selected; with
        # it, the s32 text. Preprocess both and check the branches differ.
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "types.h").write_text(TYPES_H)
            (d / "api.h").write_text(API_H)
            (d / "tu.c").write_text(FIXED_LOCALS_C)
            base = [CC, "-E", "-P", "-I", str(d), str(d / "tu.c")]
            ds = subprocess.run(base, capture_output=True, text=True)
            three = subprocess.run(base + ["-DSDK_PORT_3DS"], capture_output=True, text=True)
        self.assertEqual(ds.returncode, 0, ds.stderr)
        self.assertEqual(three.returncode, 0, three.stderr)
        self.assertIn("int x, y;", ds.stdout)
        self.assertIn("s32 x, y;", three.stdout)
        self.assertNotEqual(ds.stdout, three.stdout)


if __name__ == "__main__":
    unittest.main()
