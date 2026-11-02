# Local changes to the vendored Platinum port

`3ds_port/` is vendored from [GabeSud0/pokeplatinum-3ds](https://github.com/GabeSud0/pokeplatinum-3ds)
at the commit in `platinum.lock` (GPL-3.0). Everything here is Platinum's
except the one change below.

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
