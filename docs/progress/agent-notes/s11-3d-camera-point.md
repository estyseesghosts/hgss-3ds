# S11: the camera-point vertex gets a positive `w`

Follow-up to S10's `w = 0` workaround. The title screen's black shards were
not a missing-near-plane problem in the clipper and not a strip restart: the
game sends real vertices that transform exactly to clip `(0, 0, 0, 0)`, and
the PICA cannot divide by `w = 0`. The HGSS-side drop workaround removed the
geometry (leaving a transparent gap); the fix belongs in the port's vertex
projection, where the GPU vertex is built.

## Evidence: the primitive is a quad strip or a quad, not a strip restart

A scratch build (`/work/diagpc/prim/port_gx3d_prim.c`, not in the repo) copied
`port_gx3d.c` and counted `s.primitive` in `submit_vertex` for every vertex
whose computed clip is exactly `(0, 0, 0, 0)`. The PC runner was relaunched to
frame 5,400; the totals over the whole run were:

| `BEGIN_VTXS` low bits | primitive (`GX_g3.h`) | occurrences |
| --- | --- | --- |
| 1 | `GX_BEGIN_QUADS` | 17,016 |
| 3 | `GX_BEGIN_QUAD_STRIP` | 22,688 |

No zero vertex occurred in a triangle list (0) or a triangle strip (`tristrip
= 0` throughout). The low-bits encoding is `0 triangles, 1 quads, 2 triangle
strip, 3 quad strip` (`build/upstream/lib/NitroSDK/include/GX_g3.h`), not the
brief's "3 = fan". The **fan is the visible result**: a quad strip whose spine
is the camera vertex emits every triangle with that one vertex, and a quad
with a camera corner does the same for its two triangles.

The zero vertex is not a restart sentinel. A restart marker would be a
repeated vertex in a triangle strip; there are no triangle-strip zero
vertices. Each occurrence is a distinct model-space point (e.g.
`(379, 22, 332)`, `(-405, 22, 319)`), transformed to the camera by the
position/projection pair, with a valid and distinct `s`/`t`/`colour`
(`s = -16.4, t = 15.6, colour = 0x18c6` on the dumped title scene). The
dropped set is 76 triangles per scene at frame 4800, all opaque, textured,
cull-both, total area 24,502 px, every one with a big bounding box; the good
vertices sit around the screen (`(229,102) (253,117)`, etc.). This is
mechanism A (camera point), not B (strip restart).

## What the reference does with it

The software rasteriser receives the scene with screen `(0, 0)` for that
vertex (`to_screen` leaves `x = y = 0` when `w <= 0`) and draws the fan from
the **top-left corner**. That is `/work/diagpc/3d_frame_0004800.png`, the
clean title reference: Ho-oh's long left-wing feathers run from the corner to
the wing root. Dropping the same triangles on the PC (the S10 workaround)
removes that wing: the transparent gap the 3DS showed. The ninth-run hardware
photo (`IMG20261101131707.jpg`) has the black fan's apex at the same top-left
corner, so the PICA's failure and the DS's projection agree on the corner;
only the divide is broken.

The brief's leading hypothesis said the camera point should project to the
**viewport centre**. A scratch build was tried with
`x = x0 + width/2`, `y = top + height/2`, `clip[3] = 1`: at frame 4800 it
draws the fan from the centre and cuts Ho-oh's left wing off, and the software
frame changes completely (sha256 `7f6b3ec3…` vs the reference `7d7ea8f5…`).
That is not the reference and not the photo. Keeping the stored `(0, 0)` and
setting only a positive `w` makes the software frame **byte-identical** to the
reference (`7d7ea8f5…`), so that is the fix.

## The change

`3ds_port/nitro/src/port_gx3d.c`, `to_screen()`:

```c
    /* A vertex exactly at the camera: clip (0,0,0,0). It survives the six
     * clip planes (w + v < 0 is false at 0), and the viewport transform
     * below leaves it at screen (0,0), as the PC reference and the DS's
     * own title do. The GPU cannot divide by w = 0, though: give it the
     * smallest positive w (20.12) so the PICA divides it to that corner ...
     */
    if(v->clip[0]==0 && v->clip[1]==0 && v->clip[2]==0 && v->clip[3]==0)
        out.clip[3]=1;
```

No other vertex changes. `x`, `y`, `depth` (the far end of the Z range) and
`inv_w` (0, as before) are untouched, so the PC rasteriser's scene is
unchanged. `put_vertex()` then builds `(ndc * w, w)` with `w = 1/4096 > 0`,
NDC `(-1, +1)` (the same corner), and valid `s`/`t`/`colour`; the PICA can
interpolate the fan.

Removed with it:

- `hgss/game/app/pica_scene.c` (`Hgss_PicaDropBehindCamera`),
- its app-source line in `tools/hgss_game.py`,
- the declaration, call, `sGpuBehind` counter and `camera-plane drops` fields
  in `hgss/game/app/main_3ds.c`,
- `tests/game/pica_scene/` and the workaround assertions in
  `tests/test_hgss_game.py` (replaced by one that checks the port hunk and
  that the drop is gone).

## Tests

`3ds_port/nitro/tests/test_gx3d.c`, `test_camera_point_vertex`: loads the
title's projection (`w = -z`), emits a triangle with one vertex at raw
`(0,0,0)` and two real rim vertices, and asserts:

- the stored triangle survives (`count == 1`),
- the camera vertex has clip `(0,0,0)` and `clip[3] == 1`,
- its screen position is still `(0, 0)`,
- the rim vertices keep their real positive `w`,
- `NitroPort_Gx3dBuildBatches` keeps all 3 vertices in one batch and the GPU
  position has `w > 0`, with NDC still the top-left corner.

The existing test reference (`NitroPort_Gx3dRasterize`) is unaffected.

## Verification

Inside the dev container, on the tree above with the DS matching gate running
in its own `/work/pokeheartgold` clone (not touched; no `bootstrap.py --clean`):

```bash
make test
#   exit 0: 330 Python tests, OK; 20 SDK suites, 0 failed

make port-tests
#   exit 0: gx3d 337 + 239 checks, 0 failures; nitrosystem 17,122 checks, 0 failed

python3 tools/hgss_game.py --target host32 --keep-going --out /work/slice-3d-pc -j4
#   1092/1092 compiled, 0 failed; 150/150 .s; linked (exit 0, no unresolved)

cd /work/slice-3d-pc/title-run
/work/slice-3d-pc/pokeheartgold-pc /work/final-heartgold3ds.pak \
  /work/final-hg-data/manifest.json --save /work/title-3d.sav --frames 5400 \
  --dump-3d 4800 --screenshots /work/title-3d-shots --screenshot-every 600
#   exit 0; 3d_frame_0004800.png sha256 7d7ea8f5... == the clean reference
#   /work/diagpc/3d_frame_0004800.png (byte for byte; no black fan, wing intact)

python3 tools/hgss_game.py --target 3ds --keep-going --out /work/slice-3d -j4
#   1092/1092 compiled, 0 failed; 150/150 .s; linked (exit 0, no unresolved)
#   packaged pokeheartgold.3dsx, 9,278,120 bytes,
#   sha256 3f4936616ff68e32413b6b2c2e3a1630b8c27cc9a5e0a36b079e1f5aa9fb7f00
#  3dsxdump rc 0 (CODE 2126, RODATA 112, DATA 13, BSS 4446 pages)
```

No hardware run was made, so the PICA's textured fan is still the owner's
check. The software reference and the 3DS build are what this slice proves.

## Divergence from `platinum.lock`

`3ds_port/` is no longer byte-identical. The upstream PR content is the one
`to_screen()` hunk above plus `test_camera_point_vertex`; it is recorded in
`docs/decisions.md` and `docs/wiki/3DS-Pitfalls.md`. No `platinum.lock` change
and no `bootstrap.py` run.
