# Benchmark packets (Packet A)

This folder holds the frozen configurations for performance A/B runs. The
rules behind them are in `PICA200-BUILDING.md` (Packet A) and
`PICA200-INSTRUMENTATION.md` (§B, §I): every result needs a run manifest, a
matched baseline, and Old 3DS hardware as the authority.

Everything here is diagnostic. Nothing in `docs/bench/` or the two tools it
names changes how the game renders.

## Files

| File | Purpose |
|---|---|
| `port-baseline.ini` | the frozen `port.ini`: documented defaults only, every knob spelled out |
| `../tests/inputs/ss-continue-field.txt` | SoulSilver: boot with a save → title → CONTINUE → field |
| `../tools/bench_manifest.py` | writes the run identity (commit, artifact/save/script/ini hashes, model, flags) |
| `../tools/bench_report.py` | turns a port `log.txt` into the per-sample table and summary |

## A frozen run, in order

1. **Build** the program for the game you are testing and note its SHA-256
   (`tools/hgss_game.py --target 3ds ...`). Never compare a dirty artifact
   with a clean one; the manifest records which it is.
2. **Stage the SD card** (Azahar: `%APPDATA%\Azahar\sdmc\3ds\`; a console:
   the card's `3ds\`):

   ```
   3ds/<app>.3dsx                      pokeheartgold.3dsx / pokesoulsilver.3dsx
   3ds/pokeheartgold[-ss]/<pak>        heartgold3ds.pak / soulsilver3ds.pak
   3ds/pokeheartgold[-ss]/manifest.json
   3ds/pokeheartgold[-ss]/<save>.sav   the frozen save (raw 512 KiB)
   3ds/pokeheartgold[-ss]/port.ini     docs/bench/port-baseline.ini
   3ds/pokeheartgold[-ss]/script.txt   the frame-indexed input script
   ```

3. **Write the manifest** before running:

   ```
   python tools/bench_manifest.py --game soulsilver \
       --artifact <staged>.3dsx --save soulsilver.sav \
       --script tests/inputs/ss-continue-field.txt \
       --ini docs/bench/port-baseline.ini \
       --model "Old 3DS" --label ss-continue -o build/bench/ss-continue.json
   ```

4. **Run it.** On hardware, launch from the Homebrew Launcher and leave the
   console untouched until the script ends plus one more 600-frame sample
   (~10 s). Hold START+SELECT for 2 s to leave cleanly.
5. **Pull `log.txt`** from the game folder and keep the manifest beside it.
   Check the files still match: `python tools/bench_report.py log.txt
   --check-manifest build/bench/ss-continue.json`.
6. **Report**: `python tools/bench_report.py log.txt --manifest
   build/bench/ss-continue.json --from-frame N --to-frame M`.

## Instrumentation builds (PROFILE vs OFF)

The default build is **PROFILE**: every counter and counter log is compiled
in. That is the right build for *work* proof (which stage did what), but it is
not the build to quote timings from — the profiler's own cost must be measured
first (PICA200-INSTRUMENTATION.md §B).

Build the **OFF** variant with `HGSS_PROFILE=off` (PowerShell:
`$env:HGSS_PROFILE='off'`). It compiles out the 2D renderer's per-draw tick
reads (26 per engine draw, `R2D_NOW`) and the geometry diagnostics' logging;
every other flag, the commit and the inputs stay identical, and the port
refuses to link an archive whose profile mode does not match the run.

```
$env:HGSS_PROFILE='off'
python tools/hgss_game.py --game heartgold --target 3ds --keep-going \
    --out build/game-3ds-off -j 8
```

Then run matched alternating trials and compare (this drives Azahar, copies
the save/script/ini for each trial, re-focuses the window and reports medians
with the spread):

```
python tools/bench_ab.py --game heartgold \
    --artifact-a build/bench/ab/goldenrod-profile.3dsx --name-a profile \
    --artifact-b build/bench/ab/goldenrod-off.3dsx     --name-b off \
    --save build/bench/heartgold.sav \
    --script tests/inputs/hg-continue-field.txt \
    --ini docs/bench/port-baseline.ini \
    --trials 3 --seconds 240 --label goldenrod-profile-vs-off
```

Two identical builds that overlap in every metric are the honest outcome for
emulator timing; the hardware A/B is the claim.

## What the report means

> Run Azahar with its window focused. The emulator throttles hard while it is
> in the background: an unfocused run reached ~1,200 emulated frames in
> 4 minutes where a focused one does ~18,000. All the numbers below were taken
> focused.

The shell samples every 600 frames. One row is one 600-frame aggregate:

- `frame ms` — the entry-to-entry interval at the sampled frame, in emulated
  ticks. At 16.7 ms the loop is at 60 Hz; lower means the sample caught a
  faster frame (in an emulator the emulated clock can outrun wall time, so
  treat this column as a shape, not a rate). 60 Hz on hardware means the game
  clock kept up.
- `game ms` / `state ms` — game-side cost and hand-off cost in that interval.
- `comp/s`, `pub/s`, `drawer ms`, `geom ms` — presentation, publication,
  worse-screen drawer wall time, and geometry worker service time.
- `bg takes` — GPU BG quads committed in the window (zero is a fallback, not
  a success).
- `lin free KB` — linear-heap headroom (texture room).

Caveats the tools print and the guides require us to repeat: one sample is
not a frame-time distribution; wall time includes preemption and waits; an
Azahar run is functional coverage, never Old 3DS timing.

## Status

- **Save**: the provided `soulsilver.sav` (524,288 B) is a **HeartGold** save
  (CONTINUE shows player `AAR`, 5:24, 2 badges, 10 Pokédex; the SoulSilver
  build correctly offers New Game only). Keep a copy named `heartgold.sav`.
- **Baseline (Azahar 2126.2, Old-3DS mode, 268 MHz profile)**, 2026-11-09:
  - Goldenrod field, frames 9000-18000: **21 composes/s**, 60 publishes/s,
    drawer **4.61 ms/frame**, 2 GPU BG takes/frame, geometry **30.8 ms/scene**
    at 92% busy. Geometry worker is the binding stage; the drawer is cheap.
  - Title screen, frames 4200-14400: drawer 48-54 ms/frame (16 fps),
    36-38 composes/s, **zero GPU BG takes and zero nomination attempts**.
  - Per-120-scene counts: 1,683,600 vertices in, 113,637 triangles out,
    482,241 clipped, hidden lists skipped 1,090,801 vertices.
  - Full record: [claims/2026-11-09-goldenrod-baseline.md](claims/2026-11-09-goldenrod-baseline.md).
- **Instrumentation A/B done in Azahar** (3 alternating trials per build,
  frames 9000+, window focused): PROFILE vs `-profoff` — every metric's range
  overlapped, medians within 0.3% (comp/s 20.90 vs 20.95, drawer 4.61 vs
  4.62 ms, geometry 30.97 vs 31.02 ms). The profiler's cost is below the
  emulator's noise (~±3%), so the baseline numbers stand within that bound;
  the same A/B must run on hardware.
  See [claims/2026-11-09-profile-vs-off.md](claims/2026-11-09-profile-vs-off.md).
- **Old 3DS hardware run: pending.** Two packets are staged:
  - `build/bench/sd-hg/` — PROFILE build (counters and `[gx3d]` lines; use
    for *work* proof).
  - `build/bench/sd-hg-off/` — OFF build (`-profoff`; use for *timing*
    claims; it is the binary the profile-vs-off A/B measures).
  Copy either `3ds` folder to the card root, run it, and send back `log.txt`
  plus the manifest. Both boot the same save into Goldenrod with no input.
