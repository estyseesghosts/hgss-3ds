# Session 18 — 3DS remaster, Phase A

## Goal

Give HGSS the Platinum port's native-resolution 3DS display, and make it the
**only** display path. The user's constraint: it must also work on an Old
3DS,
with no classic 1:1 fallback.

## What landed

### Shell

`hgss/game/app/main_3ds.c` now uses Platinum's display architecture:

- `NitroPort_ComposeInit` after `NitroPort_PicaInit` on the display thread.
- Three thread kinds:
  - display/GPU (`GpuComposeLoop`): owns the newest scene, recomposes it when
    a drawer publishes new 2D, keeps a scene waiting for its 2D or an LCD
    framebuffer, and increments `sGpuScenes` when a scene is acquired;
  - 2D drawers (`DrawerMain`): engine A and B on a New 3DS (A on core 2), one
    drawer for both on an Old 3DS (core 0);
  - V-blank (`VBlankMain`): presents due frames and wakes the display thread.
- `DrawFrame` offers the frame with `NitroPort_Compose2DOffered`, captures
  H-blank lines once (`NitroPort_HBlankLinesOnce`), hands the repeating
  weather BGs to the compositor, copies the state with
  `NitroPort_Render2DTakeState` and signals free drawers.
- `RenderScreen` uses `NitroPort_WideLayout` on the upper screen,
  `NitroPort_Render2DSplit[IfChanged]`, fills the `ComposeLayers` fields
  (including the weather BG picture), and `ShowScreen` publishes.
- Touch maps `px * 4/5`, `py * 4/5`.
- `NitroPort_Gx3dSetWideView(320)` while engine A is on top;
  `NitroPort_SetWideScreen(32)` at start.
- Platinum's `__system_allocateHeaps` reserves linear memory for compose.
- `DisplayErrorScreen()` is the failure path; the game thread does not start.

### Build

`tools/hgss_game.py`:

- compiles `3ds_port/game/app/port_compose_pica.c`;
- drops `pica_classic_stubs.c`;
- adds `3ds_port/game/app` to the app include path.

### Settings

| Setting | New 3DS | Old 3DS |
| --- | --- | --- |
| upper | 320x192 -> 400x240 | same |
| lower | 256x192 -> 320x240 | same |
| filter | sharp | sharp |
| smooth3d | 2 | 0 |
| stereo | 1 | 1 |
| edges/edges3d | 0/0 | 0/0 |
| filter3d | nearest | nearest |

## Tests

`tests/test_hgss_game.py` was updated from the old classic-display
expectations:

- `test_shell_presents_through_the_remaster_only` replaces the blit test.
- `test_shell_runs_the_remaster_and_geometry_threads` replaces the classic
  GPU-worker test.
- `test_shell_refuses_to_fall_back_to_2d` replaces the "falls back to 2D"
  test: a failed display must show the error screen.
- `test_shell_has_a_console_only_on_its_error_screens` checks the pack/display
  error paths.
- `test_shell_waits_after_the_draw_never_before_it` now checks the hand-off
  instead of `gfxSwapBuffers`.
- `test_shell_hooks_are_strong_references` lists the hooks the shell still
  declares.
- `test_3ds_program_has_the_pica_backend` expects `port_compose_pica.c` and
  no `pica_classic_stubs.c`.

In the Linux build container the whole file passes (102 tests). On macOS the
four `ObjectAliasTests` fail before any assertion because `gcc-10` and the
32-bit toolchain are not installed; that is an environment limit, not a
regression (the same tests pass in the container).

## Builds

Both programs build with the remaster app objects and pass
`tools.hgss_game.validate_3dsx`:

| Program | Size | SHA-256 |
| --- | --- | --- |
| `pokeheartgold.3dsx` | 9,403,732 B | `9c9c63dff3853e4b2fc85b712c94512c0ea9b5b642286fddb353b8ed96f97656` |
| `pokesoulsilver.3dsx` | 9,403,804 B | `774a98c13541598717e0f8aad89fcbaf64e1d0d9c11c355294470fb3becb0c90` |

Staged in `dist/sd-card/3ds/`, `dist/soulsilver-sd-card/3ds/` (SS) and
`hgss-private/sd-card/3ds/`.

## Still open

- Real Old 3DS and New 3DS runs: frame rate, memory headroom, touch
  alignment, stereo/slider, audio and the HOME-menu close.
- Phase B-E: HGSS window hooks, pixel-perfect field, per-screen layouts and
  the optional lower menu.
- If an Old 3DS cannot hold a playable rate, tune the 2D filter/drawer
  scheduling; do **not** reintroduce a 1:1 fallback.

## Wave 2: filters, performance, trainer crash

### X+R pixel-filter presets

`main_3ds.c` cycles five mixed presets with X+R and shows a 2-second toast on
the lower screen:

| Preset | 2D | 3D |
| --- | --- | --- |
| `NEAREST/NEAREST` | nearest | nearest |
| `SHARP/NEAREST` | sharp | nearest |
| `SHARP/SHARP` | sharp | sharp |
| `SHARP/XBRZ-S` | sharp | xBRZ smooth |
| `SCALE2X/XBRZ-S` | Scale2x | xBRZ smooth |

`NitroPort_ComposeSetFilter` stores a packed atomic request; the display
thread applies it and calls `NitroPort_PicaTextureFilter` only there. Each
published `LayerSet` latches its 2D filter so publish and sampling cannot
disagree. The toast uses a local 5x7 font drawn into the lower screen's
`under` (and `over` when split), so it goes through the compositor.

Important: the presets change sampling, not the texture scale. A **startup**
`filter=` in `port.ini` is what reaches the 1x texture path.

### Performance wave 1

- One compose wake per game frame: `DrawFrame` counts handed drawers with an
  atomic add; the last drawer signals the display thread. `ShowScreen` no
  longer wakes it.
- MASTER_BRIGHT (0x6C) is excluded from the 2D small signature. When only
  brightness changes, the drawer skips the 320x192 render but still publishes
  the untouched layers so fades do not stick.
- The over-layer publish is skipped when `over_rows` found no front pixels.
- `port.ini` accepts `filter=`, `stereo=` and `wide=`.
- `[perf] display`, `[perf] geometry`, `[perf] audio` and `[perf] drawer`
  lines now report real thread costs; `FRAMETIME` is renamed
  `game_us`/`state_us` so it no longer implies the display was measured.

### Trainer walk-up crash

Patch 1908: `TrainerApproach_IsDone`/`Delete` prototypes now take `SysTask *`,
`ScrCmd_TrainerStepTowardsPlayer` passes `*approachTask`, and
`TrainerApproach_GetDone` tolerates NULL. This is the log-14 data abort:
the no-argument call left the callee with `r0` pointing at the ScriptEnv
attribute, then `work->done` dereferenced `0x4`. Port-only; the DS `#else`
text is unchanged.

### Verification

- `test_hgss_game.py`: 102 tests pass in the container.
- `make test`: 384 Python tests + 19 SDK suites pass.
- Both `.3dsx` build with the filter, performance and 1908 fixes.
- Hardware still needs the frame-time/geometry numbers and the trainer walk-up
  repro on the SD save.

## Wave 3: PC box MOVE POKEMON crash

Selecting MOVE POKEMON launched the PC box as mode 4. `PCBox_State_Start`
handled only modes 0-3 and left `nextState` uninitialised; the fade returned
the previous frame's `PCBoxWork *` as the state index, and
`PCBox_RunStateMachine` faulted on `gPCBoxStateTable[heap pointer]`
(`log-move-pc-crash.txt`, `*state == work == 0x01243658`).

Patch 1909 initialises/defaults the start state, maps mode 4 to the move
layout, defaults the mode grid layout, and bounds-checks the dispatch
(port-only; DS `#else` text unchanged). Deposit/withdraw were unaffected
because they use modes 0/1.

## Wave 4: black screens after HOME/sleep

`log-black-screen.txt` ends at `paused (APT hook 0)` with no restore. The
display thread could start a compose after `sSuspended` because
`ComposeGpuBusy()` only sees already-submitted GPU work. APT then took the GPU
mid-build, wedging the GX queue and preventing HOME from completing.

The shell now brackets both compose calls with `sComposing` (busy-first, then
check `sSuspended`), waits for it before `ComposeHold(1)`, and defers the real
resume to `NitroPort_OnFrame` after `aptMainLoop` returns so citro3d's restore
hook has run. `sAptHome`/`sAptAsleep` guards and detailed `[apt]` logs added.
