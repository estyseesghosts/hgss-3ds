# Session 19 — Old 3DS performance plan

## Status

Investigation only. **No tracked source was changed by this session.** Every
number below is tagged *measured* (a log line, a parsed binary, or a source
reference) or *estimated* (a cost model, with its assumptions stated). The wave
plan in [§6](#6-the-wave-plan) is not yet implemented.

## Implementation status (updated as the waves land)

| Wave | Result |
| --- | --- |
| W0 instruments | **Landed and used.** `LOG_LINE` 512; 32 KB stdio buffer with a once-a-second flush plus explicit flushes at `[perf]` and on exit; one timestamp anchor for `total_us`/`state_us`; `hblank_us`/`handoff_us` and the `hb_keep`/`hb_dma`/`hb_irq` split; `[perf] mem:`; `[perf] oam:`; the accepted core-1 APT share. Every line exists because a guess had already been wrong. |
| W1 compiler | **Landed.** `-O2` for the 1,092 game objects (plus `-fno-ipa-icf`). Image 33.26 → 29.77 MiB; artifact 9.40 → 5.78 MB; game `.text` −44.6%. |
| W2 memory | **Landed.** Explicit small-process split (12 MB game), lazy classic readbacks (1,152 KB), opt-in shadow room (1,438 KB), explicit `texScale` with a `texscale=` key. Linear heap 15.73 → 22.16 MB; free for 3D textures 408 KB → 7.6–8.1 MB. |
| W3 game-thread cost | **Solved, and not where the plan expected.** The cost was `NitroPort_IrqThreadSwitch` running once per scanline, not the H-blank handlers or the GXFIFO refresh. Deferring the switch across the walk took `hb_irq_us` 12,751 → **411 µs** and the game frame 41 ms → **2.3–8.1 ms**. |
| W4 2D renderer | **Landed.** Empty-row skips, span-limited OBJ clears, wide fills, per-line window copy removed, opaque-pair merge. The merge is limited to `flags == 0` and so does not fire on the upper split screen (`T_LAYER`). Render 80 → 39–59 ms/frame. |
| W5 publish | **Landed.** 1x band-diff, brightness-only publishes, arithmetic RGB565 (proved identical over all 32,768 inputs). Publish 44 → 16–23 ms. |
| W6 threading | **Landed.** Display/compose on core 1 at `0x32` on an Old 3DS (`displaycore=`), New 3DS unchanged; the APT core-1 share is requested before any core-1 thread exists. |
| 3D command build | **Partial by design.** State caching and alpha-31 texture hoisting kept; **opaque batch merging and the stencil change reverted** — on hardware they produced duplicated geometry and rectangular halos. |
| Audio mixer | **Landed.** Silent-channel skip, hard-pan hoisting, full-volume shortcut, all bit-exact. The brief's `step == PHASE_ONE` path was proved unreachable at 32,768 Hz and not added. |
| Geometry clipping | **Landed (added during the work).** Clipping/projection converted from `double` (soft-float) to `float`: 32.8 → 19.9–24.7 ms/scene, core-1 busy 97% → 56–76%. |

The full session record, including every wrong turn and its evidence, is
[agent-notes/s20-performance-session.md](s20-performance-session.md).

The owner's target, in their words: the Old 3DS must run the game well, and at
minimum match or exceed what the original hardware did. 30 fps presentation at
full game speed is the accepted interim target; 60 fps is a later project.

## 1. Evidence base

| Source | What it is |
| --- | --- |
| `/Users/terezi/hgss-private/log-o3ds-soulsilver.txt` | Old 3DS, SoulSilver, 149 lines, current shipped build |
| `/Users/terezi/hgss-private/log-soulsilver-normal-playthrough.txt` | New 3DS, SoulSilver, 5,229 lines, same build |
| Five read-only audits | compiler/build flags, CPU hot paths, GPU/compositor, memory/VRAM, threading/frame pacing |
| Source reads by the lead | `main_3ds.c`, `port_compose_pica.c`, `port_gx3d_pica.c`, `port_os.c`, `port_render2d.c`, `tools/hgss_game.py` |

Where an audit disagreed with the log, the log wins and the disagreement is
recorded in [§5](#5-what-is-not-settled).

## 2. The three facts the plan rests on

### 2.1 The process is exactly 64 MiB, and 33.26 MiB of it is our own image

*Measured* by parsing the `.3dsx` header of `build/game-3ds-ss/pokesoulsilver.3dsx`:

| Segment | Bytes | MiB |
| --- | ---: | ---: |
| code | 8,820,940 | 8.41 |
| rodata | 466,332 | 0.44 |
| data (includes `.bss`) | 25,588,616 | 24.40 |
| **image** | **34,875,888** | **33.26** |
| heaps (log: `2 × 15,732 KB`) | 32,219,136 | 30.73 |
| **total** | | **63.99** |

64 MiB is the maximum an application process gets on an Old 3DS, so **there is
no "request more memory" lever** — the earlier idea of a 64 MB loader mode is
already in effect. The only levers are our own image size and how the remaining
memory is split.

Of the 24.40 MiB of `.bss`, about 8 MiB is `NitroPort_MainMem` alone
(`3ds_port/nitro/src/port_memory.c:19`), plus ~5.2 MB of compositor shadows,
~2.1 MB of publish shadows, ~1.8 MB of 3D scenes and ~1.9 MB of 2D working
state. BSS is zero-filled but it is still **committed** and therefore still
taken out of the same 64 MiB.

### 2.2 The heap split never does what its comment says on an Old 3DS

*Measured* in source and arithmetic (`hgss/game/app/main_3ds.c:109-141`):

```c
#define HEAP_SMALL_TOTAL (48u << 20)
#define HEAP_SMALL_GAME  (16u << 20)
if (remaining < HEAP_SMALL_TOTAL && remaining > HEAP_SMALL_GAME * 2u) { ... }
```

`remaining` is 30.73 MiB, which is **below** the `16 MB * 2` lower bound, so the
"small process: game 16 MB, display the rest" branch is skipped and the code
falls through to a 50/50 split: 15,732 KB game heap / 15,732 KB linear heap. The
log confirms the half split, and the game heap was using **798 KB** at startup.

### 2.3 The 3D texture cache is 408 KB

*Measured*, `log-o3ds-soulsilver.txt` lines 8-20:

```
[gx3d] 3D setup: linear heap 14149 KB free of 15732 KB
[compose] linear memory: 9873 KB free
[compose] not enough memory for the sharp 2D on the upper screen: smooth instead
[compose] not enough memory for the sharp 2D on the lower screen: smooth instead
[compose] not enough memory for the weather's BG on the GPU: drawn with the 2D
[compose] 168 KB of linear memory left for the 3D's textures: the display made smaller
[compose] the display made smaller for the memory (408 KB left)
```

The ledger sums exactly to 15,732 KB:

| Consumer | KB | Verdict |
| --- | ---: | --- |
| pre-existing linear (audio/NDSP/etc.) | 1,583 | needed |
| PicaInit: citro3d command list | 1,024 | oversized |
| PicaInit: classic readback images + depth/IDs | 1,152 | **dead in the remaster** |
| PicaInit: 2 vertex buffers incl. shadow room | 2,100 | 1,428 is shadow-only room |
| upper 2D sets, 3 × (512×256×2 + 512×256×4) | 2,304 | 1×, power-of-two, 320×192 used |
| lower 2D sets, 3 × (256×256×2 + 256×256×4) | 1,152 | 1× |
| LCD framebuffers, 3 upper × 2 eyes + 3 lower | 1,575 | right eye never used (`stereo 0`) |
| lower-menu + map textures | 768 | **HGSS never uses them** |
| weather BG textures (3 × 532,480 B) | 1,560 | kept |
| quad vertices | 58 | needed |
| two per-frame command lists (2 × 1 MB) | 2,048 | oversized |
| **free** | **408** | whole 3D texture cache |

The fallback ladder kept the weather BG (1,560 KB) and the unused lower menu
(768 KB) while accepting 408 KB for the 3D texture cache. That is the direct
cause of `[hitch] ... building (20 textures converted: 8907.4 ms)` at frame
1204: with 408 KB, a 512×512 RGBA5551 texture (512 KB) can never be resident, so
the cache evicts on nearly every unique texture and `settle()` waits for the GPU
on each eviction.

**Sharp 2× cannot fit on an Old 3DS.** The 2× layer sets alone are 13,824 KB
(upper 9,216 + lower 4,608), and a minimum 2× compose is ~17.5 MB — more than
the entire 15.7 MB linear heap, before Pica and gfx. This is arithmetic, not a
tuning question. (The current build already falls back to 1×; the startup banner
still prints "2D sharp" because it prints the requested filter, not the
effective `texScale`.)

## 3. Game objects are compiled at `-O0`

*Measured.* `tools/hgss_game.py:195-218` builds the flag list for all 1,092 game
C/C++ objects and contains **no `-O` flag**, so GCC's default applies. The app
objects add `-O2` explicitly (`:1391-1398`) and `3ds_port/common.mk:49-51` sets
`-O2 -g` for the vendored port libraries. So the split today is:

| Objects | Level |
| --- | --- |
| game (1,092 files) | **`-O0`** |
| app (`main_3ds.c`, `port_compose_pica.c`) | `-O2` |
| port libraries (`port_render2d.c`, `port_os.c`, `port_gx3d.c`) | `-O2` |

Compiling all 1,092 game objects at `-O2` was measured out of tree with
devkitARM GCC 16.1.0, `-j10`, exact section sums:

| Level | `.text` | vs O0 | Compile time |
| --- | ---: | ---: | ---: |
| **O0 (shipped)** | 8,146,868 | — | 104.5 s |
| **O2** | 4,512,808 | **−44.6%** | 204.5 s |
| O3 | 5,296,444 | −35.0% | 166.7 s |
| Os | 3,902,940 | −52.1% | 152.2 s |

The complete linked artifact was also built: **9,403,804 → 5,762,900 B**
(code 8,820,940 → 5,159,668), same link, no new errors. Artifacts exist at
`build/game-3ds-o2/pokeheartgold.3dsx` (5,762,844 B) and
`build/game-3ds-o2-ss/pokesoulsilver.3dsx` (5,762,900 B). **They are unvalidated
on hardware and were not shipped.**

The change is now in the tree (`port_cflags()`), and the native build measures
it end to end: the image is **29.84 MiB** instead of 33.26, so `remaining` grows
from 30.73 to **34.16 MiB**.

Two consequences, both large:

1. **~3.42 MiB of committed image is freed**, and with the explicit Old-3DS
   split (12 MB game) the linear heap becomes **22.16 MB** instead of 15.73 MB.
2. **Code shrinks 41%**, which matters on a CPU with a 32 KB L1 I-cache and no
   L2. At `-O0` no inlining happens, so thousands of tiny accessors remain
   calls in 8.15 MB of text.

`-O3`, `-Os`, LTO, PGO, `-mthumb` and `-ffast-math` were all evaluated and
rejected for now; see [§8](#8-not-doing-this-wave).

## 4. Where the time actually goes

*Measured* from the Old 3DS log:

| Metric | Value | Note |
| --- | --- | --- |
| display | 3.6 composes/s, **177.59 ms build** at frame 1200; 0.3-1.0 composes/s, 669-1,639 ms later | wall clock, includes preemption |
| drawer (both screens, core 0) | **249.71 ms/frame = 160.83 render + 88.87 publish**; later 586-1,310 render | wall clock, not CPU |
| geometry | 16.4-23.3 scenes/s, 14.6-26.5 ms/scene, 43-49% busy | own thread, core 1 |
| audio | 18.7-26.8% of core 0 | priority `0x2e`, above the game |
| texture conversion | **8,907 ms for 20 textures** | one event, frame 1204 |
| `state_us` | 13 µs (intro) → 9.7-14.1 ms (field) | see §5 — not a clean measurement |

The Old 3DS is **not** 3× slower than the New 3DS. The same build on the New
3DS ran the upper drawer at 12 ms, the lower at 3.8 ms and the display at 68-71
composes/s. The extra factor on the Old 3DS comes from:

- 268 MHz vs 804 MHz (3×) and **no L2** (the renderer's working set plus the two
  128 KB VRAM views exceed the 32 KB L1 D-cache);
- **both engines drawn serially on one core** instead of two drawers on two
  cores (~2×);
- the 408 KB texture cache
  ([§2.3](#23-the-3d-texture-cache-is-408-kb)).

The GPU is **not** the constraint: reported GPU time is 2-19 ms against a 33 ms
frame, and total per-frame GPU traffic is ~4-5 MB. Do not trade visuals for fill
rate on this console.

## 5. What is not settled

Three audits disagreed. Both disagreements are recorded here rather than
smoothed over, because both affect how the waves are judged.

### 5.1 Is the DS clock at 60 Hz or ~40 Hz?

*Measured*: the audio counters say the clock is **not** 60 Hz on average.

```
[1200] [perf] audio: 18.7% of core 0 (2833.9 ms mixing in 15.2 s)  → 600/15.2 = 39.5 Hz
[1800] [perf] audio: 26.8% of core 0 (3856.7 ms mixing in 14.4 s)  → 41.7 Hz
[2400] 46.5 Hz   [3000] 46.5 Hz   [3600] 39.5 Hz
```

*Measured*: but the sampled frame bodies are much faster than that average.

```
[1200] FRAMETIME frame=1200 game_us=4957  state_us=9718  total_us=14675
[1800] FRAMETIME frame=1800 game_us=0     state_us=14118 total_us=10810
[2400] FRAMETIME frame=2400 game_us=1467  state_us=12745 total_us=14212
[3000] FRAMETIME frame=3000 game_us=3817  state_us=10232 total_us=14049
[3600] FRAMETIME frame=3600 game_us=4017  state_us=10227 total_us=14244
```

So the *average* clock is 39-46 Hz while a sampled frame costs ~14.2 ms
(~70 Hz). The deficit is therefore **stalls**, not steady-state work: the game
does not average 25 ms because every frame costs 25 ms.

Frame 1800 is internally inconsistent (`total_us` 10,810 < `state_us` 14,118),
so **these counters cannot attribute cost**. The leading hypothesis, and it is
only a hypothesis, is that the game thread and the V-blank thread wait on
`sPresentLock`/`sSetLock`, which the display thread holds across its whole
compose (`port_compose_pica.c:3091-3267`, `:3551-3657`). At a 1.6 s compose that
would stall the clock once per compose. W0 exists to settle this.

### 5.2 How much does `wide=off` save?

The CPU audit estimated 35-45% of upper-screen render; the GPU audit estimated
~20% and judged it not worth the pillarboxing. **Unresolved.** The owner has
decided to keep widescreen on Old 3DS regardless, exposed as a launch option, so
this is now a measurement for W4, not a blocking decision.

### 5.3 What is `state_us`?

The threading audit attributed 9.7-14.1 ms to the H-blank walk
(`main_3ds.c:954`); the CPU audit split it between H-blank callbacks and
`TakeState`; the compiler audit called it lock/wait contention. All three are
plausible because `state_us` is **wall clock around `DrawFrame`**, which is
preemptible by the audio thread (`0x2e`, above the game) and can block on
compose locks. The H-blank walk is real work (`port_os.c:143-170`: 192 lines,
handler dispatch, `KeepLine` memcpy of 2 × 0x70 bytes per line), and on the New
3DS the same code costs ~80 µs — so most of the Old 3DS number is not the copy.

The call site is confirmed: `NitroPort_HBlankLinesOnce()` runs at
`main_3ds.c:954` **before** the drawer-free scan at `:961`, and the source
comment says this is deliberate because the compositor's weather BG offsets are
read from H-blank-updated registers. **It cannot simply be moved or skipped** —
the callbacks mutate game state. W3 compacts it; W0 measures it first.

## 6. The wave plan

Order is deliberate: each wave makes the next one measurable.

| Wave | Work | Files | Expected | Risk |
| --- | --- | --- | --- | --- |
| **W0** | Trustworthy instruments | `hgss/game/app/main_3ds.c` | Enables everything below | none |
| **W1** | `-O2` for the 1,092 game objects | `tools/hgss_game.py` | −3.49 MiB image, +3.5 MiB linear, 41% less code | low |
| **W2** | Old-3DS memory profile | `main_3ds.c`, `port_compose_pica.c`, `port_gx3d_pica.c` | 408 KB → ≥4 MB texture cache; stops the 8.9 s hitches | medium |
| **W3** | Game-thread frame cost | `port_os.c`, `port_render2d.c` | `state_us` down; restores the 60 Hz clock | medium |
| **W4** | 2D renderer hot paths | `port_render2d.c` | the long pole; est. 40-60 ms true CPU toward 15-25 | low, must stay pixel-exact |
| **W5** | Publish hot paths | `port_compose_pica.c` | ~89 ms → ~25-35, near zero on static screens | low |
| **W6** | Threading and pipelining | `main_3ds.c`, `port_compose_pica.c` | +20-50% upper compose cadence | medium |

### W0 — instruments

- `LOG_LINE` **220 → 512** (`main_3ds.c:67`). Every `[perf] display` line in the
  Old 3DS log is truncated mid-field (`handover 0.00 m`, `scene build 970.07 ms,
  GPU 0.00 ms, handover 0.`), so the existing data is incomplete.
- Buffer the log file instead of `fflush` per line (`main_3ds.c:190-215`); the
  current path does SD I/O in the frame path during hitches.
- Split the counters so they can be trusted: `state_us` into H-blank /
  TakeState / other; drawer render into engine A and engine B; publish into
  scan / convert / tiling / flush; and record `linearSpaceFree()` low-water,
  the game-heap high-water, and 3D texture cache hits / misses / evictions /
  `settle()` waits.

**Acceptance:** a new Old 3DS log whose `[perf] display` line is not truncated
and whose `total_us` equals `game_us + state_us + other`.

### W1 — compiler

Add `-O2` to `port_cflags()` so the game objects are optimized, for both
`3ds` and `host32` so the PC replay validates the release flags.

**Acceptance:** 1,092/1,092 still compile; `make test`, `make game-pc`,
`boot_smoke`, save/CRC and the replay checks pass; the `.3dsx` code segment is
~5.16 MB; on hardware the linear heap line reads ~18 MB (or ~24 MB once W2's
split lands).

**Watch for:** `-O2` enables `-fipa-icf`, which can merge functions and change
function-pointer identity; it also exposes uninitialized reads. If a regression
appears, rebuild with `-fno-ipa-icf` before considering anything drastic.

### W2 — Old-3DS memory profile

**Landed:**

- The half split is replaced by an explicit small-process split:
  `remaining <= HEAP_SMALL_TOTAL` now means "game 12 MB, display the rest",
  instead of the old `remaining > 16 MB * 2` test that a 30.73 MiB Old 3DS
  failed. With W1's image the linear heap is **22.16 MB** (was 15.73).
  12 MB, not 10, keeps headroom over the port's own caches (a 4 MB pak block
  cache plus 2.9 MB of wide-art slots plus a 256 KB read scratch and PNG decode
  transients); `[perf] mem:` now reports the game-heap usage so the next run can
  settle the number.
- The classic path's readback images and depth/IDs are allocated by
  `classic_images_ready()` on the first `NitroPort_PicaRender`, not in
  `NitroPort_PicaInit`: **1,152 KB** that the remaster can never use.
- The sun-shadow vertex room is opt-in: `NitroPort_PicaSetShadowRoom(on)`, which
  the shell calls with `sCompose.shadows` (**0**) before `GpuStart`:
  **1,438 KB**. The default stays 1 so the vendored behaviour is unchanged for
  any caller that does not ask.

**Still open (ranked):**

- 1 MB command lists → 512 KB (`port_gx3d_pica.c:128-137`, `:546`): **1,536 KB**.
- Skip the lower-menu and map textures on Old 3DS (`port_compose_pica.c:1025-1041`):
  **768 KB**.
- Allocate the upper screen's right-eye LCD buffers lazily on the first
  non-zero slider (`:899-924`): **576 KB**.
- **Enforce a texture-cache floor** on the last compose attempt instead of
  accepting whatever is left. If the floor is not met, drop the weather BG and
  the menu before the texture cache: the current order is inverted.
- Make `filter=nearest` a launch value that does **not** change allocation, and
  make the effective `texScale` explicit in the config rather than inferred from
  the filter and the memory ladder.
- Log the effective config once (texScale, LCD buffers, menu, BG, cache bytes,
  free linear). The current banner reports the requested filter, which is why
  the log claims "2D sharp" while publishing 1×.

**Acceptance:** `linearSpaceFree()` after compose init ≥4 MB (target 6-8 MB with
W1); `textures converted` is 0 after the first frame of a scene;
`settle()` waits are ~0.

### W3 — game-thread frame cost

Compact the H-blank capture in `port_os.c` to the registers the renderer
actually reads, instead of `KeepLine` copying 0x70 bytes per line per engine,
and drop the per-render full 8 KB `ioAll` copy in `port_render2d.c:3953-3956`.
Separately, address the compose lock hold times (W6) because that is the other
candidate for the clock deficit.

**Do not** change the number of H-blank callbacks: effects depend on VCOUNT, and
the weather BG reads their result.

**Acceptance:** W0's split shows H-blank under ~1.5 ms/frame; the average clock
from the audio counter reaches ~60 Hz; field effects (wave phases, window
wipes, fades, battle-start line slides, weather BGs) are unchanged.

### W4 — 2D renderer

Lower screen at **30 Hz only** (owner's decision); game logic stays 60 Hz.
`paired_2d_ready` already supports a game that offers 2D every other V-blank
(`port_compose_pica.c:594-624`), so this is a handoff policy, not a redesign.

Then, pixel-exact only:

- `MergeT` fast path for opaque rows (`port_render2d.c:1996-2001`), writing two
  pixels at once when both are opaque;
- skip the second `memset(row)` when `t.filled == 0`, and the `SnapshotOver`
  copy/alpha fill when nothing is in front (`:3740-3765`, `:4045-4054`);
- clear only the OBJ bounding span on a line, not the full width (`:3002-3016`);
- cache `EngineWindows` once per render instead of a spin-locked
  `NitroPort_WideWindows` call per BG per line (`:1307`, `:4407-4425`).

Widescreen **stays on** for Old 3DS, as a launch option. Measure `wide=0` for
the record (this closes §5.2) but do not make it the default.

**Acceptance:** drawer upper render time down with pixel-identical output on a
field frame, a text box, a battle intro, a fade and the lower menu.

### W5 — publish

At 1× the compositor never uses the band-diff publisher — `publish_whole()` runs
whenever `texScale == 1` (`port_compose_pica.c:1367-1378`), so a one-pixel change
re-tiles and re-uploads the whole screen, including on brightness-only fades.

- Port the existing 4-row band diff (`NitroPort_PublishDoubledOver`) to the 1×
  path.
- Replace the 128 KB `sToRgb565` lookup with arithmetic.
- Add a brightness-only publish that skips conversion and flush.
- Skip the over-layer conversion when `overBottom == 0`, and allocate the over
  texture as RGBA5551 when nothing is translucent.

**Acceptance:** publish below ~35 ms contended; near zero on a static screen;
colours and alpha bit-exact against the current build.

### W6 — threading

- Split the drawer into A and B and wake the display when **A** finishes; today
  `sDrawFramePending` reaches zero only after B (`main_3ds.c:832-872`).
- Demote the display thread below the drawer (`0x31` → `0x32`) so publishes land
  first.
- Narrow `sSetLock`/`sPresentLock` to buffer reservation and queueing
  (`port_compose_pica.c:3091-3267`, `:3551-3657`); this is also the likely
  fix for the clock deficit in §5.1.
- Move the lower-screen drawer off core 0.
- Geometry stays on core 1. **Do not** move engine B or the whole drawer there:
  Platinum measured engine B alone at 14-25 fps on a 30% core-1 share, unevenly,
  while taking time from geometry.
- Log the **accepted** APT core-1 share (`APT_GetAppCpuTimeLimit` after the
  attempt). The plan currently assumes 30%; nothing in the log proves it.

**Acceptance:** composes/s up 20-50% at the same quality; HOME/lid cycles still
settle (the `sComposing` handshake must be re-checked after any lock change).

## 7. Profiles

Old 3DS and New 3DS get **separate fixed launch profiles**. Runtime X+R filter
cycling is removed on Old 3DS (three of the five presets are identical at 1×,
and the `filter3d` presets multiply texture memory 2-4× with under a megabyte
free). New 3DS keeps the presets and the smoother filters.

| Setting | Old 3DS | New 3DS |
| --- | --- | --- |
| 2D filter | launch-time; 1× texels, no runtime cycle | unchanged, presets kept |
| `texScale` | explicit `1` | `2` (sharp) |
| upper canvas | **wide 320, launch option, default on** | wide |
| lower screen 2D | **30 Hz** (logic stays 60 Hz) | 60 Hz |
| upper screen 2D | 60 Hz | 60 Hz |
| `smooth3d` | `0`, and stop reserving its 2.3 MB of VRAM | `2` |
| stereo | keep, but allocate the right eye lazily | keep |
| edges / filter3d | off / nearest | unchanged |
| lower menu, map textures | off | unchanged |
| game heap | 12 MB (validate against the `[perf] mem:` high-water) | unchanged |

## 8. Not doing this wave

- **`-O3`** for game or renderer: +17% game code, +49% renderer object (87.5 →
  130.5 KB), against a 32 KB L1 I-cache, with no measured speed gain.
- **`-Os` globally**: saves only 0.61 MB over `-O2` and slows hot code. One
  renderer-only experiment is worth a hardware A/B later.
- **LTO**: the manual link needs the `objcopy --weaken-symbols`, duplicate and
  nnm passes reworked, and the hot loop is single-file; defer.
- **`-mthumb`**: ARMv6K has Thumb-1 only, and GCC refuses Thumb-1 with the
  hard-float VFP ABI (`sorry, unimplemented: Thumb-1 'hard-float' VFP ABI`).
- **`-ffast-math`**: reassociation and NaN/Inf risks for no hot libm.
- **Dropping `GF_ASSERT`**: `-DNDEBUG` is a no-op (the switch is
  `PM_KEEP_ASSERTS`), and some assert operands have side effects.
- **`fps=30` as a performance mode**: it halves the game clock. It is a
  diagnostic only.
- **2× SHARP on Old 3DS**: arithmetically impossible (§2.3).
- **Raising the drawer above the game on core 0**: the DS clock is per frame, so
  the game slows proportionally.

## 9. Expected trajectory

*Estimated.* After W0-W3 the game clock should return to ~60 Hz with 2D/3D
presentation in the 8-15 fps range. After W4-W5, 20-30 fps presentation. W6 adds
20-50% cadence on top. A locked 60 fps is not reachable with this software 2D
renderer and requires GPU-side 2D (Phase 11).

The ceiling arithmetic for 30 fps presentation: with ~10.5 ms of game + audio +
input per 60 Hz frame, core 0 leaves ~6.2 ms/frame (372 ms/s) and a 30% core 1
adds ~300 ms/s, so ~670 ms/s of drawing budget (less ~150 ms/s if geometry stays
on core 1). That means the drawer + compose must fall to roughly **22 ms per
composed frame** with core 1 available, or ~17 ms with geometry there. The
current drawer is 160-1,310 ms **wall** per frame; its true CPU cost is
estimated at 45-60 ms render + 25-35 ms publish. The gap between 45-60 and 22 is
what W4/W5 have to close.

## 10. Open items handed to the next session

1. Implement W0, rebuild both games, and get one Old 3DS log with the new
   counters before touching anything else.
2. Decide the game-heap number from that log's high-water.
3. Resolve §5.1 (clock deficit) — W0's split either confirms or kills the
   lock-contention hypothesis.
4. Resolve §5.2 (`wide=off`) with the W4 measurement.
5. The earlier out-of-tree `-O2` artifacts at `build/game-3ds-o2/` and
   `build/game-3ds-o2-ss/` are **superseded**: the flag is in `port_cflags()`
   now and the same builds are produced by the normal command. Take one Old 3DS
   log with the W0 instruments before starting W3.
