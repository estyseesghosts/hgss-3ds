# Phase 5 progress

Status: **in progress — software side under construction; every hardware item is pending the user.**

Last verified: **2026-11-01**, container `/workspaces/pokeheartgold-3ds`.

## What is in the tree

### 3DS shell (`hgss/game/app/main_3ds.c`)

Adapted from Platinum's shell, built around `Heartgold_Run`:

- **Paths** `sdmc:/3ds/pokeheartgold/` for `heartgold3ds.pak`, `manifest.json`,
  `heartgold.sav`, `log.txt`, `port.ini` and `script.txt`. The SoulSilver
  variant (folder `pokeheartgold-ss/`, `soulsilver3ds.pak`, save name, build
  ID) is selected by `HGSS_SS_VARIANT`; the shared sources carry the default.
- **Display** centered 1:1 only: top 256×192 at (72,24) in 400×240, bottom at
  (32,24) in 320×240. The framebuffer is written in its stored orientation
  (`fb[x * 240 + (239 - y)]`), not rotated twice. The remaster, wide and
  lower-menu paths are off.
- **Input** D-pad and circle pad (40-unit dead zone) → DS D-pad;
  A/B/X/Y/L/R/Start/Select mapped; ZL/ZR and C-stick unused.
- **APT** an `aptHook` pauses on suspend/sleep and resumes on restore/wakeup;
  the main thread keeps `aptMainLoop` serviced.
- **Crash log** `threadOnException` writes the exception type, r0-r12/sp/pc/
  cpsr, the last 64 log lines and 16 stack words to `log.txt`, then stops.
- **NDSP** starts only when `dspfirm.cdc` loads; otherwise the game runs
  silent, `log.txt` says so, and the pak-error screen adds a sound note. It
  never stops the game. Audio mixing into NDSP is not wired yet (see gaps).
- **Build** `python3 tools/hgss_game.py --target 3ds --out …` links 1092/1092 with 0 unresolved and packages `pokeheartgold.3dsx` (9,103,728 bytes) plus `pokeheartgold-3ds.smdh` (smdhtool + the generic placeholder icon).
- **Scripted input** `port.ini`'s `script=PATH` (or `script.txt` beside the
  pack) is parsed by `3ds_input.c` and replayed frame-for-frame, including
  six-frame button holds and touch positions. Without a script the console's
  controls drive the game.

The plain-C parser has a host test (`tests/game/script_input/run.sh`).

### Screen routing (P5-B)

The PC shell and `NitroPort_RenderScreens` already follow `POWCNT1` bit 15
(which is what `GX_SetDispSelect` writes): engine A to the top or bottom,
engine B to the other. A dedicated mid-run flip test is still to land; see
the gap list.

### Frame-time instrumentation (P5-C)

Both shells log one sample per 600 frames (60 on the PC with `--frametime`):

```text
FRAMETIME frame=1234 logic_us=820 draw_us=9100 audio_us=0 total_us=16720
```

The 3DS measurement is the interval between `NitroPort_OnFrame` calls (the
game's own work), the time inside `DrawFrame`, and the same for the PC. Audio
is not mixed yet, so `audio_us` is 0.

`tools/frametime_report.py` parses either log (whole `log.txt` is fine; other
lines are ignored) and prints mean/min/p95/max per subsystem plus the frame
rate against 59.8 Hz. It writes JSON/Markdown for the budget table. Tests are
synthetic. **The budget table itself is a hardware deliverable and is pending
the user's New 3DS run.**

### CIA build (P5-D)

**Blocked on a tool decision.** `makerom` and `bannertool` are **not** in the
container (checked 2026-11-01, as is `3dsxtool`/`smdhtool`). The plan says to
stop and ask before adding a new tool dependency. The `.3dsx` path needs no
new dependency. The banner and icon must come from the user's cartridge at
build time into `build/`, never git.

## Pending user runs

All of these are in `docs/wiki/Hardware-Testing.md` with exact steps and the
definition of a pass:

- Scripted New 3DS run to Oak's speech (full speed, visually correct).
- New 3DS frame-time log and budget table.
- Old 3DS measurement (the support decision is recorded only after it).
- Save round-trip both ways: port save → melonDS Continue; cartridge save →
  port Continue.
- `dspfirm.cdc`: without it the run is silent by design.

None of these is marked done. The container cannot attach a console, read an
SD card or run a cartridge.

## Open gaps

- Audio is initialized through NDSP but not mixed; the port's
  `NitroPort_SoundRender` output is not yet fed to `ndspAddWave`.
- `--sanitize` requires `libstdc++` at link time only for the ASan runtime;
  the normal link stays libstdc++-free.
- SMDH: `smdhtool` can package a generic, non-ROM icon; the cart-derived icon
  and banner are generated locally and never committed.
