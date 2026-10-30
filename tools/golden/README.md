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
