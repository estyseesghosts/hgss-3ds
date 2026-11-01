# Local changes to the vendored Platinum port

`3ds_port/` is vendored from [GabeSud0/pokeplatinum-3ds](https://github.com/GabeSud0/pokeplatinum-3ds)
at the commit in `platinum.lock` (GPL-3.0). Everything here is Platinum's
except the one change below.

## 1. Camera-point vertex (`nitro/include/nitro_port_gx3d.h`,
## `nitro/src/port_gx3d.c`, `nitro/src/port_gx3d_vertex.c`)

HGSS's title quads and quad strips contain vertices that transform exactly to
clip `(0,0,0,0)`. The DS and the software reference put that point at screen
`(0,0)` and give it `inv_w = 0`, so it contributes no texture sample: the fan
is the rim vertices' feathers. The PICA cannot divide by `w = 0`, so
`to_screen()` stores `clip[3] = 1` for exactly that vertex and marks it
`NITRO_GX3D_VERTEX_CAMERA_POINT`. `put_vertex()` then gives the GPU a huge
`w` (2^18) instead of `1/4096`: the camera point's `1/w` (the perspective
weight) becomes negligible like the software's `inv_w = 0`, so the PICA's
perspective-correct interpolation no longer lets its grey texel cover the
whole fan. The position is unchanged in NDC, and z scales with `w` in a
Z-buffer scene (a W-buffer scene stores the DS `W` itself), so the stored
depth is the same. The same path is in `NitroPort_Gx3dBuildOpaqueVertices`
for the classic renderer.

The regression test is `nitro/tests/test_gx3d.c` `test_camera_point_vertex`.

The patch against Platinum is
[`docs/upstream/platinum-camera-point.patch`](../../docs/upstream/platinum-camera-point.patch).
It is the content of the upstream PR **"gx3d: give a camera-plane vertex a
positive w"**. The decisions are in [`docs/decisions.md`](../../docs/decisions.md)
and [`docs/progress/agent-notes/s11-3d-camera-point.md`](../../docs/progress/agent-notes/s11-3d-camera-point.md)
(and the follow-up in `s13-nns-volume-table.md`).

## Updating the vendored copy

1. Re-vendor `3ds_port/` at a new `platinum.lock` commit.
2. Try to drop this patch if the upstream PR has landed; otherwise apply
   `docs/upstream/platinum-camera-point.patch` from the Platinum root
   (`patch -p1 < ...`).
3. Run `make port-tests` and `make test`; the camera-point test must pass.
4. Record the new pin and any patch change in `docs/decisions.md`.
