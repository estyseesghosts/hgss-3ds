"""Native tests of the battle GPU-background veto.

Builds the actual wide-layout file with the same isolated SDK typedef
adaptation as the renderer fixture (no production header is changed) and
checks that battles nominate no GPU quads while the field keeps its takes.
"""
import os
from pathlib import Path
import shlex
import subprocess
import unittest

REPO = Path(__file__).resolve().parents[1]
SUPPORT = Path(__file__).resolve().parent
UPSTREAM = REPO / "build" / "upstream"


class WideBattleTests(unittest.TestCase):
    def test_battle_veto(self):
        out = REPO / "build" / "wide-battle-native"
        inc = out / "include" / "nitro"
        inc.mkdir(parents=True, exist_ok=True)
        types = (UPSTREAM / "lib/include/nitro/types.h").read_text()
        self.assertIn("typedef unsigned long u32;", types)
        (inc / "types.h").write_text(types.replace(
            "typedef unsigned long u32;", "typedef unsigned int u32;").replace(
            "typedef signed long s32;", "typedef signed int s32;"))
        for name in ("nitro.h", "nnsys", "nnsys.h", "cw", "cw.h"):
            link = inc.parent / name
            if not link.exists():
                link.symlink_to(UPSTREAM / "lib/include" / name)
        for header in (UPSTREAM / "lib/include/nitro").iterdir():
            link = inc / header.name
            if header.name != "types.h" and not link.exists():
                link.symlink_to(header)
        flags = ["-std=gnu99", "-O2", "-fno-strict-aliasing", "-fno-short-enums",
                 "-fsigned-char", "-fwrapv",
                 "-DSDK_ARM9", "-DSDK_TS", "-DSDK_4M", "-DSDK_CODE_ARM",
                 "-DSDK_FINALROM", "-DNNS_FINALROM", "-DSDK_PORT_3DS",
                 "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
                 "-Wno-unknown-pragmas", "-Wno-deprecated-non-prototype",
                 "-I" + str(inc.parent), "-I" + str(REPO / "3ds_port/nitro/include"),
                 "-I" + str(UPSTREAM / "include"),
                 '-DWIDE_SOURCE="' + str(REPO / "3ds_port/nitro/src/port_wide.c") + '"']
        flags += shlex.split(os.environ.get("HGSS_WIDE_TEST_CFLAGS", ""))
        exe = out / "wide-battle-test"
        subprocess.run([os.environ.get("CC", "cc"), *flags,
                        str(SUPPORT / "fixtures/wide_battle_nomination.c"),
                        "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
