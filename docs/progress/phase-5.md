# Phase 5 progress

Status: **runs on hardware; the 3D camera-plane drop, 60 Hz cap and audio
level/staging fixes package, pending a console re-run.** The game boots, plays
the intro, reaches Oak's speech and the player's house, draws both screens and
plays audio. The ninth run (log-9) showed the GPU pipeline working at 87 Hz;
frame starts are now capped at 60 Hz, the NNS volume table, pre-saturation
6 dB headroom and NDSP cache/rate fixes address the audio, and the title's
zero-area camera-plane triangles are dropped instead of being drawn as a fan.
Remaining Phase 5 exits: the owner's hardware re-run (3D rate and title, audio
by ear, frame times), a full-speed run through Oak's speech, the save
round-trip, the Old 3DS decision and the `.cia` decision.

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
- **Audio** starts only when `dspfirm.cdc` loads. `AudioThread` feeds the DS
  mixer (`NitroPort_SoundRender`, 32768 Hz stereo) into NDSP in four 512-frame
  blocks, and `NitroPort_OnFrame` pumps the ARM7's alarm replies on the game
  thread. The exit log prints `audio: N dropped frames of M`. Without
  `dspfirm.cdc` the game runs silent, `log.txt` says so, and nothing else
  changes.
- **Build** `python3 tools/hgss_game.py --target 3ds --out …` links 1092/1092 with 0 unresolved and packages `pokeheartgold.3dsx` (**9,278,696 bytes** with the latest camera-plane-drop/audio-table/pre-saturation-headroom/NDSP-cache fixes) plus `pokeheartgold-3ds.smdh` (smdhtool + the generic placeholder icon). It also compiles Platinum's classic PICA backend (`port_gx3d_pica.c`, `port_pica_abi.c`) and the shader blob from the three vendored `.v.pica` files; see the GPU-pipeline and camera-plane sections below.
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

### Session summary: eight runs, eight bugs

The first hardware session found, in order: a folded relocation the console's
Loader rejected; `main()` killing a still-initialising game through
`aptMainLoop`; a `threadJoin` that reported an end while the thread ran; a
weak hook that bound to NULL so nothing was ever drawn; the missing New 3DS
clock (13 Hz); the double-buffer blit rule (flicker); engine A's 3D layer
(black field); and the console painted over the bottom screen. Each has an
entry with symptom, cause and rule in
[../wiki/3DS-Pitfalls.md](../wiki/3DS-Pitfalls.md).

Per-run results, condensed:

| Run | Result |
| --- | --- |
| 1 | Loader `udf #0`; the `.3dsx` held one impossible relocation |
| 2 | Started, then stopped at frame 0 — `aptMainLoop` polled from the wrong thread |
| 3 | `threadJoin` lied; the shell's own flag was added |
| 4 | 39,070 frames, black screens: weak hook bound to NULL |
| 5 | 13.2 Hz: New 3DS clock never requested |
| 6 | 35.7 Hz after the speedup |
| 7 | 58.7 Hz with audio (0.4% dropped) — and three display bugs |
| 8 | Flicker and console gone; field 3D still black, rule fixed |

### Frame-time instrumentation (P5-C)

Both shells log one sample per 600 frames (60 on the PC with `--frametime`):

```text
FRAMETIME frame=1234 logic_us=820 draw_us=9100 audio_us=0 total_us=16720
```

The 3DS measurement is the interval between `NitroPort_OnFrame` calls (the
game's own work), the time inside `DrawFrame`, and the same for the PC. Audio
is mixed on its own `AudioThread`; `audio_us` is still 0 because the shell
does not time that thread. The latest tree caps frame starts at 60 Hz, so a
healthy `total_us` is near 16,700, not the 11,500 of the uncapped log-9 run.

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

- `audio: mixer 32768 Hz, DSP 32728 Hz (no resampling), 4 blocks of 512 frames` at startup, and
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

## Eighth run: flicker and console gone, the 3D still missing

The owner confirmed no flicker and a clean bottom screen, and that the run got
through Oak's intro to the player's house. The remaining fault is the 3D
environment: the house's background is black while the 2D text box ("Hi, Al!
You're still awake.") draws correctly. So 2D composition is fine and the 3D
layer is not reaching engine A.

The previous build tried to detect new 3D images from outside by watching
`NitroPort_Gx3dImageCount()` and forcing a compose when it moved. That cannot
work: `sPublishCount` only advances when the port *publishes* an image, and
publication is lazy -- it happens as part of a composite request. Nothing
outside the port can see a pending image, so the force never fired, and the
port's own `uses3D` test (`(dispcnt & 0x0108) == 0x0108`) does not hold for
this scene either.

Rule now: **engine A is composed every frame, exactly as
`NitroPort_RenderScreens` did** (which is the configuration that rendered the
3D correctly in the earlier runs). Engine B cannot show 3D at all, so it is
the only one skipped when its inputs are unchanged -- that is where the
savings come from, and it cannot hide a 3D layer.

## Ninth slice: the GPU pipeline (built; pending the owner's hardware run)

The eighth run left the 3D environment black while the 2D text drew. The cause
was not the composite: on the 3DS `NitroPort_Gx3dAcquireImage` can only convert
an image a GPU worker already handed over with `NitroPort_Gx3dSubmitRGBA8`. The
HGSS shell had no worker, so the published index stayed `-1` and engine A drew
only its 2D layers. The `[gx3d] ... scenes: N vtx` lines came from the scene
builder; they were never evidence that an image existed.

The shell now runs Platinum's **classic** pipeline, the same architecture its
non-remaster path uses:

- **Geometry thread.** `NitroPort_Gx3dStartThread` (LightEvent ops) plus
  `NitroPort_Gx3dThreadMain`; on a New 3DS it runs on core 2, and falls back to
  the game's core if the kernel refuses it. `NitroPort_OnFrame` calls
  `NitroPort_Gx3dFlush` once a frame, before the composite, exactly as both PC
  and 3DS shells do.
- **GPU thread.** `NitroPort_PicaInit`, then a loop over
  `NitroPort_Gx3dAcquireScene` → `NitroPort_PicaRender` →
  `NitroPort_Gx3dReleaseScene`, converting the handed-over image on that
  thread. It wakes from a strong `NitroPort_Gx3dScenePublished` override (the
  port's default is a weak no-op).
- **Links.** `3ds_port/game/app/port_gx3d_pica.c` and `port_pica_abi.c` are
  compiled into the program. The ABI file is built without the port's
  `-fno-short-enums` so its `DVLE_s`/`C3D_FrameBuf` match libctru's. The
  shaders are the three vendored `.v.pica` files assembled by `picasso` and
  embedded with `bin2s` (both already in devkitPro; no new dependency).
  `hgss/game/app/pica_classic_stubs.c` supplies the two compose-only profiler
  hooks the vendored file references.
- **Fallback.** `NitroPort_PicaInit` or a thread failing is logged
  (`[gx3d] 3D setup failed …`, `[gx3d] couldn't start the 3D thread …`, or
  `[gx3d] no geometry thread …`) and the shell keeps the old 2D-only
  behaviour. It does not crash or run a half-built pipeline.
- **Evidence in the log.** Startup: `[gx3d] PICA ready (classic path)` and
  `[gx3d] geometry on its own thread`. Every 600 frames:
  `3D images: N (scenes drawn: M)`. Exit:
  `[gx3d] N 3D images, M scenes drawn` before the usual `stopping` line.

**Before**, from the eighth run's log (log-7, the current pre-slice build):
2 samples, `draw` mean 26.5 ms, `total` mean 24.7 ms → **40.5 Hz**, `logic`
60 µs; the run was stopped at frame 1377. **After** is pending the owner's
hardware run; record `tools/frametime_report.py log.txt` on the new artifact
and compare the same scene.

**Local verification (container, 2026-11-01).** The 3DS build compiles
1092/1092 with 0 unresolved and packages `pokeheartgold.3dsx` (9,300,752 B,
+197,024 B over the 2D-only build). `3dsxdump` exits 0 (CODE 2131, RODATA 113,
DATA 13, BSS 4446 pages) and the harness's `validate_3dsx` accepted the
packaged file. The PICA symbols are `T` in the ELF, including a strong
`NitroPort_Gx3dScenePublished`; `NitroPort_PicaInit`, `PicaRender` and
`PicaExit` are present. `make test` is 327 tests plus 21 SDK suites, all green;
`make port-tests` is 17,122 checks, 0 failed, exit 0. Artifact
`sha256 d494b06991745115313217990b326ebac8b5a4d602696c211fdd163fe7298248`,
staged at `sd-card/3ds/pokeheartgold.3dsx`. None of the visual or frame-time
claims are verified: the container cannot run a console.

## Ninth hardware run: 3D works -- and the worker was starved

The staged artifact ran on the owner's New 3DS (log-8). It does what the slice
was for: the lower-screen title now shows Ho-oh, the game reaches the field,
and the player can walk through the house and outside to Lyra and Marill. Two
defects showed up in the same log and photo:

- **The 3D updated at about 1 fps.** `3D images:` reached **105 in 4,680
  frames** (about 1.1 a second) while the geometry builder made 120 scenes
  every 240 frames (30 a second). The GPU/display thread was created on the
  game's core, one priority below the game; the 2D composite holds that core
  for 20-30 ms of a frame, so the worker only ran in a V-blank wait that a
  long frame never reaches. The game's thread converted each image itself.
- **The title's Ho-oh had black shards fanning from the screen centre.** The
  port clips to ±x/y/z <= w; a vertex at `(0,0,0,0)` satisfies all six and
  survives. The GPU vertex is `(ndc * w, w)`, so `w = 0` becomes
  `(0, 0, z, 0)` and the PICA's divide puts it at the centre.
- **Audio dropped frames and felt slow** during the intro: `audio: 82 dropped
  frames of 23821` (0.34%), and the audio thread (`0x31`) was below the game
  and shared the game's core with the starved GPU worker.

The frame-time samples: 7 samples, `total` mean **24.7 ms (40.4 Hz)**, with
the field at 19.8-20.9 ms (48-50 Hz) and the intro at 31-36 ms (28-32 Hz).

**Fixes (next artifact).**

1. The GPU/display thread goes on **core 2** on a New 3DS (core 0 only if the
   kernel refuses it), so its render overlaps the game's software composite
   instead of queueing behind it.
2. The app objects are compiled at `-O2` (the harness was building them at
   `-O0`; the port archives were already `-O2`).
3. `hgss/game/app/pica_scene.c` degenerates stored triangles with a `w <= 0`
   vertex before `NitroPort_PicaRender`; the count is in the log as
   `camera-plane drops`. **(Superseded twice: the `w <= 0` drop left a
   transparent gap in Ho-oh, and the `w = 1` / `w = 2^18` port workarounds
   only changed the fan's shading. The final fix drops the exact zero-area
   camera-plane triangles in `stage_triangle()`; the file and the log field
   are gone. See below.)**
4. The audio thread moves to priority `0x2e`, above the game (`0x30`) and the
   geometry thread (`0x2f`).

The next log should show `[gx3d] display on core 2`, `3D images` near 300 per
600 frames rather than 10, **no** `camera-plane drops` field (the workaround
is gone; the port drops zero-area triangles itself), and near-zero audio
drops. The after frame times are still the owner's run.

**Local build for that run:** 1092/1092, 0 unresolved; `pokeheartgold.3dsx`
9,277,656 B (`-O2` shrank it from 9,300,752); `3dsxdump` rc 0 (CODE 2126,
RODATA 112, DATA 13, BSS 4446 pages); `make test` 327 tests OK + 21 SDK
suites; `make port-tests` 17,122 checks, 0 failed; the new host test
`tests/game/pica_scene/run.sh` passes. Staged artifact
`sha256 ce683574cfe912d4896a6db18089c6b53773bfd893e01a2c0b2138f9e9ad4afd`.

## The 60 Hz frame cap: log-9 ran at 87 Hz

The run after the GPU-worker fixes (log-9) proved the pipeline but exposed a
clock problem: `tools/frametime_report.py` over 40 samples reported mean
`total_us` **11.49 ms**, **87.0 Hz**, with `logic_us` under 1 ms. The game and
its scripted cutscenes therefore ran **1.45x fast**.

The emulated DS's only clock is the V-blank the port raises once per
`NitroPort_OnFrame`; the call spacing used to come from `gfxSwapBuffers`' wait
for the display. That wait ends once the previous frame has been presented, so
as soon as the composite fit under one refresh it returned early and the loop
free-ran. A frame that fits under the refresh is not a 60 Hz frame.

`NitroPort_OnFrame` now ends with `WaitForFrameCap`: frame starts are held to
a wall-clock grid of `SYSCLOCK_ARM11 / fps` ticks from `svcGetSystemTick()`,
and only the remainder is slept with `svcSleepThread`. It is a deadline, not a
second V-blank — a frame that already overran returns immediately — and if the
frame is more than one period behind, the grid restarts from now instead of
bursting to catch up. Both blits and `gfxSwapBuffers` stay unconditional and
ahead of the wait, so `draw_us` still measures only the draw.

`fps` in `port.ini` selects the cap: **60** (the default, the game's intended
speed) or **30**. 30 is a comparison mode and **halves the game's speed**,
because the game clock advances once per frame; missing or invalid values use
60. Startup logs `[perf] frame cap 60 Hz`. Every 600 frames the existing
`FRAMETIME` line keeps its shape; at 60 Hz `total_us` should settle near
**16,700** (the period is 4,468,530 ticks = 16,667 us, 60.0 Hz) with
`draw_us` unchanged, and a mean near 11,490 is the symptom the cap was added
for.

**Local verification (container, 2026-11-01).** `make test` **329 tests, OK**
(the two new shell-text tests) plus the `tests/sdk/*` suites, exit 0. The 3DS
build compiles **1092/1092** with **0 unresolved** and packages
`pokeheartgold.3dsx` (**9,278,584 bytes**); `3dsxdump` exits **0** (CODE 2126,
RODATA 112, DATA 13, BSS 4446 pages) and the ELF carries
`[perf] frame cap %d Hz`. Artifact
`sha256 a25cbee94bbe59a3c496a6734cc5c7777d04f3e99b0cacf3f0f02f3a3cdaf910`.
*(Historical session-8 artifact; see the final verification at the end of this
page for the current one.)*
None of the timing claims are verified: the container cannot run a console.

## The hung noise note: a missed 0x400 offset

The audio symptom after the hardware runs was a note that hangs and is only
replaced by the next note, with individual sounds and slower songs/percussion
distorted. The cause was in the ARM7 driver, not the mixer. Patch 1405 moved
`NitroPort_SoundRegisters` to the DS low 16-bit address (channel *c*'s control
at `0x400 + 16c`, timer at `0x408 + 16c` — the addresses
`port_sound_chip.c`'s `CHANNEL_BASE` reads) but missed the two direct writes in
`SND_SetupChannelNoise` (`sub/lib/src/sound_channel.c:127,133`). They kept the
pre-1405 relative index, so a noise note wrote DS `0x040000E0`/`0x040000E8` —
an unused hole inside the array — and left the channel's previous control word
alone. If that word came from a looped PCM (enable bit 31 and repeat bit 27
set), the mixer kept reading the stale PCM control and retriggering its loop
until a later PCM setup replaced it: the note that hung until the next note.

Patch **1407** writes `0x400 + channelOffset` and `0x408 + channelOffset`,
matching the Psg branch, and leaves the `#else` DS text untouched (`gcc-10 -E
-P` without `SDK_PORT_3DS`: **0 diff lines**). A grep of the ARM7 sound
sources confirms no other `NitroPort_SoundRegisters` access lacks its
`0x400`/`0x500` base. `tests/sdk/arm7_sound` now leaves a running looped PCM on
noise channels 14 and 15 and checks the noise write: the control/timer at
`0x4E0`/`0x4E8` and `0x4F0`/`0x4F8` become the noise word (format 3, enable
and repeat clear), while `0x0E0`/`0x0E8` and channel 0 keep their sentinels.
Before the patch that suite reports 24 failures, including the stale
`0x88400040` control; after it, 122 checks, 0 failures.

## The NNS volume table: the note that still hung after 1407

The hardware run after 1407 still had a note held at full volume until the
next one, and the mix clipped. The remaining cause was outside the ARM7
driver and the mixer: NitroSystem's `_0210D750` maps a linear 0..127 player
volume to decibels (`lib/NitroSystem/src/snd_player.c`), and in the DS build
it is an **empty alias section** (`lib/asm/nitro_sdk_data.s`) that the DS link
order places immediately before `SNDi_DecibelTable`; the two names share the
table. The port's link order put the empty section on an unrelated pointer
table, so NNS read pointer bytes as decibels: the external fader became
+25.6 dB, every channel clamped to 0 dB, and the release stop
(`volume <= -723`) could never fire.

`hgss/compat/src/nns_volume_table.c` supplies the real table;
`tools/hgss_game.py` drops the empty `.rodata.alias_d750` section from the SDK
data object and checks the linked 256-byte table. A PC trace at the title
went from 14 channels at 0 dB and 4 natural ends in 90 s to volumes 41-103,
192 ends, no saturated 50 ms window and real quiet gaps. `make test` adds
`tests/test_hgss_game.py::NnsVolumeTableTests`; the DS gate never runs the
port's object step. See [agent-notes/s13-nns-volume-table.md](agent-notes/
s13-nns-volume-table.md) and
[3DS-Pitfalls](../wiki/3DS-Pitfalls.md#the-nns-volume-table-is-an-empty-alias).


**Then the output level.** The notes were right, but the title was still about
6 dB hotter than a calibrated melonDS capture (DS peak ≈0.42/RMS 0.071, port
1.0/0.139). `NitroSound_Mix` now applies the 6 dB headroom **before** its
16-bit saturation (`outputShift`); the first `NitroPort_SoundRender` trim ran
after saturation and left flat-topped peaks at ±16384, so the crunch survived.
PSG duty 7 is also silence (GBATEK 0%) rather than a 100% DC level.

**Then the NDSP staging.** The mix was level-right but still rough on hardware.
`trim.wav` was clean except the flat tops, so the remaining roughness was
after it: HGSS's shell never flushed a rendered block before
`ndspChnWaveBufAdd`, and the DSP's DMA cannot see the ARM11 write-back L1 (it
read stale FCRAM fragments: recognizable music, scratchy). It also announced
32768 Hz to a 32728.5 Hz DSP, asking libctru for a 1.0012 two-tap resample; its
fractional gain swept at 39.5 Hz over the top octave. `main_3ds.c` now flushes
each 2048-byte block and announces `NDSP_SAMPLE_RATE` (0.12% / two cents flat,
inaudible). The independent ADPCM/interpolation audit found no decoder defect;
the sound-chip suite is now 81 checks (exact ADPCM samples and a smooth-loop
test included).

## The camera-plane triangle: zero area, not a wing

S10 and S11 read the title's fan as Ho-oh's left wing and tried to draw it
faithfully: first give the exact `(0, 0, 0, 0)` vertex a positive `w`
(`clip[3] = 1`), then make its `1/w` negligible. The hardware kept showing a
deformed bird, and the user pointed out that the PC reference was deformed too.
A real DS title capture (melonDS 1.1, retail ROM, the user's DS BIOS) settles
it: Ho-oh is intact, with no fan and no gap. The fan is our software
rasteriser's artifact, not the game.

The vertex is on the **camera plane**, `(0, 0, 0, 0)`. Both edges from it to a
real vertex project to that vertex's screen point, so a triangle with one of
these has **zero screen area**; the DS's clipper stores no polygon for it. The
software rasteriser stored its screen as `(0, 0)` and gave the triangle real
area, which fanned the left wing out of the corner. `stage_triangle()` now
drops exactly those triangles (`camera_plane()`), before either the software
reference or the PICA scene sees them. Dropping every `w <= 0` triangle (the
S10 workaround) also removed real near-plane-crossing wing geometry, which is
why it left the transparent gap.

`3ds_port/` is deliberately no longer byte-identical to `platinum.lock`; the
upstream PR content is the `camera_plane()`/`stage_triangle()` hunk plus
`test_camera_plane_triangle` in `nitro/tests/test_gx3d.c`. The audit is
[agent-notes/s13-nns-volume-table.md](agent-notes/s13-nns-volume-table.md);
the earlier reasoning is kept in
[agent-notes/s11-3d-camera-point.md](agent-notes/s11-3d-camera-point.md) for
history.

**Local verification (session 10, container, 2026-11-01).** `make test` **338
Python tests**, OK, plus 20 SDK suites; `make port-tests`: sound chip **81**,
gx3d **327 + 229**, nitrosystem **17,122 checks, 0 failed**; host32 and 3DS
1092/1092, 0 unresolved; 3DS `pokeheartgold.3dsx` **9,278,696 bytes**
(`sha256 fc4472b59eada839e31e5d46f2c7e58dd285989c924e561a6420d0f2a0eb68a7`),
`3dsxdump` rc 0 (CODE 2126, RODATA 112, DATA 13, BSS 4446 pages). The PC audio
render is 0.0708 RMS, 0.586 peak, with no flat tops; the PC title frame 4800
shows the complete bird; the DS capture is the visual reference. The DS
matching gate remains at **78 patches** for both games (HG `4fcded0e…b5a37`,
SS `f8dc38ea…e582`). The PICA's title and the audio are the owner's hardware
check.

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

- The classic PICA pipeline runs on hardware; the latest **60 Hz cap, NNS
  volume-table/pre-saturation-headroom/NDSP-cache audio fixes and camera-plane
  drop** are pending the owner's re-run (frame times, no clipping or rough
  timbre, no title fan or gap).
- Old 3DS 3D: the classic target and images need several MB of linear memory;
  `NitroPort_PicaInit` logs and falls back to 2D if it cannot get it, but the
  Old 3DS case is still unmeasured.
- `--sanitize` requires `libstdc++` at link time only for the ASan runtime;
  the normal link stays libstdc++-free.
- SMDH: `smdhtool` can package a generic, non-ROM icon; the cart-derived icon
  and banner are generated locally and never committed.
- `3ds_port/` now carries one deliberate upstream-style fix (the camera-plane
  drop in `stage_triangle()` plus `test_camera_plane_triangle`); `platinum.lock`
  is unchanged and the PR content is recorded in
  [decisions.md](../decisions.md).
