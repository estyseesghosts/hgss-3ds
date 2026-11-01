# Reference captures (golden frames)

Goal: frames of both screens from a **reference emulator** (BizHawk with the
melonDS core), driven by frame-indexed input, repeatable bit for bit. Later
phases diff the PC build against these.

BizHawk has no macOS build. Run captures on a Linux or Windows machine you
choose. **Golden frames show game art: they stay local (`tests/golden/` is
git-ignored) or in private storage. Only the scripts and inputs are committed.**

Never compare by frame number against a real DS; compare emulator to emulator,
or emulator to the PC build.

## Fixed settings (do these before every capture session)

Set these in BizHawk and record the BizHawk version, core version and the
settings you used in a local note next to the captures. Menu names move between
versions; this list says *what* to fix, so confirm each in the UI.

1. **RTC: 2026-01-01 10:00:00.** Use the core's fixed/synced clock option, not
   "real time", and disable "sync to host time". The game reads the clock at
   boot; `InitializeMainRNG()` in `src/main.c` seeds from clock and ticks.
2. **Firmware profile:** fixed nickname, birthday, language English, and a
   fixed MAC. Do not use "boot from real firmware with auto-detect".
3. **Start from a power-on state**, not a savestate, with the same save file
   (or none) each run. For "no save" scenes use an empty/deleted `.sav`.
4. **Screen layout:** fixed (vertical, 1x, no gap changes) so screenshots are
   comparable.
5. **No frameskip, no speed-up that drops frames**, audio irrelevant.
6. Same ROM SHA1 (HG US `4fcded0e2713dc03929845de631d0932ea2b5a37`).

## Input scripts

`tests/inputs/<scene>.txt`, format documented at the top of `capture.lua`.
Input is frame-indexed from power-on (frame 0). **The shipped scripts are
uncalibrated**: their frame numbers are estimates that must be tuned against a
first run (watch where the logo, movie and menus really are, adjust, commit).
Don't skip the opening movie in `boot-title`; watch it in full.

## Running

```
cd <repo>            # so tests/inputs and tests/golden resolve
# BizHawk Lua console:
SCENE = "boot-title"; dofile("tools/golden/capture.lua")
```

The script prints `joypad.get()` at frame 0. **Check the touch input names it
prints** against `TOUCH_*` at the top of `capture.lua` (unverified).

## Determinism procedure

For every scene:

1. Capture into `tests/golden/<scene>/` (run A).
2. Move that to `tests/golden/<scene>.runA/`, capture again from a fresh
   power-on with identical settings (run B).
3. Compare: `python3 tools/golden/compare_runs.py tests/golden/<scene>.runA tests/golden/<scene>`
   (exact, byte-for-byte on decoded pixels; exit 1 on any difference).
4. If they differ, find what was not fixed (clock, firmware, save, input
   timing, RNG seed) and fix it. Don't raise thresholds to hide it.
5. Only when A == B, keep it as the reference and delete run A.

Committed: `capture.lua`, `compare_runs.py`, `tests/inputs/*.txt`. Never
committed: anything under `tests/golden/`.

## The PC-side runner (`run.py`)

`tools/golden/run.py` replays a scene through `pokeheartgold-pc` and compares
the screenshots. It needs no emulator:

```
python3 tools/golden/run.py --scene newgame-buttons
#   capture + compare against tests/golden/newgame-buttons/ when it exists
python3 tools/golden/run.py --scene boot-title \
    --reference tests/golden/boot-title
#   compare an existing run (regression mode) without capturing:
python3 tools/golden/run.py --scene newgame-buttons --no-run \
    --run /work/shots --reference /work/previous-run
```

The scene file is `tests/inputs/<scene>.txt`; `every:` becomes
`--screenshot-every`, button lines become `--press` and `touch` lines become
`--touch`. The PC writes `frame_%07d.png` (256×384, upper screen above
lower); melonDS captures are `<frame>.png`, and both are accepted.

**Report format.** `build/golden/<scene>/report.json` has the scene, run and
reference directories, the alignment `{offset, first_match}`, the thresholds,
one entry per frame `{frame, reference_frame, differing, max_delta, allowed,
status}` and a summary. `report.html` is the human table; `heat/` holds one
RGB PNG per failing frame (red = differing pixel, black = within tolerance).

**Thresholds.** 2D frames are exact (`--tolerance 0`). The 3D frames (opening
movie and title) allow a per-channel difference of **8/255** up to the scene's
`--3d-until` frame (6600 for `newgame-buttons`, which is where the 3D ends).
The 8 was chosen as the smallest value that covers the PC rasterizer's
fixed-point rounding without hiding a structural difference; it is recorded
in `docs/decisions.md`. Alignment uses the exact 2D frames, so a 3D tail
cannot drag the offset.

Without a reference directory the tool still captures a baseline and says so
in the report (`pass: true`, `note` says the golden comparison is pending).
That is regression mode; it is **not** a melonDS match.


## The diff runner (`run.py`)

`run.py` replays a scene through the PC build, captures both screens,
aligns them against a reference directory and writes a report. It never
needs the ROM: the pack and manifest are passed at run time.

```text
python3 tools/golden/run.py --scene newgame-buttons
python3 tools/golden/run.py --scene boot-title --reference tests/golden/boot-title
python3 tools/golden/run.py --scene newgame-buttons --no-run \
    --run /work/shots --reference /work/previous-run
```

- `--scene` reads `tests/inputs/<scene>.txt` (the same `every`/`end`/input
  format `capture.lua` uses).
- With no `--reference`, `tests/golden/<scene>/` is used when it exists. If
  neither exists, the run is a **regression baseline**: the report is
  written with `pass: true` and a note that the golden comparison is still
  pending. This is the mode to use until the user's captures exist.
- `--no-run --run DIR` compares an existing capture directory (fruitful for
  comparing two PC runs against each other).
- The PC writes `frame_%07d.png` (256×384: upper above lower); melonDS
  writes `<frame>.png`. Both are accepted.

### Alignment and thresholds

1. Parse the scene into `--press FRAME:BTN,...` and `--touch FRAME:X:Y,...`.
2. Run the PC with the scene's `end` as `--frames` and `every` as
   `--screenshot-every`.
3. Try every frame offset in `±--align-window` (default 240) and pick the
   one with the longest run of matching frames; report the offset and the
   first matching frame.
4. Compare every overlapping frame. A pixel differs when the largest RGB
   channel delta exceeds the frame's tolerance.

**2D frames must match exactly** (`--tolerance 0`). **3D frames** (the
opening movie and title, up to `--3d-until`, default 6600) allow a
per-channel tolerance of **8/255** (`--3d-tolerance 8`); the value is in
`docs/decisions.md`. Raising either threshold to hide a difference is not
allowed — fix the cause or record the gap.

### Report format

Under the output directory (`build/golden/<scene>/` by default):

- `report.json`: `scene`, `run`, `reference`, `image`, `alignment`
  (`offset`, `first_match`), `tolerance`, `frames` (each with `frame`,
  `reference_frame`, `differing`, `max_delta`, `allowed`, `status`,
  optional `heat_map`) and `summary` (`frames`, `compared`, `passed`,
  `failed`, `reference_missing`). `pass` is true only when `failed` is 0.
- `report.html`: the same table, with links to the heat maps.
- `heat/frame_NNNNNNN.png`: red where a pixel differs beyond tolerance,
  black where it matches.
- `events.jsonl`: the PC runner's machine-readable events.

Exit status: 0 when every compared frame passes (or no reference was
available and a baseline was written), 1 on a differing frame, 2 on a setup
error.
