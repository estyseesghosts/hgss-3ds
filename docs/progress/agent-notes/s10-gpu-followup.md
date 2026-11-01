# S10: the 3D works; the worker was starved, and `w = 0` fans

Follow-up to the ninth hardware run (log-8, photo IMG20261101131707). The
pipeline from S9 is correct in principle and does draw: the lower-screen title
shows Ho-oh, the game reaches the field, and the player can walk through the
house and outside to Lyra and Marill. The run exposed two defects and a frame
rate problem.

## What log-8 says

- **3D images:** 105 over 4,680 frames -- about **1.1 a second** -- while the
  geometry builder made 120 scenes every 240 frames (30 a second). The 3D was
  drawn but almost never updated. That is the "unplayably slow" walking: the
  room changes once a second.
- **Frame times:** 7 samples, `total` mean **24.7 ms (40.4 Hz)**; field
  19.8-20.9 ms (48-50 Hz); intro 31-36 ms (28-32 Hz). `draw_us` is the
  software 2D composite (the GPU worker's render was not overlapping it).
- **Audio:** `audio: 82 dropped frames of 23821` (0.34%) and a distorted/slow
  intro. The audio thread was `0x31`, below the game, on the game's core, in
  competition with the starved GPU worker.
- **The photo:** black triangular shards fanning from the **centre** of the
  bottom screen over Ho-oh.

## Root causes

### The GPU worker was on the game's core

`GpuStart` created the display thread with core `-2` (the game's core) and
priority `priority + 1`. The port's software 2D composite holds that core for
20-30 ms of a frame; the worker at a lower priority only runs when the game
blocks. `gfxSwapBuffers` is that block, but a frame that already overruns the
V-blank returns from it at once, so the worker often got no time at all. The
game's thread then converted the image itself inside `NitroPort_Render2D`.

The fix is the one Platinum's architecture implies: on a New 3DS, core **2**
is free (the geometry thread uses it between scenes), so the render overlaps
the composite instead of queueing behind it. On an Old 3DS core 2 does not
exist, so the fallback is the game's core.

### A camera-plane vertex becomes a fan from the centre

The port's clipper (`port_gx3d.c:outcode`, `stage_triangle`) tests six planes
±x/y/z <= w. A vertex at `(0, 0, 0, 0)` satisfies all six: `w + v < 0` is
false for 0. It survives. The GPU vertex is built as `(ndc * w, w)`, so
`w = 0` gives `(0, 0, z, 0)`; the PICA's divide by `w` puts it at the centre
of the render target. Every triangle touching it fans out from there. On the
title, a polygon crossing the camera plane produced the shards over Ho-oh.

The proper fix is a `w > 0` near plane in the port's clipper (an upstream
change; `3ds_port/` is byte-identical and was not edited). HGSS owns a
workaround: `pica_scene.c` degenerates each stored triangle with a `w <= 0`
vertex while the GPU thread owns the scene, and counts them
(`camera-plane drops`).

## What changed

- `hgss/game/app/pica_scene.c` (new): `Hgss_PicaDropBehindCamera`. A triangle
  with no vertex the GPU can divide by becomes a finite point at `w = 1`.
- `hgss/game/app/main_3ds.c`
  - Calls it on the acquired scene before `NitroPort_PicaRender`.
  - GPU/display thread on core 2 on a New 3DS, with a retry on the game's
    core; logs `[gx3d] display on core 2`.
  - `camera-plane drops` in the 600-frame and exit lines.
  - Audio thread priority `0x31` -> `0x2e`.
- `tools/hgss_game.py`: the 3DS app objects are compiled at `-O2` (the port
  archives already are; the app was `-O0`). `pica_scene.c` added to the 3DS
  app sources.
- `tests/game/pica_scene/run.sh` + `test_pica_scene.c` (new): host test for
  the workaround (w = 0, w < 0, all-behind, tiny positive).
- `tests/test_hgss_game.py`: the app-source, thread, workaround-order and
  audio-priority assertions.

## Local verification

```bash
python3 tools/hgss_game.py --target 3ds --keep-going --out /work/final-3ds2 -j6
#   1092/1092 compiled, 0 failed; linked, 0 unresolved
#   packaged pokeheartgold.3dsx (9277656 bytes)
3dsxdump /work/final-3ds2/pokeheartgold.3dsx /tmp/v2.bin
#   CODE 2126, RODATA 112, DATA 13, BSS 4446 pages; rc 0
arm-none-eabi-nm .../pokeheartgold-3ds.elf | grep -E 'Hgss_PicaDrop|NitroPort_Pica|Gx3dThread'
#   T Hgss_PicaDropBehindCamera
#   T NitroPort_PicaInit / PicaRender / PicaExit
#   T NitroPort_Gx3dThreadMain / Flush / ConvertPending / ScenePublished
make test        # exit 0: 327 Python tests, OK; 21 SDK suites
make port-tests  # exit 0: 17,122 checks, 0 failed
bash tests/game/pica_scene/run.sh
#   pica_scene: all checks passed
sha256sum /work/final-3ds2/pokeheartgold.3dsx
#   ce683574cfe912d4896a6db18089c6b53773bfd893e01a2c0b2138f9e9ad4afd
```

## Pending the owner's hardware run

- `[gx3d] display on core 2` in the log.
- `3D images:` near **300 per 600 frames** (30 a second) rather than 10, and
  walking animates instead of updating once a second.
- No black fan from the centre; `camera-plane drops` is a small number (it
  should be non-zero in the title if the diagnosis is right).
- `audio: N dropped frames of M` near zero, and the intro music not
  distorted.
- The after frame-time log (`tools/frametime_report.py`).

## Notes

- The PC reference at frame 1200 (intro, Ho-oh) is clean: the fan is specific
  to the PICA's `w` handling, not the scene data in general.
- The remaining cost is the software 2D composite (~20 ms). The GPU/display
  thread moving off core 0 should overlap its 3D render with it, but if the
  field still misses 60 Hz, the next step is the port's GPU compositor
  (`port_compose_pica.c`), not another change to this classic path.
