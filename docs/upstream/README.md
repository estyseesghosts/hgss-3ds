# Upstream patches

These are changes this repository needs in a third-party project. They are
kept here so a monorepo checkout still shows the exact divergence from the
pinned commit, and so the fix can be sent upstream as a small PR.

| Patch | Base | Applies to | Status |
| --- | --- | --- | --- |
| `platinum-camera-point.patch` | `platinum.lock` (`359fbec8…`) | [GabeSud0/pokeplatinum-3ds](https://github.com/GabeSud0/pokeplatinum-3ds) `nitro/` | local; PR candidate "gx3d: drop the zero-area camera-plane triangle" |

Apply from the Platinum repository root:

```sh
patch -p1 < /path/to/pokeheartgold-3ds/docs/upstream/platinum-camera-point.patch
```

The patch touches `nitro/src/port_gx3d.c` (the `camera_plane()` check and the
polygon drop in `stage_triangle()`) and `nitro/tests/test_gx3d.c`
(`test_camera_plane_triangle`), and is the same content as the `3ds_port/`
divergence described in
[`3ds_port/LOCAL-CHANGES.md`](../../3ds_port/LOCAL-CHANGES.md). If the upstream
PR lands, remove the local patch from `3ds_port/` and this file.
