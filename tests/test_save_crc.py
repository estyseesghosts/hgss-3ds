"""Guard the save checksum's table from the port's header remap.

HGSS's ``GF_CRC16Init`` builds the table ``GF_CalcCRC16`` uses, and
``GF_CalcCRC16`` calls ``MATH_CalcCRC16CCITT``. On the DS,
``MATH_CRC16InitTable`` builds the CCITT table. The port's compatibility
header maps that name to ntrtwl's reflected table by default, so without the
escape hatch the game built a reflected table and then updated it MSB-first:
a deterministic CRC that is not CRC-16/CCITT.

Every save footer was therefore rejected -- a valid cartridge or emulator
save showed as no save, and a save written by the port carried a footer the
original ROM rejects. Patch 1906 defines
``HGSS_MATH_KEEP_HGSS_CRC16_INIT`` before ``math_util.c`` includes anything,
which keeps HGSS's inline for that translation unit.

The same builds wrote the save's per-substruct CRCs with the wrong table too.
The slot footer was the only CRC the loader checked, so such a save loaded,
but the first ``Save_*_Get`` on a substruct asserted; the main menu's beacon
watch leaves a CommState alive, so that assert drew the game's communication
error screen. Patch 1907 accepts and rewrites those legacy substruct CRCs on
first read.

This check runs only after ``tools/bootstrap.py`` (``make test`` can run
before a checkout exists), and is skipped then, like the HW-address scan.
"""
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ROOT = REPO / "build" / "upstream"


@unittest.skipUnless((ROOT / "src" / "math_util.c").is_file(), "needs build/upstream")
class SaveCrcTableTests(unittest.TestCase):
    def test_math_util_keeps_hgss_crc16_before_its_first_include(self):
        text = (ROOT / "src" / "math_util.c").read_text(encoding="utf-8")
        first_include = text.find("#include")
        self.assertGreaterEqual(first_include, 0)
        prefix = text[:first_include]
        self.assertIn("#ifdef SDK_PORT_3DS", prefix)
        self.assertIn("#define HGSS_MATH_KEEP_HGSS_CRC16_INIT", prefix)

    def test_patch_stack_has_the_hgss_crc16_escape_hatch(self):
        patches = sorted((REPO / "patches" / "pokeheartgold").glob("1906-*.patch"))
        self.assertEqual(len(patches), 1)
        patch = patches[0].read_text(encoding="utf-8")
        self.assertIn("+#define HGSS_MATH_KEEP_HGSS_CRC16_INIT", patch)

    def test_save_substructs_migrate_legacy_port_crcs(self):
        text = (ROOT / "src" / "save.c").read_text(encoding="utf-8")
        self.assertIn("SaveSubstruct_LegacyCRC", text)
        self.assertIn("data_u16[size / 2] = crc;", text)
        self.assertIn("if (SaveSubstruct_LegacyCRC(data, size) == data_u16[size / 2])", text)

    def test_patch_stack_has_the_substruct_migration(self):
        patches = sorted((REPO / "patches" / "pokeheartgold").glob("1907-*.patch"))
        self.assertEqual(len(patches), 1)
        patch = patches[0].read_text(encoding="utf-8")
        self.assertIn("+static u16 SaveSubstruct_LegacyCRC", patch)


if __name__ == "__main__":
    unittest.main()
