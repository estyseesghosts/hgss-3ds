# Player's guide: build and play pokeheartgold-3ds

This page is for someone who wants to **play** the port, not work on it. It
takes you from a fresh computer and your own game cartridge dump to a folder
you copy onto a 3DS SD card.

Everything here is done with three small Python scripts in `scripts/`. They
are ordinary Python 3 (no bash, no WSL on Linux/macOS), and they run the same
way on Linux and macOS. Windows builds natively too — same scripts, plus a
one-time toolchain install — see [WINDOWS.md](WINDOWS.md).

> **What you need**
>
> - A **3DS, 3DS XL, 2DS, New 3DS or New 2DS** with the Homebrew Launcher
>   working. It runs on both models: the Old 3DS is the measured baseline
>   (and the model the performance work targets), and a New 3DS has more room.
> - Your **own** US copy of Pokémon HeartGold or SoulSilver, dumped as an
>   `.nds` file. Only clean, unmodified US dumps are accepted. The scripts
>   never upload it and never change it.
> - An SD card reader and a few GB free (the game folder and the pack are
>   about 250 MB; the build's object files need a few GB more).
> - Python 3.9 or newer, `git`, and an internet connection for the first build.

> **What to expect when you play it.** The game boots and plays all the way
> through — field, battles, menus, both screens, audio. It is **not fast yet**:
> on an Old 3DS the field presents about six frames a second today, because
> the software drawer on the ARM11 is the bottleneck (the GPU is nearly idle).
> A New 3DS does better. Making it faster is the project's current work, not a
> setup problem on your end.

> **Do not share the files the scripts create from your ROM.** The `.pak` and
> the extracted game files are copyrighted game data. They are ignored by git
> and must stay on your computer and your own SD card.

## Step 1 — Get the project and the tools

Download or clone the project:

```sh
git clone https://github.com/estyseesghosts/hgss-3ds.git
cd hgss-3ds
```

Then install the 3DS compiler and libraries:

```sh
python3 scripts/setup_toolchain.py
```

The script checks what is already installed and installs only what is
missing, using devkitPro's own package manager. It asks for your password
because the tools go into `/opt/devkitpro`.

- **Debian/Ubuntu and derivatives** are set up automatically.
- **macOS** uses devkitPro's installer, which the script downloads for you.
  It also installs **Homebrew's GNU Make** if needed: macOS's own `make` is
  3.81, and the build needs 4.0 or newer. (Homebrew itself must already be
  installed — [brew.sh](https://brew.sh).) The scripts find `gmake`
  automatically. macOS's `git` and `make` come from the Xcode Command Line
  Tools; if they are missing, run `xcode-select --install` first.
- It does **not** need a ROM, a save, the original Nintendo SDK, or a
  container. Nothing proprietary is downloaded.

At the end it prints a table of every tool and its version. You can re-check
at any time without changing anything:

```sh
python3 scripts/setup_toolchain.py --check
```

## Step 2 — Prepare the game data from your ROM

```sh
python3 scripts/prepare_game.py ~/Downloads/pokemon-heartgold.nds
```

Use the path to your own dump; SoulSilver works exactly the same way and the
script works out which game it has. Three things happen:

1. The dump is checked. If it is trimmed, patched, from another region or
   damaged, the script stops and says why.
2. The cartridge's files are extracted into a private folder
   (`build/game-data/` for HeartGold, `build/game-data-ss/` for SoulSilver).
3. Those files are packed into the file the port reads:
   `build/heartgold3ds.pak` (or `build/soulsilver3ds.pak`).

Expect it to take a minute or two. Running it again with the same ROM reuses
the extracted files; add `--force` to extract again.

> **SoulSilver is supported.** The script accepts a SoulSilver dump and packs
> it as `soulsilver3ds.pak` with build ID `soulsilver-nitrofs-1`. Its app is
> `pokesoulsilver.3dsx`, and it reads `sdmc:/3ds/pokeheartgold-ss/`; that
> folder and its save (`soulsilver.sav`) are separate from HeartGold's, so
> both games can sit on one SD card. The build script picks the game from the
> pack's name and compiles the matching program automatically.

## Step 3 — Build the app and the SD-card folder

The easiest way is to give the build script the ROM directly. It prepares the
data itself if needed:

```sh
python3 scripts/build_3dsx.py --rom ~/Downloads/pokemon-heartgold.nds
```

If you already prepared the pack in step 2:

```sh
python3 scripts/build_3dsx.py --pak build/heartgold3ds.pak
python3 scripts/build_3dsx.py --pak build/soulsilver3ds.pak
```

The pack's name selects the game: the script builds `pokeheartgold.3dsx` or
`pokesoulsilver.3dsx` with the matching folder and save names.

The script:

1. fetches the decompilation source and applies the project's patches
   (`tools/bootstrap.py`; it says “already at …” when this is done),
2. compiles the game and the 3DS port (`tools/hgss_game.py`),
3. checks the result with devkitPro's `3dsxdump` when it is installed,
4. copies everything into a staging folder shaped like an SD card.

The first build takes about ten minutes; later builds are quicker. At the end
it prints the size and SHA-256 of the `.3dsx` and the exact files to copy.

It stops with a clear message if a step fails. Copy the last lines it printed
when asking for help.

## Step 4 — Copy it to the SD card

The staging folder is `dist/sd-card/` unless you chose another with `--out`.
It looks like this:

```
dist/sd-card/
└── 3ds/
    ├── pokeheartgold.3dsx
    ├── pokesoulsilver.3dsx
    ├── pokeheartgold/
    │   ├── heartgold3ds.pak
    │   └── manifest.json
    └── pokeheartgold-ss/
        ├── soulsilver3ds.pak
        └── manifest.json
```

Copy the **`3ds` folder inside it** onto the root of the SD card, merging it
with whatever is already there. If you built only one game, only its files
are in the folder. The card must end up with the files for the game(s) you
built:

| Path | What it is |
| --- | --- |
| `/3ds/pokeheartgold.3dsx` | the HeartGold app the Homebrew Launcher starts |
| `/3ds/pokeheartgold/heartgold3ds.pak` | HeartGold's files |
| `/3ds/pokeheartgold/manifest.json` | its pack index; copy it with the pack |
| `/3ds/pokesoulsilver.3dsx` | the SoulSilver app |
| `/3ds/pokeheartgold-ss/soulsilver3ds.pak` | SoulSilver's files |
| `/3ds/pokeheartgold-ss/manifest.json` | its pack index |

If your card is mounted, you can skip the manual copy by pointing `--out` at
it:

```sh
python3 scripts/build_3dsx.py --pak build/heartgold3ds.pak --out /Volumes/3DS
```

(Use the path your system shows for the card; on Linux it is often
`/media/<you>/<card-name>`.)

Put the card back, open the Homebrew Launcher, and start
**pokeheartgold**. The first start creates `heartgold.sav` and `log.txt` in
the same folder. Keep the save; it is a standard 512 KiB raw save, so it also
works in an emulator and can be moved back and forth.

### Sound

The 3DS's DSP firmware is not included. If you have
`/3ds/dspfirm.cdc` from your own console, the game plays sound; without it the
game runs silent and says so in `log.txt`. This is not an error.

## Troubleshooting

- **The Homebrew Launcher does not list the app.** Check the card has
  `/3ds/pokeheartgold.3dsx` (or `/3ds/pokesoulsilver.3dsx`) exactly
  (lowercase), and that the launcher is looking at the same SD card.
- **“Cannot read the data pack or its manifest.”** The pack and
  `manifest.json` must be copied together into
  `/3ds/pokeheartgold/`. Re-run step 3 and copy both files again; do not
  rename them.
- **A save from an emulator shows New Game.** A raw 512 KiB `.sav` now loads
  (the 2026-11-02 CRC fix). A DeSmuME `.dsv` has a text footer: run
  `python3 tools/savetool.py normalize old.dsv -o heartgold.sav` first. A save
  written by an early 3DS build has legacy substruct CRCs; the current build
  migrates them on first read.
- **“The save data could not be accessed.”** The save is read/write; check the
  SD card is not locked and has free space. Remove a damaged `heartgold.sav`
  only if you do not need it (the game makes a new one).
- **I made a SoulSilver pack but cannot find it in the launcher.** Start
  **pokesoulsilver**, not pokeheartgold; it reads
  `/3ds/pokeheartgold-ss/`. Both apps can be on the card at once, and each
  keeps its own save.
- **The build fails with a “missing separator” or `$(file …)` error.** Your
  `make` is too old (macOS ships 3.81). Run
  `python3 scripts/setup_toolchain.py` again; it installs Homebrew's GNU Make
  and the scripts use it automatically.
- **The build fails with a missing tool.** Run
  `python3 scripts/setup_toolchain.py`, then try again. On macOS, open a new
  terminal after the install so the new paths are picked up.
- **The build fails while downloading the decompilation.** Check the internet
  connection, then run `python3 tools/bootstrap.py` on its own to see its
  message.
- **It runs but the picture or sound is wrong.** Copy
  `/3ds/pokeheartgold/log.txt` off the card and attach it to a report. The log
  names the frame and the failing step.

## Command summary

| Command | What it does |
| --- | --- |
| `python3 scripts/setup_toolchain.py` | installs the 3DS compiler, libraries and packaging tools (Linux/macOS) |
| `python3 scripts/setup_toolchain.py --check` | reports what is installed; changes nothing |
| `python3 scripts/prepare_game.py GAME.nds` | checks your HeartGold or SoulSilver dump, extracts it and builds the `.pak` |
| `python3 scripts/build_3dsx.py --rom GAME.nds` | does everything: pack, build, and stage the SD-card folder |
| `python3 scripts/build_3dsx.py --pak build/heartgold3ds.pak` | builds and stages from an existing pack |
| `python3 scripts/build_3dsx.py --pak build/soulsilver3ds.pak` | the same for SoulSilver (`pokesoulsilver.3dsx`) |
| `python3 scripts/build_3dsx.py --pak … --out /Volumes/3DS` | writes the SD-card folder straight onto a mounted card |

Every script has `--help` with the details.

## See also

- [SD-Card-Layout](wiki/SD-Card-Layout.md) — every file and folder on the card
- [Hardware-Testing](wiki/Hardware-Testing.md) — what to check on the console
- [3DS-Pitfalls](wiki/3DS-Pitfalls.md) — known problems and their fixes
- [WINDOWS.md](WINDOWS.md) — the Windows setup (native build, tested)
- [Getting-Started](wiki/Getting-Started.md) — the developer/contributor setup
