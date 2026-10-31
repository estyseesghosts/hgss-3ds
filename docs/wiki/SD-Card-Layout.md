# SD card layout

The port reads **no cartridge**: HeartGold/SoulSilver's files come from a
`heartgold3ds.pak` you build on your computer from your own verified `.nds`.
The pack and its `manifest.json` live on the SD card next to the save and the
log. Nothing the builder produces from a ROM is ever committed to this
repository (see [Contributing](Contributing.md)).

## The folders

HeartGold and SoulSilver use separate folders so a save can never be read by
the other version.

| Game | Folder | Data pack | Save | Log |
| --- | --- | --- | --- | --- |
| HeartGold (USA) | `sdmc:/3ds/pokeheartgold/` | `heartgold3ds.pak` | `heartgold.sav` | `log.txt` |
| SoulSilver (USA) | `sdmc:/3ds/pokeheartgold-ss/` | `soulsilver3ds.pak` | `soulsilver.sav` | `log.txt` |

Both folders may also contain:

| File | What it is |
| --- | --- |
| `manifest.json` | The pack's index sidecar: the ROM's SHA-1, and each file's original NitroFS ID, path and size. Written by the extractor; copy it **with** the pack. |
| `port.ini` | Optional per-installation settings for the shell (read in a later phase; the Phase 2 shell starts with the console's profile). |

The save is a raw **524,288-byte** file, exactly as an emulator keeps it, so
a save can be moved between the port, an emulator and a flash card with no
conversion. The save layout never changes with the pack.

The three files go together: a pack without its `manifest.json` opens as
"can't read the data pack or its manifest.json", and a manifest from another
build is rejected as "damaged, or they don't belong together".

> **SoulSilver status.** The builder accepts the SoulSilver US dump
> (`f8dc38ea…`, see below), but there is no SoulSilver ROM in this
> workspace and no SoulSilver shell yet: the current 3DS shell
> (`hgss/game/app/main_3ds.c`) hardcodes the HeartGold folder and file names.
> A SoulSilver build has to set its own `DATA_DIR`, pack and save constants
> (the proposed names are in the table). Until then, SoulSilver extraction
> and packing are tested with synthetic NitroFS images only.

## Building the pack

Run these inside the dev container, against a directory you own. The ROM is
only read; the output is private (`/work` is not in the repository).

```sh
# 1. Verify the dump. Exit 0, or a reason (trimmed, patched, wrong region).
python3 builder/verify_rom.py /private/heartgold-retail.nds
# OK: Pokémon HeartGold (USA)

# 2. Extract NitroFS: every FAT file, including the 129 overlays, plus
#    manifest.json. ~126 MB for the retail US image.
python3 builder/extract_rom.py /private/heartgold-retail.nds -o /work/hg-data
# Extracted 513 files (129 overlays, 0 unnamed) totalling 125549829 bytes …

# 3. Pack. --build-id heartgold-nitrofs-1 is what the program checks at
#    boot; it is also the builder's default.
python3 builder/pack.py /work/hg-data -o /work/heartgold3ds.pak \
    --build-id heartgold-nitrofs-1 --rom /private/heartgold-retail.nds
```

Supported dumps (nothing else is accepted; other regions/revisions have a
different file table):

| Version | SHA-1 |
| --- | --- |
| Pokémon HeartGold (USA) | `4fcded0e2713dc03929845de631d0932ea2b5a37` |
| Pokémon SoulSilver (USA) | `f8dc38ea20c17541a43b58c5e6d18c1732c7e582` |

Copy `heartgold3ds.pak` and `/work/hg-data/manifest.json` to
`sdmc:/3ds/pokeheartgold/`. The extractor's `manifest.json` is written next
to the extracted files, so it is already in `/work/hg-data/`.

### The pak is ID-indexed

The pack keeps each file's **original FAT ID**, not a path. Some game code
opens files by index (`FS_OpenFileFast` with a NitroFS ID), so renumbering
would break it. `manifest.json` supplies the path → ID mapping for the rest;
the reader never trusts one without the other.

The 129 ARM9 overlays in the retail image are FAT entries **without names**
(IDs 0–128); the extractor names them `overlay/overlay_NNNN.bin` from the
header's overlay table. The port answers `FS_LoadOverlayImage` from the
linked overlay image, but the pack still contains them so the pack is the
ROM's complete file table.

## Running on a PC

The PC build takes the same pack and manifest:

```sh
build/pokeheartgold-pc /work/heartgold3ds.pak /work/hg-data/manifest.json --frames N
```

`--frames N` stops after N frames; without it the runner keeps going until
the game stops. The save is `heartgold.sav` in the current directory unless
`--save FILE` is given. Everything the port logs goes to stderr.

`pokeheartgold-pc --check-overlays` is the separate overlay self-check and
does not need a pack.

## Boot-time checks and their messages

`Heartgold_OpenPak` (in `hgss/game/app/heartgold.c`) maps the reader's
result to one clear line, shown by the 3DS shell and printed by the PC one:

| Cause | What you see |
| --- | --- |
| Pack or manifest missing/unreadable | can't read the data pack or its manifest.json |
| Truncated, corrupt, or manifest/pack mismatch | the data pack or manifest.json is damaged, or they don't belong together |
| Made by another builder version | the data pack was made by a different version of the builder |
| Wrong `--build-id` | the data pack has the wrong build ID; pack it again with `--build-id heartgold-nitrofs-1` |
| ROM SHA-1 not in the reader's table | the data pack was made from a ROM this version doesn't support |
| Out of memory | out of memory opening the data pack |

The reader's ROM allowlist is HGSS's, not Platinum's, without editing the
vendored tree: `tools/hgss_game.py` generates a build-private copy of
`3ds_port/pak/src/pak.c` with the two digests above (from
`builder/verify_rom.py`) prepended, compiles it as a program object and links
it before `libnitro.a`; the archive's own `pak_reader.o` member is then never
loaded. `3ds_port/` stays byte-identical to `platinum.lock`. The host boot
test `tests/sdk/pak_boot` covers the OK path and every other result.

## Read cache

The reader keeps an 8 KB-block cache of the pack (4 MB of it) because a
field load is ~1,900 small reads and each SD read is ~1 ms. This is the
vendored port's cache, unchanged; its size is measured on Platinum and will
be re-measured on field transitions in Phase 8, not tuned here.

## Deferred

Two optional items from the plan are **not** part of Phase 3 and are not
implemented:

- **Web builder.** Platinum has a Pyodide builder that runs in a browser;
  adapting it waits until this Python builder is stable.
- **CIA icon and banner.** Made from the user's cart banner
  (`home_menu_art.py`, `banner3d`); it needs a packaged `.3dsx`/CIA first
  (Phase 5) and is never committed.
