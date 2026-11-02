"""The player-facing scripts: names, game detection and small helpers.

No ROM and no build: these tests only use the mappings and pure functions, so
they run in the normal `make test` pass.
"""
import json
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))

import _util  # noqa: E402
import build_3dsx  # noqa: E402
import prepare_game  # noqa: E402
import setup_toolchain  # noqa: E402

SD_DOC = REPO / "docs" / "wiki" / "SD-Card-Layout.md"
HEARTGOLD_HEADER = REPO / "hgss" / "game" / "app" / "heartgold.h"


class GameNames(unittest.TestCase):
    def test_documented_in_the_sd_layout(self):
        text = SD_DOC.read_text(encoding="utf-8")
        for game in prepare_game.GAMES.values():
            self.assertIn(f"3ds/{game['folder']}/", text)
            self.assertIn(game["pak"], text)
            self.assertIn(game["save"], text)

    def test_heartgold_build_id_matches_the_program(self):
        text = HEARTGOLD_HEADER.read_text(encoding="utf-8")
        self.assertIn(
            f'#define HEARTGOLD_PAK_BUILD_ID "{prepare_game.GAMES["HeartGold"]["build_id"]}"',
            text,
        )

    def test_pak_name_selects_the_game(self):
        game, info = build_3dsx.game_from_pak(Path("build/heartgold3ds.pak"))
        self.assertEqual(game, "HeartGold")
        self.assertEqual(info["folder"], "pokeheartgold")
        game, info = build_3dsx.game_from_pak(Path("build/soulsilver3ds.pak"))
        self.assertEqual(game, "SoulSilver")
        self.assertEqual(info["pak"], "soulsilver3ds.pak")


class Helpers(unittest.TestCase):
    def test_human_size(self):
        self.assertEqual(_util.human_size(512), "512 B")
        self.assertEqual(_util.human_size(1024), "1.0 KiB")
        self.assertEqual(_util.human_size(9_279_628), "8.8 MiB")

    def test_manifest_must_name_the_same_rom(self):
        with tempfile.TemporaryDirectory() as tmp:
            manifest = Path(tmp) / "manifest.json"
            manifest.write_text(json.dumps({"rom_sha1": "a" * 40}), encoding="utf-8")
            self.assertTrue(prepare_game.manifest_matches(manifest, "a" * 40))
            self.assertFalse(prepare_game.manifest_matches(manifest, "b" * 40))

    def test_a_broken_manifest_is_not_reused(self):
        with tempfile.TemporaryDirectory() as tmp:
            manifest = Path(tmp) / "manifest.json"
            manifest.write_text("not json", encoding="utf-8")
            self.assertFalse(prepare_game.manifest_matches(manifest, "a" * 40))
            self.assertFalse(prepare_game.manifest_matches(Path(tmp) / "missing.json", "a" * 40))

    def test_manifest_is_found_next_to_the_pack(self):
        with tempfile.TemporaryDirectory() as tmp:
            pak = Path(tmp) / "heartgold3ds.pak"
            pak.write_bytes(b"")
            manifest = Path(tmp) / "manifest.json"
            manifest.write_text("{}", encoding="utf-8")
            found = build_3dsx.find_manifest(
                pak, prepare_game.GAMES["HeartGold"], None
            )
            self.assertEqual(found, manifest.resolve())


class ToolchainList(unittest.TestCase):
    def test_the_packager_and_shader_tools_are_required(self):
        for tool in ("arm-none-eabi-gcc", "3dsxtool", "smdhtool", "picasso", "bin2s", "make"):
            self.assertIn(tool, setup_toolchain.REQUIRED)

    def test_child_builds_get_devkitpro_on_path(self):
        env = _util.tool_env()
        self.assertIn("DEVKITPRO", env)
        self.assertIn("PATH", env)


if __name__ == "__main__":
    unittest.main()
