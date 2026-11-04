# Session 20 — Old 3DS performance, end to end

## What this session set out to do

The owner reported the New 3DS could not hold 60 fps and drained its battery,
and the Old 3DS was "a powerpoint slideshow": about **0.3 composes/s**, ten
seconds between a button press and the screen moving. The goal was to make the
Old 3DS playable, with the New 3DS kept as the higher-quality target and its
own profile untouched wherever a change was console-specific.

Everything below is measured on hardware unless it says otherwise. Three Old
3DS logs and four New 3DS logs were taken; the numbers quoted are from
`log-o3ds-soulsilver-6.txt` and `-7.txt` and `log-n3ds-0.txt`.

## Where it ended up

| | before | after |
|---|---:|---:|
| Old 3DS composes/s (field) | 0.3–3.6 | **10–14** |
| Old 3DS game frame | 13.7 ms (and 41 ms in the worst areas) | **2.3–9.2 ms** |
| Old 3DS drawer (both screens) | 2,000–4,500 ms/frame | **55–81 ms/frame** |
| Old 3DS render / publish | 1,360 / 550 ms | **39–59 / 16–23 ms** |
| Old 3DS geometry | 32.8 ms/scene, 97% busy | **19.9–24.7 ms/scene, 56–76% busy** |
| Old 3DS linear heap | 15.73 MB | **22.16 MB** |
| Old 3DS free for 3D textures | 408 KB | **7.6–8.1 MB** |
| Code image / artifact | 33.26 MiB / 9.40 MB | **29.77 MiB / 5.78 MB** |
| New 3DS | 68–71 composes/s at the title, 12 ms upper drawer | **30–32 composes/s, 59.7 fps, 5.7 ms/frame** |

The Old 3DS is no longer a slideshow. It is not 30 fps either: the field sits
around 10–14, and Goldenrod's outdoor area (≈10,700 vertices and ≈2,784 clipped
vertices per scene) is the outlier at 5–6. The remaining gap is the software 2D
renderer, not scheduling.

## The measurements that decided the work

The instrumentation added this session is what made the rest possible. Each
line exists because a guess had already been wrong:

- `FRAMETIME … hblank_us=… handoff_us=… hb_keep_us=… hb_dma_us=… hb_irq_us=…`
  splits `DrawFrame` into its phases. `hb_keep`/`hb_dma`/`hb_irq` split the
  H-blank walk into the per-line register copy, the DMA emulation and the
  interrupt dispatch.
- `[perf] mem: linear_free=… game_heap_used=…` — the two numbers the heap split
  is chosen from.
- `[perf] oam: top=A|B; A n shown, k identical, most-repeated tile T xM; B …`
  — whether duplicated sprites are in the sprite table or only in the picture.
- `[perf] core 1 share: asked 80%, the system allows N%` — read back with
  `APT_GetAppCpuTimeLimit`, because the first plan assumed 30% for a whole
  session and it was 80%.
- `LOG_LINE` 220 → 512: every `[perf] display` line in the first Old 3DS log was
  truncated mid-field, so the data needed to diagnose it was being thrown away.

## What landed

### Compiler

All 1,092 game objects were at GCC's default `-O0` (`port_cflags()` had no `-O`
at all; only the app objects and the vendored port libraries had `-O2`). They
are now `-O2`. Measured: game `.text` 8,146,868 → 4,512,808 B (−44.6%), artifact
9.40 → 5.76 MB. On a 64 MiB Old 3DS that freed ~3.4 MiB of committed image,
which goes straight to the linear heap.

`-O3` (+17% code, no measured gain), `-Os` (0.61 MB, slower hot code), LTO
(manual link + weaken/nm passes), `-mthumb` (ARMv6K has Thumb-1 only; GCC
refuses it with the hard-float VFP ABI) and `-ffast-math` were all evaluated and
rejected. `-fno-ipa-icf` was later added as a safety measure for
function-pointer identity; it did not turn out to be the cause of the sprite
bug, but it is cheap and the port passes callbacks around heavily.

### Memory

- `__system_allocateHeaps` never took its small-process branch on an Old 3DS:
  the test was `remaining > 16 MB * 2`, and `remaining` is 30.73 MiB, so it
  silently fell through to a 50/50 split. It now takes `remaining <= 48 MiB` as
  "small process" and gives the game 12 MB, the display the rest.
- The classic path's readback images and depth/IDs (1,152 KB) are allocated by
  the first `NitroPort_PicaRender`, not at startup; the remaster never calls it.
- The sun-shadow vertex room (1,438 KB) is opt-in through
  `NitroPort_PicaSetShadowRoom`, which the shell calls with `sCompose.shadows`
  (0).
- `ComposeConfig.texScale` plus a `texscale=` launch key: an Old 3DS asks for
  1 texel a pixel instead of allocating 13,824 KB of 2x layer sets, failing, and
  dropping the lower menu and weather BG on the way down.

### Display

- The display/compose thread runs on **core 1** on an Old 3DS at `0x32` (below
  the drawer); the New 3DS is unchanged. `displaycore=0|1` overrides it.
- The APT core-1 share is requested **before** any core-1 thread exists
  (`RequestCore1Share`), because the display thread is created before the
  geometry start-up used to ask.
- 1x band-diff publish (`publish_bands`) against the same per-target generations
  the 2x publisher uses; brightness-only publishes reuse a certified set's
  textures with no conversion, tiling or flush; arithmetic RGB565 (verified
  identical over all 32,768 table inputs) with the 64 KB table left only for the
  2x path.
- 2D renderer: empty-row skips, span-limited OBJ clears, wide fills, per-line
  window copy removed, opaque-pair merge (limited to `flags == 0`, so it does not
  fire on the upper split screen's `T_LAYER`).
- 3D command build: per-batch C3D state caching and alpha-31 texture hoisting.
  **Batch merging and the stencil change are permanently out** — see below.
- Audio mixer: silent-channel skip, hard-pan hoisting, full-volume shortcut, all
  bit-exact. The requested `step == PHASE_ONE` fast path was proved unreachable
  at 32,768 Hz (`SOUND_CLOCK / 32768` is not integral) and deliberately not
  added.
- Geometry clipping/projection converted from `double` to `float`. On this CPU
  double is soft-float; this is the single largest geometry win
  (32.8 → 19.9–24.7 ms/scene).

## The bugs, and what each one taught

### The H-blank walk was switching threads 192 times a frame

**Symptom:** `hb_irq_us` was 11.8–12.8 ms whenever the game armed H-blank, and
8 µs when it did not. The game frame reached 41 ms; the drawer and display
starved to 0.3–1 composes/s.

**Wrong turns:** first it was blamed on the game's H-blank handlers
(`HBlankSystem_Callback`), then on `NitroPort_GxRefresh()` re-raising the GXFIFO
interrupt per line. Hoisting the refresh changed nothing, which disproved the
second.

**Cause:** `NitroPort_IrqThreadSwitch` ran once per scanline. The switch path is
what cost the time, not the handlers (`hb_keep` 0.5 ms, `hb_dma` 0).

**Fix:** defer the switch across the walk (`NitroPort_IrqSwitchDeferred`): each
raise still dispatches its handler and does its bookkeeping, the switch request
is noted, and one switch runs when the walk ends.

**Result:** `hb_irq_us` 12,751 → 411 µs; game frame 41 ms → 2.3–8.1 ms; drawer
1.1 fps → 12.1 fps.

**Rule:** for a synthetic interrupt storm, profile *inside* the interrupt, not
just the handler. `svcGetSystemTick` around each phase is cheaper than a wrong
rewrite of the walk.

### Batches merged across a texture change duplicated geometry

**Symptom:** NPCs and characters duplicated across the field, walkable, changing
pose with the real sprite; rectangular halos around street lamps and the
player's shadow. Present on both consoles.

**Wrong turns:** the 2D renderer was reverted first (the duplicates looked like
sprites), and `-O1`/`-fno-ipa-icf` were tried. None changed anything.

**Evidence that settled it:** the OAM probe showed the top engine's sprite table
was pure `0x2C0` fill while characters were visible — so they were not sprites.
The 3D command-build slice was the only remaining change on that path.

**Cause:** the slice's opaque batch merging and the stencil-enable condition.
Merging changes which vertex ranges one `draw_arrays` covers; the stencil change
removed writes the rest of the pipeline relied on.

**Fix:** revert both permanently; keep state caching and texture hoisting, which
touch neither draw ranges nor state validity.

**Rule:** batch merging is only safe when the state tuple *and* the draw order
semantics are provably identical. A `LEQUAL` guard is not enough; the stencil
and the per-batch texture binding are part of the state too.

### A live thread was freed at teardown

**Symptom:** the first performance build died on both consoles at a clean-looking
moment.

**Cause:** `WaitForGame` loops on `sGameThreadAlive`, which the game thread sets
as its first instruction. A new thread has not necessarily run when
`threadCreate` returns, so the loop read `false`, printed "the game's thread
ended at frame 0", fell through to `threadJoin(…, 2000 ms)` (which blocked while
the game reached frame ~110), then `threadFree`d a running thread. Teardown then
unhooked APT and exited NDSP/gfx under live game code.

**Fix:** set `sGameThreadAlive` before `threadCreate` (and back to false if it
fails); free the thread only when the join succeeded; a timed-out join sets
`sThreadsStuck`, which makes the caller `svcExitProcess`.

**Rule:** a "did this thread start" flag must be set by the creator, not by the
thread. Never `threadFree` after a timed-out join.

### The native tree was missing five patches

**Symptom:** a data abort in `WirelessStatusIcon_LoadChar` on the raw
`0x04000000` read that patch 1905 replaces; Settings-close and PC-box fixes
absent.

**Cause:** `build/upstream` had never had 1904, 1905, 1909, NitroSDK 0605 or
NitroSystem 0304 applied. The container's tree had 87 commits, the native one
85. `check-bootstrap` only tested that a header existed.

**Fix:** apply the five with `git am` (SDK ones with `--directory=lib/…`):
86/86 patches, 87 commits, identical subject set to the container.

**Guard:** `tools/check_patches.py`, wired into `make check-bootstrap` and into
`tools/game.py` itself (the native workflow invokes it directly, bypassing
`make`). It compares each patch's `git am` commit subject with the tree's
history. `git apply --check` cannot be used: on a cumulative stack a later patch
rewrites the same lines, so an applied patch fails both its forward and reverse
check.

### A vendored archive did not rebuild after a port-source edit

**Symptom:** the link failed on a symbol that plainly existed in the edited
source; later, a behaviour fix appeared not to work.

**Cause:** `libnitro.a` was not rebuilt for the changed `port_os.c` in an
incremental run. The app objects are compiled by the Python tool and are always
fresh; the vendored archives are not guaranteed to be.

**Fix/rule:** after editing anything under `3ds_port/nitro/src`, delete
`build/<out>/sdk` (both game trees) before building. A stale archive produces a
build that silently contains last week's code.

### The OAM diagnostic lied (mine)

**Symptom:** `A 0 shown` for an engine that clearly had sprites.

**Cause:** attr0 bit 9 means "disabled" only when bit 8 (affine) is clear; for an
affine sprite it is the double-size flag. HGSS draws its field sprites affine,
so the filter discarded every one.

**Rule:** when a diagnostic reports an impossible state, suspect the diagnostic
before the system.

## What is still open

1. **The 2D renderer is the remaining wall** (39–59 ms/frame for both screens).
   The field sits at 10–14 fps because of it. Halving it reaches ~20; the
   realistic route to 30 is GPU-side 2D (PICA quads per layer), the plan's
   Phase 11.
2. **Goldenrod outdoors** — geometry still 19–29 ms/scene with ~2,784 clipped
   vertices; the display shares core 1 with it and starves there.
3. **Audio on core 1** — once geometry has headroom, its 27% of core 0 is
   ~27% more drawer time. It was rejected earlier *because* geometry saturated
   core 1, which is no longer true.
4. **In-between-scene matching** is still `double` (the only remaining
   soft-float hot path) and was deliberately left alone.
5. The `[hitch]` lines in Goldenrod still show 100–370 ms display frames and a
   167 ms "clears" line that has not been explained.

## Files worth knowing

- `tools/check_patches.py` — patch-stack completeness guard.
- `tools/hgss_game.py` `port_cflags()` — the `-O2` and `-fno-ipa-icf` decision
  lives here, with the measurements that justify it.
- `hgss/game/app/main_3ds.c` — heap split, thread placement, `[perf]` lines.
- `3ds_port/nitro/src/port_os.c` — H-blank walk, `raise_irq`, the split timers.
- `3ds_port/nitro/src/port_thread.c` — `NitroPort_IrqThreadSwitch` deferral.
- `3ds_port/game/app/port_compose_pica.c` — publish paths, `publish_bands`.
- `3ds_port/nitro/src/port_gx3d.c` — clipping/projection (now float).
