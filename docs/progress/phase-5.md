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

## First hardware run: a packaging defect, found and fixed

The first `.3dsx` on a New 3DS never started: Luma wrote
`crash_dump_00000001.dmp` with an **undefined instruction** in the process
named `loader`, pc at a `udf #0`. Decoding the dump (Luma's
`ExceptionDumpHeader`: 40-byte header, 92-byte register block, 96-byte code
window, then the stack and `processName`/`titleId`) showed the fault was in
the console's Loader, not in game code — our image is `0x00100000`-
`0x009263ec` and never appears, and `log.txt` was never written.

Cause: **one word in the `.3dsx` lied about being a pointer.** Patch 1406's
`LcdcAddress()` was `NitroPort_LcdcVram + (address - 0x06800000)`; GCC
reassociated it to `(NitroPort_LcdcVram - 0x06800000) + address`, so a
literal-pool word held an ABS32 relocation with addend `-0x06800000`,
resolving to `0xFA607000` at ELF `0x008e5a20`. `3dsxtool` treats any
relocated word at or above the image base as an absolute pointer, so it packed
that word as a pointer ~4 GB outside the image and the Loader rejected it.
`3dsxdump` refuses both earlier builds with `Cannot write segment data!`
(return 7), which is the same check.

Fixes: patch **1903** keeps the base read behind a `noinline` call so the
subtraction cannot fold; `tools/hgss_game.py` now runs `validate_3dsx()` on the
packaged file and fails the build instead of shipping it. Verified: the new
`.3dsx` passes `3dsxdump` (`CODE 2087 pages, RODATA 109, DATA 13, BSS 3977`)
and `validate_3dsx` reports 0 problems, while the old one reports 1.

**Every host check passed the whole time** — the PC build, the port tests and
the DS gate never look at the `.3dsx`. That is why this is now a build-time
check rather than a note.

## Second hardware run: the program stopped itself at frame 0

With the relocation fixed, the loader accepted the file and the app started.
`log.txt` from the console:

```text
[0] pokeheartgold-3ds starting
[0] NDSP started
[0] replaying sdmc:/3ds/pokeheartgold/script.txt: 42 inputs, end frame 20000
[0] running the game
[0] [heartgold] save file: sdmc:/3ds/pokeheartgold/heartgold.sav
[0] [heartgold] starting the game
[0] stopping (frame 0)
```

What that rules out is as useful as what it shows:

- `[heartgold] NitroMain returned` and the port's
  `[nitro] OS_Terminate: the game stopped itself` are **both absent**, so the
  game thread neither returned nor terminated: it was still initialising.
- `sFrame++` happens *before* `NitroPort_OnFrame`'s `aptMainLoop` check, so a
  close reported by the first frame would have printed `frame 1`.

So `main()` stopped the process: `WaitForGame` polled `aptMainLoop()` 100 ms
after the thread started, got `false`, and set the quit flag. `aptMainLoop()`
is `aptHandleSleep(); aptHandleJumpToHome(); return !aptShouldClose();` and
libctru documents it as "call this at the beginning of every frame" -- one
caller, per frame. Platinum's shell never polls it from its wait loop either
(`Watch()` waits on an event).

Fix: `WaitForGame` only joins the thread (and logs a "still starting" line
after 10 s without a frame); the game thread is the sole `aptMainLoop` caller
and, when APT asks to close, logs `the HOME menu asked to close (frame N)`,
flushes the card and leaves its thread so teardown stays ordered. A text test
now fails if `WaitForGame` ever polls APT again.

If APT really did deliver a close/cancel at launch, the next run will say so
in `log.txt` instead of dying silently, and that is the thing to chase.

## Third hardware run: a join that said "ended" while the thread ran

With APT left to the game thread, main no longer killed the run itself. The
next console log:

```text
[0] [heartgold] starting the game
[0] the game's thread ended at frame 0
[0] [nitro] not implemented: DWC_CheckUserData
[0] stopping (frame 0)
```

`WaitForGame`'s `threadJoin` returned success, so libctru reported the thread
ended — but a game-thread line (`DWC_CheckUserData`, the port's Wi-Fi stub
during the game's early init) follows it, so the thread was inside
`NitroMain`. libctru's side is:

```c
Result threadJoin(Thread t, u64 timeout) {
    if (!t || t->finished) return 0;
    return svcWaitSynchronization(t->handle, timeout);
}
```

so success means `Thread.finished` was already set (or the pointer was bad),
not that the thread returned. Nothing we know of calls `threadExit` on it: the
port never does, and the shell's two call sites both log first and neither
line is in the file.

Both plausible causes are now handled:

- **A clobbered `Thread.finished`.** libctru allocates the `Thread` block at
  the base of the same allocation the stack grows down into, so an overflowing
  host stack writes into it. The game thread's stack went from 512 KiB to
  1 MiB, and the log now records which size it used.
- **A join result that cannot be trusted for "has it ended".** `WaitForGame`
  polls the shell's own `sGameThreadAlive` (set and cleared inside
  `GameThread`) and logs `threadJoin says the game's thread ended, but it is
  still running` if the two disagree.

The log had no lock either, while two threads wrote it; stdio is not
reentrant, so line order could not be trusted. There is a `LightLock` around
every line now.

## Fourth hardware run: it runs, and draws nothing

The cycle is now: the game starts, runs the script, and keeps going until
HOME is pressed. From the console log: **frames 0 → 39,070**, overlays
loading, the 3D pipeline building and rasterising scenes (up to ~300k
vertices, ~128k triangles a frame during the intro), and the APT close
handled cleanly at the end (`paused (APT hook 0)` → `the HOME menu asked to
close (frame 39070)` → `stopping`).

The diagnostic from the previous fix did its job:

```text
threadJoin says the game's thread ended, but it is still running
```

so libctru's join really does report an end while the thread is alive; the
shell's own flag is what keeps the run alive.

**But the screens were black, and `draw_us=1` says why.** `DrawFrame` began
with a `NitroPort_RenderScreens == NULL` guard, and it was always taken:

```text
final-3ds5 elf:  w NitroPort_RenderScreens     <- weak undefined: NULL at run time
libnitro.a:      T NitroPort_RenderScreens     <- defined, never extracted
```

The shell declared the hook `__attribute__((weak))` (it cannot include
`nitro_port.h`, whose NitroSDK types clash with libctru's). A weak undefined
reference does **not** make the linker pull the definition out of
`libnitro.a`, so the call was skipped every frame. The PC shell declares it
strongly through `nitro_port.h`, which is why screenshots always worked.

Fixed: the hooks are strong declarations with `nitro_port.h`'s signatures, the
NULL guard is gone, and a test fails if any hook is declared weak again.

Two notes for the next run:

- **Timing**, from this log (with the 2D pass doing nothing): ~0.4–0.7 ms in
  menus, and **33–35 ms (~28 Hz)** in the 3D-heavy intro/title. Adding the 2D
  pass will cost more, so this is a floor, not a verdict.
- **Audio is still silent by design**: NDSP starts, `audio_us=0`, and the
  port's mixer is not fed yet. That is a separate piece of work, not this bug.

## Fifth hardware run: it draws — at 13 Hz

The strong hook fixed the display: the logo, intro and title are visible. The
owner stopped the run at frame 2122 because it was very slow. Three samples:

```text
FRAMETIME frame=600  logic_us=0    draw_us=96759 audio_us=0 total_us=88148
FRAMETIME frame=1200 logic_us=583  draw_us=71744 audio_us=0 total_us=72327
FRAMETIME frame=1800 logic_us=681  draw_us=65792 audio_us=0 total_us=66473
```

`tools/frametime_report.py`: **13.2 Hz**, draw mean **78.1 ms** (65.8–96.8),
logic 0.4 ms. So the port's 2D compositor is the entire cost; the game itself
is free at this point.

Reading the numbers carefully: `draw_us` includes `gfxSwapBuffers`' wait for
the previous frame to be presented, which is why the first sample has
`draw_us > total_us`. The component to watch is the difference between them,
not `draw_us` alone.

### Why it was that slow: the shell never asked for the New 3DS clock

Platinum's shell starts with `osSetSpeedupEnable(true)` and
`PTMSYSM_ConfigureNew3DSCPU(3)`; the HGSS shell did neither, so the program
ran at the **Old-3DS clock with the L2 cache off** while doing a memory-bound
software composite of two 256×192 screens. The shell now makes both calls
(before `gfxInitDefault`) and logs which mode it got:

```text
[perf] New 3DS: 804 MHz + L2 requested
```

That is the first thing to measure again; if 804 MHz + L2 brings draw under
the 16.7 ms budget, nothing else is needed for menus and the intro.

### Known next levers, in order

1. **The speedup above** (cheap, expected to dominate).
2. **Game/app objects are compiled at `-O0`.** `port_cflags` in
   `tools/hgss_game.py` passes no `-O`, while the vendored SDK is built at
   `-O2`; the 3D-heavy logic frames cost 33–35 ms this way. Changing codegen
   for the whole game is not free: it needs the PC smoke run and the DS gate
   re-run before it is trusted.
3. **Drawing on core 1.** Platinum's architecture; it overlaps the composite
   with the game thread instead of adding to it. Bigger change, and only worth
   it if 1 and 2 leave the budget short.
4. `NitroPort_Render2DSplitIfChanged` skips an engine whose inputs did not
   change, but a moving 3D image counts as a change, so it does not help the
   intro — it helps static menus.

## Sixth hardware run: 35.7 Hz

With the speedup on (`[perf] New 3DS: 804 MHz + L2 requested`), eight samples:

```text
draw  mean 26.9 ms  (22.2-35.8)      was 78.1 ms
logic mean  1.1 ms  (0.06-5.7)
total mean 28.0 ms  (22.3-37.9)  ->  35.7 Hz
```

So the clock/L2 request bought **2.7x**. The owner's verdict: choppy, visually
correct. The remaining gap is the 2D composite: ~27 ms against a 16.7 ms
budget, with the game itself at ~1 ms.

## Where the performance work goes next

`## Next levers` below is the working list; the short version:

1. **Per-engine change detection.** `NitroPort_Render2DIfChanged(engine, out)`
   draws only when that engine's inputs changed, so a static screen costs
   nothing. `DrawFrame` currently calls `NitroPort_RenderScreens`, which
   always composites both engines. This is small, safe, and should help
   everywhere one screen is still — menus and dialogs especially.
2. **The composite on core 1**, as Platinum's shell does, overlapping it with
   the game thread instead of adding to it. Bigger change; worth it if 1
   leaves the budget short.
3. **Audio**, which is independent of the above and is the most visible gap
   left: `NitroPort_SoundRender(stereo, frames)` already mixes the DS's
   32768 Hz stereo (the PC build writes it to a WAV); it simply has no output
   on the 3DS. NDSP is initialised and idle.
4. `-O0` game objects (see below) — now worth ~1 ms, so it waits.

## Sound, and drawing only what changed

Two shell changes, both independent of the patch stack.

### Audio plays

`NitroPort_SoundRender` already mixed the DS sound chip at 32768 Hz stereo (the
PC shell writes the same stream to a WAV); nothing consumed it on the 3DS.
`AudioThread` now renders it into four NDSP wave buffers of 512 frames
(15.6 ms each) on its own thread at priority 0x31 -- the mixer takes its own
lock, so it never holds the game up -- and `NitroPort_OnFrame` pumps the ARM7's
alarm replies on the game thread, as the PC shell does.

NDSP's own rate is `SYSCLOCK_SOC/512` (~32728.5 Hz), so the channel runs at
32768 and the DSP resamples: 0.12%, about two cents, inaudible. The exit log
now ends with `audio: N dropped frames of M`, which is the direct measure of
whether the DSP was ever starved.

### Drawing only the engine that moved

`DrawFrame` used `NitroPort_RenderScreens`, which composites both engines every
frame regardless. It now calls `NitroPort_Render2DIfChanged(engine, out)`,
which skips an engine whose registers, palettes, OAM and VRAM did not move, and
blits only the screen that was redrawn.

The swap stays unconditional on purpose: `gfxSwapBuffers` waits for the
display's refresh, and that wait is the only clock the emulated DS has (the
port raises one V-blank per `OnFrame`). Skipping it would make the game run
fast rather than save time; only the composite and the blit are skipped.

### What to read in the next log

- `audio: 32768 Hz stereo, 4 blocks of 512 frames` at startup, and
  `audio: N dropped frames of M` at exit.
- `draw_us` per frame now varies with what actually changed: near zero while a
  screen is still, the full composite when it moves. `total_us` is still the
  number that decides whether it feels smooth.

## Seventh run: 58.7 Hz and sound, and what the display fixes broke

The run reached the bedroom. Eleven samples put `total_us` at **17.0 ms
(~58.7 Hz)** with `logic_us` under 100 us, and the exit line read
`audio: 265 dropped frames of 61592` (0.4%). Both of the intended fixes
worked.

Three display bugs in the same run, two of them introduced by the per-engine
skip and one older:

1. **Flicker, and a stale screen.** The framebuffer is double buffered and the
   code skipped the *blit* of an unchanged engine. Frame N fills buffer A and
   shows it; frame N+1 skips the copy, swaps, and shows buffer B -- which still
   holds the picture from two frames ago. Every skipped screen therefore
   alternates between current and stale, which is exactly the intro flicker.
   Only the composite may be skipped; `DrawFrame` now copies both screens
   every frame.
2. **The field froze.** Engine A carries the 3D layer, and a new 3D image is a
   change even when that engine's 2D inputs are identical.
   `NitroPort_Render2DIfChanged` knows that, but only when its `uses3D` test
   `(dispcnt & 0x0108) == 0x0108` holds, and the field does not always set it.
   The shell now compares `NitroPort_Gx3dImageCount()` itself and calls
   `NitroPort_Render2D(0, ...)` whenever a new image exists. (Worth an upstream
   report: the port's own gate is what let a 3D scene go undetected.)
3. **The console was over the game.** `consoleInit(GFX_BOTTOM, NULL)` ran at
   startup, and `NitroPort_Log` writes to stderr, so every log line was drawn
   over the bottom screen -- visible in the owner's photo as scrolling text
   around the game's UI. The console now comes up only for the "can't start"
   screen.

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
