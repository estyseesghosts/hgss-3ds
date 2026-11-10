# Local changes to the vendored Platinum port

`3ds_port/` is vendored from [GabeSud0/pokeplatinum-3ds](https://github.com/GabeSud0/pokeplatinum-3ds)
at the commit in `platinum.lock` (GPL-3.0). Everything here is Platinum's
except the changes below.

## 3. Compose-phase counters (`game/app/port_compose_pica.c`)

The stage and end phases accumulated with `sDbgStageTicks -= now ... += now`,
which left the counter holding a *negative* `u64` for the whole phase. The
shell samples those counters from the game's thread every 600 frames, so a
sample taken inside the window read ~7e16 ticks: the OFF hardware log carries
`stage 16102.78 ms/draw` and `end 274113216854.52`, impossible values that
would have poisoned any timing claim. Both phases now accumulate in locals and
are published once, atomically, when the frame's own time is complete; the
five phase counters are also read and written atomically so a 64-bit value
cannot tear. A frame that returns early drops its own sample instead of
corrupting the window.

## 2. Instrumentation gate (`nitro/src/port_render2d.c`, `nitro/src/port_gx3d.c`)

Added 2026-11-09 for the benchmark contract in `PICA200-INSTRUMENTATION.md` §B.
`HGSS_PROFILE_OFF` (set by `tools/hgss_game.py` / `tools/hgss_nitro.py` when
`HGSS_PROFILE=off`) compiles out the per-draw `svcGetSystemTick()` reads in
`R2D_NOW()` (26 per engine draw, about 52 a frame) and the per-120-scene
`[gx3d]` diagnostic logging. The PROFILE build is unchanged: without the
define every branch behaves exactly as before.

Why: timing claims must come from an uninstrumented artifact, and the
profiler's own cost must be measured, not assumed. `tools/bench_ab.py` runs
matched alternating trials of the two builds. Activation is checked by
`arm-none-eabi-nm` (`port_render2d.o` references `svcGetSystemTick` in PROFILE
and not in OFF) and by the `-profoff` build banner.

This is a build-time gate, not a behaviour change: no rendering path, pixel
or counter differs between the builds when the define is absent.

## 1. Camera-plane triangles (`nitro/src/port_gx3d.c`)

HGSS's title quads and quad strips contain vertices whose clip coordinates are
exactly `(0,0,0,0)` — a vertex on the camera plane. Both edges from it to a
real vertex project to that vertex's screen point, so the triangle has **zero
screen area**; the DS's clipper stores no polygon for it. The software
rasteriser took its screen `(0,0)` literally and drew the whole left wing of
Ho-oh as a fan out of the top-left corner. `stage_triangle()` now detects the
exact camera-plane vertex (`camera_plane()`) and drops the polygon before it is
staged, for both the software reference and the PICA scene. Dropping every
`w <= 0` triangle instead (the S10 workaround) also removed real wing
geometry and left a gap.

The regression test is `nitro/tests/test_gx3d.c`
`test_camera_plane_triangle`: a triangle with one camera-plane vertex stores
no polygon.

The patch against Platinum is
[`docs/upstream/platinum-camera-point.patch`](../../docs/upstream/platinum-camera-point.patch).
It is the content of the upstream PR **"gx3d: drop the camera-plane
triangle"**. The decisions are in [`docs/decisions.md`](../../docs/decisions.md)
and [`docs/progress/agent-notes/s13-nns-volume-table.md`](../../docs/progress/agent-notes/s13-nns-volume-table.md).

## Updating the vendored copy

1. Re-vendor `3ds_port/` at a new `platinum.lock` commit.
2. Try to drop this patch if the upstream PR has landed; otherwise apply
   `docs/upstream/platinum-camera-point.patch` from the Platinum root
   (`patch -p1 < ...`).
3. Run `make port-tests` and `make test`; the camera-plane test must pass.
4. Record the new pin and any patch change in `docs/decisions.md`.
