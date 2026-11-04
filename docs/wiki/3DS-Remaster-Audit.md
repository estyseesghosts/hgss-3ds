# 3DS remaster audit: what to copy from the Platinum port

Status: **Phase A implemented; hardware validation pending.** This page compares
`pokeplatinum-3ds` with the HeartGold/SoulSilver port and describes how HGSS
gets the same kind of native-resolution, widescreen, higher-quality 3DS
display. Phase A (the shell/compositor wiring) is now in the tree and recorded
under [Phase A: implemented](#phase-a-implemented); Phases B-E are still plans.

Related: [3DS-Pitfalls](3DS-Pitfalls.md), [Architecture](Architecture.md),
[Save-Compatibility](Save-Compatibility.md).

## The short answer

The **display machinery is already vendored here**. The HGSS tree contains the
same `3ds_port` files as Platinum at `platinum.lock`: `port_upscale.c`,
`port_wide.c`, `port_render2d.c`, `port_compose_pica.c`, `port_wideart.c`,
`port_lowermenu.c`, `port_shadow.c`, `port_xbrz.c` and the GX3D/PICA backend.
Before Phase A the HGSS shell did not use them; it now compiles the compositor
into the program and presents every frame through it.

The work is therefore three layers, in this order:

1. **Wire the Platinum remaster display into `hgss/game/app/main_3ds.c`.** Use
   the GPU compositor, 1.25x 2D, a 320x192 upper canvas (400x240 native), the
   lower screen at 320x240, higher-resolution 3D (optional stereo), and the
   drawer/GPU threads. This gives the largest visible improvement without
   touching HGSS UI code.
2. **Tell the port about HGSS's windows and screens.** HGSS needs equivalents
   of Platinum patches 0012/0015 (`AddWindow`/`RemoveWindow` and the frame
   drawing functions), HGSS overlay classification for "pictures" and
   "battle", and `NitroPort_WideScreenExtra` in the battle/UI code.
3. **Redesign selected HGSS screens for the wider screen.** Text boxes and
   edge menus are generic; the party, summary, bag, PC boxes, Pokédex,
   trainer card, Pokégear and battle HUD need HGSS-specific layouts or wide
   art. This is the long, art-heavy part.

A fourth, independent item was the **lower-screen touch mapping**, which was
wrong even in the old 1:1 display; Phase A fixes it (see below).

## What Platinum actually does

Platinum has two display paths:

- **Classic:** each 256x192 screen is drawn by the CPU renderer with the 3D
  composited in, then copied 1:1 into the middle of the 3DS screen. This is
  what HGSS does today.
- **Remaster** (default on New 3DS): the GPU composes each screen from
  upscaled 2D layers and 3D drawn directly at screen resolution. The upper
  screen is a 320-DS-pixel-wide canvas, scaled 1.25x to 400x240. The lower
  screen is 256x192 scaled 1.25x to 320x240. In the field the lower screen can
  become the port's own menu, with the DS's lower screen in a panel.

Key Platinum features, and what they mean for HGSS:

| Platinum feature | What it does | HGSS applicability |
| --- | --- | --- |
| 1.25x 2D canvas | 256x192 -> 320x240; upper canvas 320x192 -> 400x240 | Directly usable; needs the HGSS shell and window hooks |
| Pixel-perfect field | 400x240 1:1 DS pixels, more world; 3D view widened | Excellent for the HGSS field; needs a camera/visibility check |
| GPU compositor | 2D behind/in front of 3D, GPU texturing, no CPU composite | Reuse `port_compose_pica.c`; needs HGSS shell wiring |
| Smooth 3D | Draw at 2x (512x384), 2x2 average into 400x240 | Reuse; good for HGSS field, intro, battle backgrounds |
| Stereo 3D | 3D slider, per-scene convergence | Reuse; tune/disable for 4:3 battle Puppet/Poképic scenes |
| Wide camera | Upper 3D view widened from 256 to 320 or 400 | Reuse; HGSS field is the main beneficiary |
| Wide art | Hand-drawn side bands for 2D screens | Reuse; HGSS needs captures and new art |
| Wide screen parts | Game lays out a screen in moving/filling parts | Reuse; needs HGSS-specific per-screen layouts |
| Lower menu | Port draws a 320x240 menu with buttons and a DS panel | Framework reusable; HGSS needs its own buttons/actions |
| Touch mapping | 3DS touch -> DS touch or lower-menu panel | HGSS shell currently misses even the 1:1 offset |

## The scaling numbers

The DS's screens are 256x192 (4:3). The 3DS's are 400x240 (top, 5:3) and
320x240 (bottom, 4:3).

| Target | 256x192 at 1.25x | Native fill | Notes |
| --- | --- | --- | --- |
| Top, 320x192 canvas | 400x240 | exact | 320 DS pixels wide: the DS's 256 plus 32 each side |
| Top, 256x192 centered | 320x240 | 80 px side bands | UI stays DS-wide; bands need rules/art |
| Top, pixel-perfect field | 400x240 = 400x240 DS pixels | exact | More world, not bigger pixels; 1:1 |
| Bottom | 320x240 | exact | 1.25x both axes; no letterbox |
| 2x DS | 512x384 | does not fit | Useful only as an intermediate render for smooth 3D |

There is no integer 2x fit. The practical rules are:

- **Render the field at 1:1 (400x240)** where the game can show more world.
  This is the only genuinely integer-scaled, pixel-perfect option and is the
  best look for HGSS's pixel-art overworld.
- **Render menus/2D at 1.25x on a 320x192 canvas.** 400x240 is exact. Use a
  **sharp** filter: each DS pixel becomes a 2x2 texel block and the GPU's
  bilinear filter does the 1.25x rest. Pixels stay the same size; only their
  borders are soft. `nearest` at 1.25 makes some pixels 1 screen pixel and
  some 2, which is visibly uneven. Scale2x/xBRZ smooth diagonals but are not
  integer either.
- **Render 3D at 2x and average down** (Platinum's `smooth3d`). The result is
  antialiased at 400x240 and avoids the jagged DS edge. This is a render
  quality choice, not an integer layout choice.
- **The bottom screen is exactly 1.25x.** It cannot be integer-scaled without
  letterboxing. Either accept 1.25 sharp, or keep a 1x centered panel and use
  the surrounding space for a port-drawn menu.

### Display modes

Phase A has **one** display mode. There is no classic 1:1 fallback on either
console: if the GPU display cannot start, the program shows
`DisplayErrorScreen()` and exits the game area instead of running at 1x.

| Mode | Top screen | Bottom screen | When |
| --- | --- | --- | --- |
| Remaster (the only display) | 320x192 canvas at 1.25x -> 400x240, wide 3D | 256x192 at 1.25x -> 320x240 | New 3DS and Old 3DS |
| Pixel field (option, Phase C) | Field at 400x240 1:1; menus at 1.25x | 1.25x | New 3DS, field only |

The same layout is used on an Old 3DS; only `smooth3d` differs (`2` on a New
3DS, `0` on an Old 3DS). The 3D is still drawn at the screen's own resolution,
which is what "native" means here; the 2D upscale is identical.

"Pixel field" is the field-only option because a 400x240 1:1 UI would show
more map/wallpaper than the game's layouts expect. Menus, battles and
cutscenes stay on the 1.25x remaster canvas.

## What the HGSS shell had before Phase A

`hgss/game/app/main_3ds.c` was a cut-down Platinum shell:

- It drew both screens with `NitroPort_Render2D` into 256x192 buffers and
  blitted them 1:1, centered.
- It ran Platinum's **classic** PICA path: a geometry thread, a GPU worker,
  `NitroPort_PicaRender` into an off-screen target, and
  `NitroPort_Gx3dConvertPending` into the 2D image API.
- It did not link `port_compose_pica.c`; `pica_classic_stubs.c` provided the
  two compose-only profiler hooks.
- It had no custom `__system_allocateHeaps`, no display drawers, no wide
  layout, no lower menu, no wide art loading and no stereo/smooth 3D options.

The display files listed above are the Platinum versions. The renderer,
upscaler and wide-layout code is compiled into `libnitro.a`; the compositor
(`port_compose_pica.c`) and PICA ABI are app objects under
`3ds_port/game/app`. The port layer has the one recorded camera-plane
divergence in `port_gx3d.c`; `gx3d_passthrough.v.pica` and the shadow shaders
are already in the tree.

## Phase A: implemented

The HGSS shell now uses the remaster display unconditionally.

**What changed (code):**

- `hgss/game/app/main_3ds.c`:
  - includes `port_pica.h` and calls `NitroPort_ComposeInit`;
  - has the three display threads: the display/GPU thread
    (`GpuComposeLoop`), the 2D drawer threads (`DrawerMain`), and the V-blank
    thread (`VBlankMain`);
  - hands each frame's display state to a free drawer with
    `NitroPort_Render2DTakeState`, renders the split layers with
    `NitroPort_Render2DSplit[IfChanged]`, publishes them with
    `NitroPort_ComposePublish`, and composes with
    `NitroPort_ComposeFrame`;
  - keeps H-blank line effects (`NitroPort_HBlankLinesOnce`) and gives the
    compositor the field's repeating weather BGs;
  - maps physical touch `px * 4/5`, `py * 4/5` (320x240 -> 256x192);
  - calls `NitroPort_Gx3dSetWideView(320)` while engine A is on the upper
    screen and `NitroPort_SetWideScreen(32)` at start;
  - adds Platinum's `__system_allocateHeaps` so the compositor's render
    targets and textures have linear memory;
  - leaves out the old `Blit`/`gfxSwapBuffers` path and the classic
    `NitroPort_PicaRender` worker.
- `tools/hgss_game.py`: compiles `port_compose_pica.c` into the program,
  drops `pica_classic_stubs.c`, and adds `3ds_port/game/app` to the include
  path.
- `hgss/game/app/pica_classic_stubs.c` is no longer part of the build.

**Settings (Phase A):**

| Setting | New 3DS | Old 3DS |
| --- | --- | --- |
| `wide` | 1 (320x192 upper canvas) | 1 |
| `filter` | `COMPOSE_FILTER_SHARP` | `COMPOSE_FILTER_SHARP` |
| `smooth3d` | 2 (2x render, averaged) | 0 (native render) |
| `stereo` | 1 (3D slider) | 1 |
| `edges` / `edges3d` | 0 / 0 | 0 / 0 |
| `filter3d` | 0 (nearest) | 0 |
| drawers | engine A on core 2, engine B on core 0 | one drawer, both engines, core 0 |
| display thread | core 0 | core 0 |
| geometry thread | core 2 | core 1 (APT time-limit share); game's thread if unavailable |

`port.ini` also accepts startup-only keys: `filter=sharp|nearest|smooth|scale2x`
(default `sharp`), `stereo=on|off|1|0` (default `on`) and `wide=on|off`
(default `on`; `off` pillarboxes the upper screen and is not a performance
knob). Non-default values are logged with `[perf]`. The runtime X+R preset
cycle still overrides the filter.

**Failure behaviour:** a failed `NitroPort_PicaInit`, `NitroPort_ComposeInit`,
drawer start or V-blank thread start stops the display, logs the reason, and
shows `DisplayErrorScreen()`. The game thread is not started and there is no
1:1 display. A failed geometry thread still returns scene building to the
game's thread; that changes where scenes are built, not how they are shown.

**Verification so far:**

- `tests/test_hgss_game.py` passes natively on macOS (115 tests; the 4
  `ObjectAliasTests` need `gcc-10 -m32` and stay container-only).
- HeartGold and SoulSilver both compile and package with the remaster shell:
  `pokeheartgold.3dsx` 5,775,356 B (`sha256 7751bd1a…bfa5`) and
  `pokesoulsilver.3dsx` 5,775,412 B (`sha256 fbed84ee…e998`); both pass
  `validate_3dsx`. The game objects are `-O2` and the image is 29.77 MiB of the
  64 MiB process.
- **Performance (sessions 19–20):** the Old 3DS went from 0.3 to 10–14
  composes/s in the field; the New 3DS holds 30–32 composes/s with 59.7 fps and
  a 5.7 ms/frame drawer. What was measured, changed and broken is in
  [agent-notes/s20-performance-session.md](../progress/agent-notes/s20-performance-session.md).

## What can be copied, and what cannot

**Copy nearly unchanged (already in the tree):**

- `3ds_port/game/app/port_compose_pica.c`: the GPU screen compositor, glass
  effects, H-blank line handling, 3D draw and display pacing.
- `port_gx3d_pica.c`, `port_pica_abi.c` and the three `.v.pica` shaders: the
  3D renderer and shader blob.
- `port_upscale.c`, `port_render2d.c`, `port_wide.c`, `port_wideart.c`,
  `port_xbrz.c`, `port_shadow.c`: upscaling, split layers, wide rules, wide
  art, texture filters and sun shadows.
- `tools/wideart/`: the editor and capture-sharing tools.
- Platinum's drawer/GPU-thread layout in `3ds_port/game/app/main_3ds.c` is the
  reference design, but it references Platinum's game functions and must be
  translated into HGSS's shell rather than copied as a file.

**Must be HGSS-owned:**

- `hgss/game/app/main_3ds.c`: a new display loop built around `Heartgold_Run`
  and the HGSS settings/pak/save paths.
- `hgss/game/app/main_pc.c`: wide capture, control commands and a PC
  compositor path for art authoring.
- `hgss/game/app/heartgold.c`/`heartgold.h`: only if display settings or
  pack paths need new fields.
- The game-side window/frame/overlay hooks (patches to `bg_window.c`,
  `render_window.c`, battle UI, place-name sign, battle transition).
- Every per-screen layout (`WideScreenParts`/`WideSplitScreen` data) and every
  wide-art picture: HGSS's UI and art are different from Platinum's.
- The lower-menu buttons/icons/actions, if an HGSS lower menu is wanted.

**Do not copy blindly:**

- Platinum's `port_lowermenu.c` button labels and actions are Platinum's. The
  framework is useful; the content is not.
- Platinum's wide layouts are keyed to its screens. HGSS's text boxes, battle
  HUD and menus have different window geometry.
- Platinum's 3D camera assumptions: HGSS's field camera, battle backgrounds
  and Poképics need their own wide/stereo tuning.
- `port_wide.c`'s overlay names (`game_opening`, `battle`, ...) are Platinum's;
  HGSS overlays are `intro_title`, `field`, `OVY_6`/`OVY_7`, etc.

## Gap analysis

### 1. Shell/display wiring (done in Phase A)

Platinum's display architecture is in the HGSS shell:

- `3ds_port/game/app/port_compose_pica.c` is compiled into the program;
  `pica_classic_stubs.c` is gone;
- the game thread takes the DS state, 2D drawers render into
  `NitroPort_ComposeLayersOf(engine)`, and the display thread calls
  `NitroPort_ComposeFrame` / `NitroPort_ComposePublish`;
- the V-blank thread calls `NitroPort_ComposeVBlank` once per V-blank;
- `NitroPort_Gx3dSetWideView(320)` / `NitroPort_SetWideScreen(32)` are used
  while engine A is on the upper screen; `NitroPort_SetView(400,240)` for the
  pixel-perfect field remains Phase C;
- `NitroPort_Render2DTakeState` at handover; `NitroPort_Render2DSplit` with
  `snapshot`, `width`, `windowsToEdge`.

There is deliberately **no classic fallback**: the requirement is that the
remaster works on both consoles. A failed GPU display shows
`DisplayErrorScreen()`.

Acceptance: the game boots at 400x240/320x240 with the 3D visible, no
regression in the frame cap, audio, save or close paths. The build and the
automated tests pass; hardware acceptance is pending (see
[Phase A: implemented](#phase-a-implemented)).

### 2. HGSS window hooks (small patch, large payoff)

Add port-only calls in HGSS's `src/bg_window.c` and `src/render_window.c`,
analogous to Platinum patches 0012/0015:

- `AddWindowParameterized` / `AddWindow` -> `NitroPort_WindowAdded`;
- `RemoveWindow` / `WindowArray_Delete` -> `NitroPort_WindowRemoved`;
- `DrawFrameAndWindow1/2/3` -> `NitroPort_WindowFramed` with the matching
  frame kind, and the erase paths -> 0;
- `NitroPort_WideBgReset` when a BG is set up/freed.

HGSS's `Window` field order (`bgConfig`, `bgId`, `tilemapLeft`, `tilemapTop`,
`width`, `height`) matches the port's `still_there` read, so the generic
tracker can use HGSS windows directly.

This alone gives: text boxes widened to the screen edges, menus near the
left/right edge moved with their icons, and the place-name sign anchored
(once the anchor call is added).

### 3. Overlay/screen classification (small, but HGSS-specific)

`port_wide.c` recognises Platinum's overlay names (`game_opening`,
`game_start`, `end_credits`, `cutscenes`, `battle`). HGSS names differ
(`intro_title`, `credits`, `OVY_*`, `field`, `options_app`, ...). Without
classification:

- the intro/title may be treated as UI instead of pictures, so the bands are
  filled by UI rules instead of wide art;
- battle sprites beside the DS screen may be dropped.

Options, in order of cleanliness:

1. HGSS-owned shell glue calls `NitroPort_WideForcePictures()` each frame
   based on HGSS overlay IDs. This handles pictures but not `WideCells`
   battle sprites separately.
2. Add a small HGSS-owned port API for "picture overlays" / "battle overlay"
   names (or a registration function). This is a `3ds_port` change; if it is
   worth keeping, it should be an upstream-style local patch like the
   camera-plane one, recorded in `docs/upstream/`.
3. As a first pass, accept that bands on the intro/title use plain rules and
   that battle sprites at the edges may pop. Do not ship that as final.

### 4. Wide-screen-aware game code (medium patches)

Platinum patches tell the game how much extra width exists
(`NitroPort_WideScreenExtra`) so scripts/UI place things beyond the DS's
256-pixel screen. HGSS needs equivalents for:

- battle health boxes / action text at the wider screen's edges;
- place-name sign / field text anchoring;
- scripted NPCs entering/leaving at the DS edge (they would appear/disappear
  inside the wider view);
- battle background/platform and trainer slide-in extents;
- map neighbours (optional; Platinum brings people from adjacent maps in).

Without these, the first remaster pass still looks good but has edge pop-in
and some UI pinned to the DS's 256-wide area.

### 5. Per-screen wide layouts (large, art-heavy)

HGSS screens are designed for 256x192. A first remaster pass can leave them
centered at 1.25x with side bands; that is native resolution but not a
redesign. Platinum's later patches redraw screens in parts. HGSS equivalents
needed, roughly by effort:

| Screen/app | Recommended treatment | Effort |
| --- | --- | --- |
| Text boxes/dialogue | Generic frame widening + left/right anchoring | Low |
| Field start menu | Anchor to screen edge; icons move with it | Low-medium |
| Place-name sign | `NitroPort_WindowAnchor` left | Low |
| YES/NO, shop lists, simple menus | Generic framed-window anchoring | Low |
| Battle HUD (HP boxes, text box) | Move HP boxes to edges; widen text box; wide background | Medium |
| Bag | Parts layout: bag left, list to right edge, description across | High |
| Party | Parts layout: panels moved, background widened | High |
| Summary | Parts layout: left column, widened page panel | High |
| PC boxes | Parts layout: panel left, box right, wallpaper between | High |
| Pokédex list/page | Parts layout: label/picture left, rows right; page redraw | High |
| Trainer card | Split layout: body widened, fields left, photo right | Medium-high |
| Pokégear / bottom screen | Scale 1.25; optional native HGSS lower-menu redesign | Medium / high |
| Intro/title/credits | Wide art bands; picture detection first | Medium |
| Battle transition effects | Stretch over the wider field or keep 4:3 letterboxed | Medium |
| Pokéathlon / Frontier / Safari / mini-games | Keep 4:3 centered with bands initially | Low initially |

The table is a planning order, not a requirement for the first release. The
biggest visual win per unit of work is: shell/compositor, window hooks, field
pixel-perfect, battle HUD, then the large menu apps.

Where the HGSS UI lives (for the patches/layout work):

| Area | Main files |
| --- | --- |
| Window/frame plumbing | `src/bg_window.c`, `src/render_window.c` |
| Dialogue, YES/NO, script menus | `src/dialog_box.c`, `src/field/script_menu.c`, `src/field/touchscreen_choice_menu.c` |
| Field start menu / touch menu | `src/start_menu.c`, `src/field/touchscreen_menu.c` |
| Bag | `src/bag.c`, `src/bag_view.c`, `src/application/bag/bag_app.c`, `bag_app_display.c` |
| Party | `src/party_menu.c`, `src/party_menu_sprites.c`, `src/party_menu_items.c` |
| Summary | `src/pokemon_summary.c`, `src/pokemon_summary_scene.c`, `src/pokemon_summary_text.c` |
| PC boxes | `src/application/pc_box/pc_box.c`, `pc_box_ui.c`, `pc_box_sprites.c`, `pc_box_text.c` |
| Pokédex | `src/application/pokedex/pokedex_app.c`, `pokedex_screen_setup.c`, `pokedex_windows.c`, `pokedex_entry_popup.c` |
| Trainer card | `src/application/trainer_card/overlay_trainer_card*.c` |
| Battle HUD / text | `src/battle/battle_system.c`, `src/battle/battle_main.c`, the battle scripts and `src/battle/battle_bag_menu.c` / `battle_party_menu.c` |
| Options / save | `src/options_app.c`, `src/touch_save_app.c`, `src/application/check_savedata.c` |
| Pokégear / bottom screen | `src/field/gear_phone_calls.c`, the Pokégear apps (the bottom screen is already the game's touch UI) |


## 3D models and effects

HGSS's 3D is low-poly DS geometry: field maps/buildings/characters, the
intro/title, battle backgrounds, and the Poképic quads used for battle
Pokémon. The remaster does **not** replace models or textures. It can:

- draw the same geometry at 400x240 instead of 256x192;
- widen the field camera so the sides show more world (and widen the game's
  visibility tests through the port's `SetWideView`);
- render 3D at 2x and average down (`smooth3d`) for clean polygon edges;
- optionally use xBRZ/smooth texture filtering (costly; HGSS's textures are
  pixel art, so test screen by screen);
- use the 3D slider for depth, with per-scene convergence.

Cautions:

- HGSS's battle scenes are composed for 4:3. A wider camera can show the
  edges of the battle background or the sides of the arena; use the DS 4:3
  framing (320x192, centered in 400x240 with bands) for battles until the
  backgrounds/HUD are adapted.
- The Poképics are flat cards; stereo makes them look like cards. A low
  stereo strength or 2D-only battle is safer.
- The field camera and map-loader have DS-width assumptions; pixel-perfect
  widens the visible world a lot (144 more DS pixels across, 48 down). Test
  map loading, NPC visibility and scripted walks at the new edges.
- HGSS's 3D uses edge marking; Platinum disables outlines by default on the
  larger screen because they get in the way. Expect the same.

## Lower and touch screen

Before Phase A the lower screen was centered 1:1 at (32,24), but physical
touch was passed through as **raw 3DS coordinates**:

```c
touchX = touch.px;   /* 0..319 */
touchY = touch.py;   /* 0..239 */
NitroPort_SetTouch(down, touchX, touchY);   /* expects 0..255, 0..191 */
```

That was a pre-existing bug even in the 1:1 display: physical touches were
offset by (32,24) and the right/bottom 64x48 area was clamped. Scripted input
passes DS coordinates, so scripts worked and the bug was easy to miss.

Phase A fixes it: the lower screen fills 320x240, so physical touch is mapped
`dsX = px * 4 / 5`, `dsY = py * 4 / 5` (0..255, 0..191). Scripted input still
passes DS coordinates unchanged.

For a future lower menu with a DS panel, use `NitroPort_LowerMenuTouch`,
which returns whether the touch belongs to the game and maps the panel to DS
coordinates.
- The lower screen's content is already a touch UI in HGSS (Pokégear and
  friends). The cheapest native-resolution path is to scale it 1.25x and keep
  its own touch targets, not to force Platinum's eight-button menu onto it.
- Platinum's `port_lowermenu.c` is reusable as a framework (panel, buttons,
  press handling, tall panel) but its button set, icons, labels and actions
  are Platinum's. An HGSS lower menu would need HGSS buttons (Pokégear,
  party, bag, save, options, ...), HGSS icons, and game-side handlers.

Recommendation: **Phase 1: 1.25x lower screen with corrected touch mapping.**
A custom HGSS lower menu is a separate optional project.

## Memory, performance and platform

Platinum's remaster uses several render targets, textures and a bigger linear
heap; its docs put the display at about 14 MB of linear memory at minimum,
more for textures. Phase A adds Platinum's `__system_allocateHeaps`:

- with little free process memory (`< 48 MB`, e.g. the .cia in 64 MB mode),
  the game's heap gets 16 MB and the compositor/textures get the rest;
- otherwise the two heaps are split as libctru does, capped at 24 MB for the
  game and 32 MB for linear memory.

There is no classic fallback. The Old 3DS story is the remaster itself:
`smooth3d = 0` (3D drawn at 192-line native resolution instead of a 2x
render), the same 1.25x 2D and the same 400x240/320x240 presentation. The
remaining questions are frame rate and memory headroom on real hardware; the
container cannot answer them.

The existing 60 Hz frame cap stays: `WaitForFrameCap` still sleeps after the
frame is handed off, and presentation happens in the V-blank thread, not as a
second wait on the game thread.

## Tooling needed

- **PC runner**: port Platinum's `--wide`, `--wide-art DIR`, `--control DIR`,
  `capture`, `wideart`, `look`/`mshot` commands into
  `hgss/game/app/main_pc.c`. Wide captures and preview are how the side art is
  made; HGSS's current PC runner only screenshots 1:1.
- **Wide art**: reuse `tools/wideart/` and `port_wideart.c`. The editor and
  sharing tools are game-agnostic; HGSS captures need the HGSS PC runner.
- **Tests**: add renderer tests for 320->400 and 400x240 pixel-perfect paths
  (the port's existing tests cover the math; HGSS needs shell-level checks),
  plus a touch-coordinate test. Golden captures for HGSS do not exist yet.

## Proposed delivery phases

| Phase | Work | Acceptance |
| --- | --- | --- |
| A (implemented) | Compile/use compose; drawer threads; 1.25x 2D; 3D at native res; 320x240 lower; 400x240 upper; touch mapping | Builds and passes tests; boots and plays on hardware, 3D visible, no save/audio/close regression (hardware run pending) |
| B | HGSS window hooks + overlay classification; text boxes/menus anchor | Menus/text boxes use the wider screen |
| C | Field pixel-perfect + wide camera/visibility; battle HUD/wide text; scripted NPC edge handling | Field looks native and wider; battle HUD at edges; no pop-in at common scripts |
| D | Per-screen layouts/wide art (bag, party, summary, PC, dex, trainer card, intro/title) | One screen at a time, compared against captures on hardware |
| E | Optional HGSS lower menu / stereo tuning / texture filters | Only if the first pass is stable and the user wants it |

Phase A intentionally has no classic 1:1 path, so there is nothing to select
back to. Later phases may add display options (pixel field, filters, stereo),
but not a fallback that replaces the remaster when it is slow or its GPU
setup fails.

## Runtime pixel-filter presets

Holding **X+R** cycles five mixed 2D/3D filter presets, cheapest first:

| # | Label | 2D filter | 3D texture filter |
| --- | --- | --- | --- |
| 0 | `NEAREST/NEAREST` | `COMPOSE_FILTER_NEAREST` | 0 nearest |
| 1 | `SHARP/NEAREST` | `COMPOSE_FILTER_SHARP` | 0 nearest |
| 2 | `SHARP/SHARP` | `COMPOSE_FILTER_SHARP` | 2 sharp |
| 3 | `SHARP/XBRZ-S` | `COMPOSE_FILTER_SHARP` | 4 xBRZ smooth |
| 4 | `SCALE2X/XBRZ-S` | `COMPOSE_FILTER_SCALE2X` | 4 xBRZ smooth |

The cycle starts on `SHARP/NEAREST` (1). It never changes `smooth3d`, so the
upper screen's 3D/stereo path is untouched, and all five presets are safe on
both Old and New 3DS. A small text toast on the lower screen names the preset
for about two seconds. X and R are masked from the game while the combo is
held. The 2D filter is latched into each published layer set; the 3D texture
filter is applied on the display thread at the next frame. The toast is drawn
only into the lower screen's layers, behind and (when the picture is split)
in front of the 3D.
