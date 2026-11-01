# Notice

## What this project licenses

Only the original code, tools and documentation written for this project are
covered by this project's license, the GNU General Public License v3.0 (see
[`LICENSE`](LICENSE)).

## What it does not

- **The game.** Pokémon HeartGold/SoulSilver and all of their content belong to
  Nintendo, Game Freak, Creatures and The Pokémon Company. None of it is
  included here. Players supply their own legally dumped cartridge.
- **The decompilation.** ctrl-cheeb-del/pokeheartgold and pret/pokeheartgold
  are separate projects with their own terms. They are downloaded at build
  time, not redistributed here.
- **The DS toolchain.** The Metrowerks compiler (`mwccarm`) and the Nitro SDK
  tools are proprietary. They are used only for the byte-matching reference
  build, only from the user's own copies, and are never in this repository, in
  an image layer, or in CI artifacts.
- **Third-party libraries.** devkitARM, libctru and citro3d keep their own
  licenses.

## Code reused from other projects

`3ds_port/` and part of `tools/` are vendored from
[GabeSud0/pokeplatinum-3ds](https://github.com/GabeSud0/pokeplatinum-3ds)
(GPL-3.0) at the commit named in `platinum.lock`. Their copyright notices and
license headers are kept. HGSS-specific changes go in new files, or in patches
intended for upstream.

The vendored copy has **one deliberate local change**, the camera-point
`to_screen()` hunk in `nitro/src/port_gx3d.c` plus its test; it is recorded in
[`3ds_port/LOCAL-CHANGES.md`](3ds_port/LOCAL-CHANGES.md) and
[`docs/upstream/platinum-camera-point.patch`](docs/upstream/platinum-camera-point.patch),
and is the content of an upstream PR. Everything else in `3ds_port/` is the
pinned Platinum tree.

## Rules for contributors

- Never commit ROMs, extracted assets, golden screenshots, save files, MWCC or
  Nitro SDK files, or built game binaries.
- Never add code or assets copied from leaked source code, or from ports that
  bundle the game's assets.
- Keep changes to the decompilation as patch files in `patches/`.

## License

GPL-3.0, like the Platinum port it builds on. By contributing, you agree that
your contributions are licensed the same way.
