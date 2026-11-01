# S9: the 3D GPU pipeline on the 3DS

Continues `handoff-gpu-pipeline.md`. The task: bring Platinum's PICA pipeline
into the HGSS shell so `NitroPort_Gx3dAcquireImage` has an image on the
console.

## Diagnosis (confirmed, not re-derived)

`3ds_port/nitro/src/port_gx3d_image.c`:

- On the PC, `NitroPort_Gx3dAcquireImage` calls `render_host()`: it rasterises
  the newest scene and publishes it itself.
- On the 3DS (`#ifdef __3DS__`), `render_host()` is compiled out. Acquire only
  calls `convert_pending()`, which converts an image handed over by
  `NitroPort_Gx3dSubmitRGBA8`; `sPublished` stays `-1` without a GPU worker, so
  Acquire returns NULL and engine A draws 2D only.
- `NitroPort_Gx3dSubmitRGBA8` increments `sPublishCount` immediately, so
  `NitroPort_Gx3dImageCount()` is the direct evidence that a worker submitted
  images. The `[gx3d] ... scenes: N vtx` lines are the scene builder; they are
  not evidence of drawing.

## What Platinum does (mapped)

`3ds_port/game/app/main_3ds.c`:

- `StartGpu` (761) creates a core-0 display thread at `priority+1`; `GpuMain`
  (725) calls `NitroPort_PicaInit`, then either `GpuClassicLoop` (595) or the
  remaster's `GpuComposeLoop` (632). The classic loop waits on `sGpuWake`,
  acquires a scene, calls `NitroPort_PicaRender`, releases it, and notifies
  the drawers (`DrawersImageHandedOver`).
- `NitroPort_Gx3dScenePublished` (543) is a strong override of the port's weak
  no-op; it signals the GPU thread at each SWAP.
- `StartGeometry` (917) / `StartGeometryOn` (903) start a thread that runs
  `NitroPort_Gx3dThreadMain` with LightEvent ops (`GeometryOps`, 878).
  `NitroPort_Gx3dFlush` is called once a frame at 2569, before the drawers.
- The classic path is `port_gx3d_pica.c`: an off-screen 256x192 RGBA8 target,
  `C3D_FrameBegin(0)`/`FrameEnd(GX_CMDLIST_FLUSH)`, a `C3D_SyncDisplayTransfer`
  to a linear image, then `NitroPort_Gx3dSubmitRGBA8` (1369). The 2D drawer
  converts it when it next acquires (the `if (!sRemaster &&
  NitroPort_Gx3dConvertPending())` path in `DrawerMain`, 1780).
- `port_pica_abi.c` exposes libctru's `DVLE_s`/`C3D_FrameBuf` layout to the
  port; it must be compiled without the port's `-fno-short-enums`.
- The shaders are `gx3d_{passthrough,shadow_cast,shadow_recv}.v.pica`, in
  that DVLE order, assembled by `picasso` and embedded by `bin2s`
  (`Platinum's Makefile`, 469-475). `picasso` and `bin2s` are already in the
  container's devkitPro tools, so this is **not** a new tool dependency.

`port_compose_pica.c` (the remaster's GPU composition) is not needed for the
classic path; `port_gx3d_pica.c` only references two of its profiler hooks,
which are no-ops here.

## What changed

- `hgss/game/app/main_3ds.c`
  - Includes `<nitro_port_gx3d.h>` (standalone, safe next to libctru).
  - Declares `NitroPort_PicaInit/Render/Exit`.
  - `NitroPort_Gx3dScenePublished` strong override → signal the GPU thread.
  - `GpuMain`: PicaInit, then acquire/render/release/convert. Converts on the
    GPU thread so the game's frame only reads a published image.
  - `GpuStart`/`GpuStop`: safe fallback if the thread or PicaInit fails; a
    thread that will not stop sets `sGpuStopFailed` and the main exits the
    process after closing the log rather than tearing citro3d down around it.
  - `GpuStartGeometry`/`GpuStopGeometry`: LightEvent ops, one flush per frame,
    and a return to the game's thread if the thread cannot start.
  - `NitroPort_OnFrame`: `NitroPort_Gx3dFlush()` before the composite;
    `3D images: N (scenes drawn: M)` every 600 frames.
  - startup/exit log lines, and `[gx3d] N 3D images, M scenes drawn`.
- `hgss/game/app/pica_classic_stubs.c` (new): no-op `NitroPort_PicaCut` and
  `NitroPort_PicaProfileMark`, the two compose-only hooks.
- `tools/hgss_game.py`
  - `app_sources("3ds")` adds `port_gx3d_pica.c`, `port_pica_abi.c`, and the
    stubs file.
  - `gx3d_shader_sources()`/`gx3d_shader_object()` build the DVLB with
    `picasso`, embed it with `bin2s`, and assemble it with
    `arm-none-eabi-as`.
  - `port_pica_abi.c` is compiled separately without `-fno-short-enums`.
  - An app source failure now stops the build before the link.
- `tests/test_hgss_game.py`: the old assertion that `main_3ds.c` must not
  mention `NitroPort_Gx3dImageCount` is inverted (it is logged now); new tests
  cover the PICA app objects, shader order, the threads, the strong callback,
  flush-before-composite, and the 2D fallback.

## Local verification

Run in the container on 2026-11-01, tree with no patch changes:

```bash
python3 tools/hgss_game.py --target 3ds --keep-going --out /work/final-3ds1 -j6
#   1092/1092 compiled, 0 failed; 150/150 .s; linked, 0 unresolved
#   packaged /work/final-3ds1/pokeheartgold.3dsx (9300752 bytes)
/opt/devkitpro/tools/bin/3dsxdump /work/final-3ds1/pokeheartgold.3dsx /tmp/v.bin
#   CODE 2131, RODATA 113, DATA 13, BSS 4446 pages; rc 0
arm-none-eabi-nm /work/final-3ds1/pokeheartgold-3ds.elf | grep -E 'NitroPort_Pica|Gx3dThread'
#   T NitroPort_PicaInit / PicaRender / PicaExit, PicaBindVertices, ...
#   T NitroPort_Gx3dThreadMain, Gx3dFlush, Gx3dConvertPending
# plus: 001027b4 T NitroPort_Gx3dScenePublished  (strong, not w)
make test        # exit 0: 327 Python tests, OK; 21 SDK suites, all green
make port-tests  # exit 0: 17,122 checks, 0 failed
sha256sum /work/final-3ds1/pokeheartgold.3dsx
#   d494b06991745115313217990b326ebac8b5a4d602696c211fdd163fe7298248
```

The build's `validate_3dsx` runs automatically on the packaged file and
accepted it. `git status` also carries an unrelated uncommitted
`hgss/game/app/main_pc.c` (`--render-3ds`); it is not part of this change.

## Pending the owner's hardware run

- The room/player sprite and the intro movie's 3D on the console.
- Startup `[gx3d] PICA ready (classic path)` / `[gx3d] geometry on its own
  thread (core 2)`, and a rising `3D images:` counter every 600 frames.
- Both 2D layers still correct, no flicker, clean `the HOME menu asked to
  close` → `stopping`, audio still ends with `audio: N dropped frames of M`.
- The frame-time log after this change (`tools/frametime_report.py`); the
  before is log-7: 2 samples, 24.7 ms, 40.5 Hz, with no 3D.
