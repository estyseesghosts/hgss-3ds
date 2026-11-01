# Local changes to the vendored Platinum port

`3ds_port/` is vendored from [GabeSud0/pokeplatinum-3ds](https://github.com/GabeSud0/pokeplatinum-3ds)
at the commit in `platinum.lock` (GPL-3.0). Everything here is Platinum's
except the one change below.

## 1. Camera-point vertex (`nitro/src/port_gx3d.c`)

HGSS's title quads and quad strips contain vertices that transform exactly to
clip `(0,0,0,0)`. The DS and the software reference put that point at screen
`(0,0)`; the PICA cannot divide by `w = 0`, so every triangle touching it was
black. `to_screen()` now stores `clip[3] = 1` (20.12) for exactly that vertex,
leaving its screen position, depth and `inv_w` unchanged. The GPU divides it
to the same corner with valid texture/colour.

The regression test is `nitro/tests/test_gx3d.c` `test_camera_point_vertex`.

The patch against Platinum is
[`docs/upstream/platinum-camera-point.patch`](../../docs/upstream/platinum-camera-point.patch).
It is the content of the upstream PR **"gx3d: give a camera-plane vertex a
positive w"**. The decision is also in
[`docs/decisions.md`](../../docs/decisions.md) and
[`docs/progress/agent-notes/s11-3d-camera-point.md`](../../docs/progress/agent-notes/s11-3d-camera-point.md).

## Updating the vendored copy

1. Re-vendor `3ds_port/` at a new `platinum.lock` commit.
2. Try to drop this patch if the upstream PR has landed; otherwise apply
   `docs/upstream/platinum-camera-point.patch` from the Platinum root
   (`patch -p1 < ...`).
3. Run `make port-tests` and `make test`; the camera-point test must pass.
4. Record the new pin and any patch change in `docs/decisions.md`.
