# S21 — Goldenrod benchmark: kill the 2D renderer for good

Status: **plan only. No code changed.**

Benchmark is fixed: **outdoors Goldenrod City, busy, lots of 3D.**
The DS cuts through it like butter because its 2D is fixed-function scanout:
tilemaps + sprites + windows + blend read straight out of VRAM at 67 MHz with
zero per-pixel CPU. We re-decode every tile/texel/line on an ARM11 that also
runs the game, audio, display and (on o3DS) both engines serially — then do it
again at 320 wide (+25%), at 2x on N3DS (+300%), and re-publish whole screens
the display drops half of. A 3DS *should* cakewalk this. This plan makes it.

## 0. Ground truth (tree `2cddfe5` + ~46 dirty files, logs to `log-o3ds-10`, `log-n3ds-0/2`)

Goldenrod outdoors per logs/session notes:

* ~10,700 verts/scene, ~2,784 clipped; worst geometry 47–51 ms/scene 92–96% busy,
  display build 490–768 ms when display shares core 1 with geometry (o3DS log-6:2400).
* o3DS field drawer 55–124 ms = render 39–87 + publish 16–34 (`log-10:1200-3000`).
  `[perf] 2d`: text 21–25%, finish 9–15%, obj 2–5%, merge/blend ~1–3%, affine/window ~0%.
  ~1/3 unattributed = per-frame setup (keys, art lookups, palettes, splits).
  `emptyLines` 0–8% — backdrop skip is marginal.
* Slice B row cache exists in tree (`TextRowCached :2568`, `rowHits/Misses`,
  `setupTicks`, `sTextKey`, perf `rows hit/miss :1481-1486`). Hit rate unreported —
  first hardware run decides band-cache design.
* N3DS same area lighter but still wrong: drawer upper 5.7 ms + lower 1.8–2.8 ms
  + game 8–12 ms + display 4–7 ms + audio 0.85 ms + H-blank 3.0 ms on core 0
  = 18–26 ms > 16.6 ms → 30–32 composes/s, never sleeps, drains battery.
  Core 1 idle on N3DS. Display publishes 58/s, composes 30/s — half the drawer
  work is dropped. `smooth3d=2` costs ~6 ms GPU/frame (`2x2 … opaque 5.0 + translucent 1.0`).
* H-blank walk fixed by deferral (`port_os.c:156-174`, `port_thread.c:290-299`):
  o3DS `hb_irq` 12.7 ms → 0.4 ms. N3DS heavy-area `hb_irq` still 2.45 ms
  (`log-n3ds-0:9600`) = game handlers, not the switch. Not the Goldenrod wall,
  do not chase here.
* Memory done: linear 15.73 → 22.16 MB, texture cache 408 KB → 7.6–8.1 MB,
  conversions single-digit ms. Image 33.26 → 29.77 MiB (`-O2`), artifact 9.40 → 5.78 MB.
* Reverted/failed, do not retry blind: opaque batch merging + stencil drop
  (duplicated NPCs, halos — `s20-performance-session.md:137-160`), audio→core 1
  on o3DS (core 1 61→84–100%, display →0.0/s, log-9), `-O3`/`-Os`/LTO/thumb/fast-math,
  `fps=30` (halves game clock), batch merging across texture change.

## 1. Benchmark freeze (lead, Wave 0, no delegation)

1. One save + one scripted path in Goldenrod outdoors (same stairs/route/grass,
   same time-of-day, same party). Record: entry frame, overlay set, `field` load line.
2. Metrics per 600-frame interval (already in tree): `FRAMETIME`, `[perf] display/drawer/geometry/audio/2d/mem`,
   `[gx3d] 120 scenes` verts/clipped, `[gpu] clears`, `3D images`, hitch lines.
   Add before any slice: setup-tick split already exists — quote `setup %` + `rows hit/miss`
   from first run; that chooses Wave 1 cache granularity.
3. Pixel bar: PC replay of same path must stay bit-exact (2D exact, 3D tol 8/255)
   via `tools/golden/run.py` on synthetic + real pack. No slice lands without it.
4. Artifacts: HG+SS `.3dsx` sha256 + `validate_3dsx []` per build. One staged pair per wave,
   supersede explicitly (see §5 trap: `build/game-3ds-o2` confusion).

## 2. Waves

Order is deliberate: measure → stop CPU bleed → move 2D to GPU → fix Goldenrod 3D +
threading → integrate. Waves overlap only on disjoint files (table below).

### Wave 0 — lead only: baseline + instruments + freeze

Objective: trustworthy Goldenrod numbers on both consoles before any optimization.
Allowed: `hgss/game/app/main_3ds.c` perf lines only, `tools/check_patches.py`,
`docs/progress/**`, `port.ini` benchmark profile. No renderer/compositor/geometry edits.
Deliverables: frozen save+script+port.ini, one o3DS + one N3DS log with untruncated
`[perf] display/2d` lines, quoted `setup %`, `rows hit/miss`, `hb_*`, texture/settle counts,
`linear_free` high-water, artifact sha256s. Stop if `total_us < state_us` or any
`[perf] display` line truncates (LOG_LINE regression) — fix instruments first.
Avoid: changing draw/compose/geometry/audio behavior; committing dirty W-tree as-is
(commit per slice after green, §5).

### Wave 1 — CPU kill: make software 2D pay only for what changed (parallel)

All pixel-exact (bit-identical PC replay). Each slice owns one file set, no overlaps.

* **S1-text (owns `port_render2d.c` text path only: `DrawTextBgT :1623`, `TextBgSegments`, `TextRowCached :2568-2600`, `sTextKey`):**
  Complete row cache: key includes map base + palette + mosaic + affine params +
  wide-split origin; invalidate on VRAM/palette/scroll writes (hook writes, don't
  re-hash to check). Report `rows hit/miss` → target >80% hits scrolling, ~100% static.
  Deliver: hit-rate log + halved text % on Goldenrod. Avoid: changing key granularity
  mid-slice; caching across palette writes; touching merge/blend/finish/OBJ code.
* **S2-band (owns `port_render2d.c` band/setup only: `DrawEngineT :4022`, `SetUpEngine`, `TakeState :5459`, signatures `EngineSmall/VramSignature :5486-5659`):**
  Per-BG 4-row band cache keyed by small-sig + VRAM gen (write-counted, not hashed).
  Cache setup outputs (keys, art lookups, palettes, splits, `WideWindows`) per frame,
  not per line (`EngineWindowsRef` pattern). Replace static-frame 864 KB VRAM hash
  with write-dirty generations (increment on VRAM/pltt/OAM DMA/write paths; hash only
  to certify a skip, never as the skip test itself). Deliver: `setup %` down by half,
  static menus skip draw with ~0 hash bytes. Avoid: skipping VRAM display-mode banks;
  masking `DISPCNT` bits the comment doesn't justify; changing draw order.
* **S3-merge (owns `port_render2d.c` merge/finish/OBJ only: `MergeT :2087`, `BlendLine* :3802-3977`, `SnapshotOver :3979`, `DrawObjs`, backdrop/brighten):**
  Extend opaque-pair merge to `T_LAYER` upper rows (prove flag-independence first);
  SnapshotOver copy/alpha/second-zero skip when front empty; OBJ span clear already
  done — add art-count fast path; finish (backdrop/split fixups) vectorize, no logic change.
  Deliver: merge+blend+finish % down, Goldenrod drawer -10–20%. Avoid: touching text/band/setup;
  reassociating blend math; `-ffast-math`.
* **S4-hblank-minor (owns `port_os.c`, `port_thread.c`, `port_timer.c`, `port_dma.c` only):**
  No walk redesign (done). Only: per-callback timing split (which H-blank client owns
  N3DS 2.45 ms), `OS_GetTick` call-count + `SyncTimers` divide cost counter,
  `SoundPumpReplies` ticks split from `game_us`. Deliver: attribution, no behavior change.
  Avoid: changing callback count/order/VCOUNT semantics; moving walk after drawer scan
  (weather BG reads H-blank-updated regs — `main_3ds.c:1078-1088` comment).

Wave 1 exit: Goldenrod drawer down ~30–40% with identical pixels + reported
`setup %`, `rows hit/miss`, `hb` split. If text still >15% of core, Wave 2 starts
regardless — CPU caching has a ceiling on scrolling tilemaps.

### Wave 2 — GPU 2D: the real kill (parallel, the only structural fix)

DS 2D maps to PICA almost 1:1: each BG = 1–2 tiled textures + quad; OBJ = instanced
quads; windows = stencil/scissor; blend/brightness/mosaic = TEV + LUT/uniform.
Software `DrawEngineT` becomes the reference model + fallback, not the frame path.

* **S5-gpu-bg (owns new `port_render2d_gpu.c` + `shaders/2d*.v.pica`, `port_pica.h` additions only):**
  Text + affine BGs as paletted textures (exact DS formats incl. 4×4 comp, same CPU
  decode + texture cache as 3D path), one draw per BG per band-range, scroll via
  UV offset (no re-decode on scroll — this is the win Wave 1 cannot get).
  Mosaic via UV quantize; affine via quad matrix. Deliver: text BG bit-exact vs
  software on field/textbox/battle-intro/fade/lower-menu goldens; text % → ~0 CPU
  when GPU path takes the BG. Avoid: changing decode (reuse `NitroPort_Gx3dDecodeTextureTiled`);
  inventing new texture formats; touching OBJ/blend/window code.
* **S6-gpu-obj (owns new OBJ path in same files, disjoint hunks from S5 by BG-vs-OBJ split — lead assigns hunks, or serialize S6 after S5):**
  OBJ sort + affine double-size + 4/8-bit palettes as instanced quads; span-clear
  and per-line OBJ walk disappear. Deliver: obj % → ~0 + affine/mosaic goldens.
  Avoid: reordering priority (DS priority is exact); merging across palette change.
* **S7-gpu-compose (owns `port_compose_pica.c` 2D-layer side only: layer-set alloc/sampling,
  `publish_bands/whole :1337-1458`, `publish_unchanged :1540-1606`, `rgb565`, `overBottom/hasOver`):**
  Publish becomes texture-upload (dirty bands only) + GPU blend/brightness; fade =
  uniform change with zero convert/tile/flush (`publish_unchanged` pattern extended to
  GPU sets). Over as RGBA5551 when nothing translucent (today hard RGBA8 at 1×
  `:1213,:1401` — fix + measure). Deliver: publish <10 ms contended o3DS / <1 ms N3DS,
  fade with no conversion. Avoid: touching 3D/PICA init/cmd lists; changing `COMPOSE_TEXTURE_ROOM`
  floor in same slice (that's Wave 3 memory).
* Fallback rule (all three): any mode not yet proven (bitmap modes, VRAM display,
  capture-sourced BG, exotic blend) falls back to software path per band, logged once.
  No silent pixel change. Tests: `test_render.c` patterns + field goldens per BG kind;
  `NitroPort_Render2DProfile` keeps counting (GPU path reports 0 CPU + upload bytes).

Wave 2 exit: Goldenrod 2D CPU ~0 (upload + uniforms only); drawer = publish/upload bound
(<15 ms o3DS, <3 ms N3DS). If a BG kind can't be proven exact, it stays on CPU —
exactness outranks coverage.

### Wave 3 — Goldenrod 3D + threading + memory (parallel)

* **S8-geom (owns `port_gx3d.c` clip/cull + `port_gx3d_prune/raster/vertex` + `port_gx3d_pica.c` batch/state only, disjoint from S7 by file):**
  Reduce 2,784 clipped/scene: frustum + backface + zero-area earlier (camera-plane drop
  pattern `stage_triangle`), far-plane tighten where game fog covers it, batch by state
  without merging across texture/stencil (revert proved LEQUAL-only merging duplicates
  NPCs — keep `set_combiner_cached`/`bind_batch_cached`, never merge draw ranges).
  Mid-scene match already float — profile, don't rewrite. Deliver: clipped/scene down,
  geometry ms/scene down at same pixels, `lost: clipped` + `culled` quoted.
  Avoid: batch merging across texture/stencil; stencil-enable removal; double reintro.
* **S9-core (owns `hgss/game/app/main_3ds.c` threading/config only):**
  N3DS: use core 1 (idle today — §0). Move display or drawer-B or audio off core 0
  (o3DS audio→core 1 failed under 80% cap, log-9 — N3DS has no cap, do not copy the
  conclusion). Add drawer→display backpressure (don't render what will be dropped:
  publishes 58/s vs composes 30/s today). Keep `sComposing` Dekker handshake +
  `ONRESTORE`-after-`aptMainLoop` + `sGameThreadAlive`-before-create rules.
  Lower-30 already on o3DS; A/B lower-30 + `smooth3d 2→0` + `texScale 2→1` + `wide off`
  on N3DS, one variable per run. Deliver: core-0 frame <16.6 ms, composes 30→60,
  cap sleeps, battery down. Avoid: raising drawer above game on core 0; `fps=30`
  (halves clock); moving engine B + geometry together onto core 1 (Platinum: 14–25 fps).
* **S10-mem-publish (owns `port_gx3d_pica.c` cmd/alloc + `port_compose_pica.c` init ladder
  + `port_sound_chip/snd` counters only):**
  Cmd lists 1 MB→512 KB (save 1.5 MB), right-eye lazy, menu/map off on o3DS,
  enforced texture floor (fail loud instead of accepting 408 KB), `filter=nearest`
  no-alloc. Audio: move off game core only with S9; mixer bit-exact skips stay
  (silent/hard-pan/full-volume), no rate/block redesign. Deliver: `linear_free ≥4 MB`
  post-init, conversions 0 after first frame, `settle` ~0, audio drops 0.
  Avoid: touching draw/merge/publish math; LTO/thumb/fast-math (`s19 §8` rejected).

### Wave 4 — lead: integrate, harden, verify

Lead merges slices in order W1→W2→W3 (each: `bootstrap`, `make test`, `port-tests`,
host32+3DS 1092/1092 0-unresolved, `--check-overlays` 129/0, `symbol_crosscheck` 0,
`validate_3dsx []`, PC Goldenrod replay pixel gate). Full HG+SS DS gate at
`/work/pokeheartgold` after any patch-stack touch (never `/work/patched`, needs
`mwasmarm_patcher` first — shape pins go stale on any `#ifdef` line shift).
One staged HG+SS pair per wave, supersede named (`s20` confusion: `o2/`, `7d39/9b0b`
pairs). Hardware: Goldenrod scripted run both consoles + frame table + save round-trip;
anything user-hardware-gated stays **pending with procedure**, never marked done.
Upstream PR drafts under `docs/outreach/` (generic port fixes only), never opened
without owner.

## 3. Pitfalls (from logs/commits — read before touching the files)

* Batch merging across texture/stencil duplicates NPCs + halos (reverted; keep state
  caching + texture hoisting only — `s20:137-160`, artefacts logs/photos 11-03).
* `threadFree` after timed-out join kills live thread (`sGameThreadAlive` set by creator
  before create, free only on successful join — `STATE.md:425-449`).
* 5 missing patches abort (`WirelessStatusIcon` `0x04000000` — `check_patches.py` 86/86
  in build, `STATE.md:397-409`). DS gate at 78, tree at 86 — rerun gate after any patch.
* Stale `libnitro.a` after `port_os.c` edit: delete `build/<out>/sdk` (incremental builds
  silently keep last week's code — `s20:386-390`).
* OAM attr0 bit9 = disable only when affine clear; HGSS field sprites affine —
  diagnostic lied (`s20:213-222`). Suspect diagnostic before system.
* `GxRefresh` hoist changed nothing; cost was `IrqThreadSwitch`/frame (deferred —
  `port_os.c:156-174`). Profile inside interrupt before rewriting walk.
* Audio→core 1 tested + reverted on o3DS (geometry 61→84–100%, display →0.0/s).
  Do not re-try on o3DS; N3DS core 1 is a different (free) core.
* `sToRgb565` is 64 KiB not 128; `publish_whole` vs `PublishDoubledOver` dispatch moved
  (`:1658-1674`); `LOG_LINE`/`LOG_RING`/`fflush` line numbers shifted — don't quote stale lines.
* Locks held for pick/queue only; long hold is `sComposing` (`:3391-3567`/`:3851-3957`).
  Game thread takes no compose lock — don't "fix" a wait that doesn't exist.
* `total_us` vs `state_us` + truncated `[perf] display` (LOG_LINE 220) misattributed a whole
  round (`decisions.md:137`, `s19 §5`). Instruments first, then judge.
* `3dsxtool` packs `symbol-0x06800000` folds as pointers (patch 1903 `noinline` + `validate_3dsx`).
  Weak hook binds NULL (`d996e22`); blit both screens every frame (`e16f0ec`);
  console only in `PakErrorScreen`; exception handler lock-free; APT single owner;
  `threadJoin` success ≠ finished; literal DS addresses gated by `test_hw_addresses.py`.
* `fps=30` halves game clock (diagnostic only); lower-30 is publish-only, logic stays 60 Hz.
* `wide=off` is layout choice, not accepted perf knob — measure, don't default.
* Goldenrod `[hitch] 100–370 ms` + 167 ms `clears` unexplained (`s20:237-238`) — Wave 3 target,
  not Wave 1. Don't bury it in 2D averages.
* Tree is dirty (~46 files) on `2cddfe5`; no manifest ties any `hgss-private/log-*` to a
  source state. Wave 0 freezes benchmark + records sha256s first, or every later
  comparison is void.

## 4. Acceptance

Per slice: PC Goldenrod replay bit-exact + `make test`/`port-tests` green + 1092/1092
both targets + `validate_3dsx []` + quoted perf delta on same save/script.
Per wave (lead): full matrix + HG+SS gate SHA1s (`4fcded0e…`, `f8dc38ea…`) if patches touched
+ staged pair + STATE/decisions/wiki/Patch-Stack updated + committed per slice.
Done = Goldenrod outdoors playable on o3DS (target 20 fps presentation, game clock 60 Hz,
no 100 ms+ hitches) + N3DS 60 composes with headroom (core-0 frame <14 ms, cap sleeps,
audio 0 drops) + save round-trip both ways. Anything needing console/captures stays
pending with exact procedure.

Final report: commands + tails, what changed, what remains + why, decisions needed.
Stop and ask on: proprietary/ROM/save/golden/`dspfirm` gaps, weakening a test, save-layout
change, DS-behavior change outside `SDK_PORT_3DS`, same failure twice, new tool dep,
any upstream push.
