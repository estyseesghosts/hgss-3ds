# S13: the NNS volume table, and the camera-point fan

Follow-up to S9's noise-register fix and S11's camera-point `w = 1` change.
The reported symptom did not go away after 1407, and the hardware's 3D fan was
grey instead of the reference's feathered wing.

## The audio: every note held until the next one

**Symptom.** A note was not a note and a fade: it was a sustained,
high-pitched crunch until the next note reused the channel. The whole mix
clipped.

**The trace.** A temporary trace in `NitroSound_Mix` logged every channel
restart, every natural end, and one snapshot a second of each running
channel's control word and per-channel peak. At the title (frame 1,967,808 of
a 5,400-frame run) 15 channels were running, 14 of them at `ctrl` volume
`0x7f` and shift 0 — **0 dB, full scale** — and their per-channel peaks were
19,000–32,767. In 90 s the mixer logged **4** natural ends; the later fixed
run logged **192**.

**Cause.** NitroSystem's `snd_player.c` converts its linear 0..127 player
volume to decibels through `_0210D750` and sends the sum to the ARM7 as the
external fader. On the DS, `_0210D750` is an **alias** of NitroSDK's
`SNDi_DecibelTable`: `lib/asm/nitro_sdk_data.s` emits an empty
`.rodata.alias_d750` section, and the DS link order puts it immediately before
`SND_util.o`'s `.rodata.snd_decibel` (`lib/asm/tools/nitro-section-renames/
nitro_link_order.json`). The port's default section order put the empty
section somewhere else: in the linked host binary `_0210D750` was at
`gFndExpHeapAllocatorFunc`, a table of function pointers. NNS read pointer
bytes as s16 decibels. At volume 127 the table read `0x0040` (64) four times,
so the fader was **+256** (25.6 dB) instead of 0; the ARM7 clamps the sum above
0 dB to 0 dB, so every channel played at full scale. The same positive fader
made `SND_ExChannelMain`'s release stop (`volume <= -723`) unreachable, so a
released note kept looping until the next key-on.

**Fix.** `hgss/compat/src/nns_volume_table.c` supplies the real 128-entry
table, the same values as `lib/NitroSDK/src/snd/SND_util.c`'s
`SNDi_DecibelTable` (0.1 dB from -72.3 dB at index 0 to 0 dB at index 127).
`tools/hgss_game.py` removes `.rodata.alias_d750` from the assembled SDK data
object for the port links (`objcopy --remove-section`) and adds the new object
to the program. After the link it checks that `_0210D750` is 256 bytes and
prints `[sound] _0210D750 -> nns_volume_table.c (256 bytes)`; a missing or
wrong-sized table fails the build.

The DS matching gate is untouched: it links the original object with the DS
link order, and the port's object step runs only in `hgss_game.py`.

**Verification (PC, same save and frames):**

| | buggy | fixed |
| --- | --- | --- |
| title RMS (stereo, 59–73 s) | 0.39–0.54 | 0.13–0.19 |
| samples at full scale | 4.5–14% | 0–0.02% |
| 50 ms windows above 0.4 RMS | 285 of 420 | 0 |
| 50 ms windows below 0.02 RMS | 11 | 12 |
| natural mixer ends in 90 s | 4 | 192 |
| start volumes (ctrl low byte/shift) | 127/0 (0 dB) | 41–103 with shifts |

`tests/test_hgss_game.py::NnsVolumeTableTests` keeps the port table equal to
the NitroSDK source (128 entries, monotonic, -32768..0) and asserts the port
programs link it and drop the right empty section.

## The 3D: why `w = 1` made a grey fan

S11 gave the camera-point vertex `clip[3] = 1` so the PICA would not divide by
zero. The stored vertex keeps screen `(0,0)` and the software rasteriser's
`inv_w = 0`; the GPU vertex becomes NDC `(-1, +1)` with `w = 1/4096`. The
software rasteriser interpolates a varying with weight `inv_w`, so that vertex
contributes **nothing**: the fan takes the rim vertices' texels (the red
feathers). The PICA's perspective-correct interpolation uses `1/w`; at
`1/(1/4096) = 4096` versus the rim's `1/1 ≈ 1`, the camera point dominates and
the whole fan takes its grey texel. That is the hardware's grey/black fan.

**Fix.** Mark the vertex (`NITRO_GX3D_VERTEX_CAMERA_POINT`) in
`to_screen()`, and in `put_vertex()` give the GPU a huge `w` (2^18) instead of
`1/4096`: its `1/w` is then negligible, matching the software's zero weight.
The position is unchanged in NDC (x and y scale with w), and in a Z-buffer
scene z scales with w so `z/w` (the stored depth) stays the same; a W-buffer
scene stores the DS W itself and needs no z change. The same path is in
`NitroPort_Gx3dBuildOpaqueVertices` for the classic renderer.

**Tests.** `test_gx3d.c::test_camera_point_vertex` now checks the flag, that
the camera vertex's GPU w is more than 1000× the rim's, and that `z/w` is
still -0.5. `make port-tests` (nitro): gx3d **340 + 242** checks, 0 failures.
The PC reference at title frame 4800 is byte-identical to the clean reference
(`sha256 7d7ea8f5…97ad8`), so only the GPU path changed.

**Not yet run on hardware:** the PICA must be checked on the title, with the
3D slider both at 2D and 3D. The fan should be the feathered wing, not grey.

**Local artifact.** 3DS build 1092/1092, 0 unresolved; `.3dsx` **9,278,472 B**
(`sha256 63d880e7a19d3a61218b471fca8b94a06b8866430025e1aed8b2b2508c729c03`),
`3dsxdump` rc 0 (CODE 2126, RODATA 112, DATA 13, BSS 4446 pages). Staged as
`sdmc:/3ds/pokeheartgold.3dsx`; the previous artifact
(`3f493661…b7f00`) is kept beside it as `pokeheartgold-3f493661.3dsx`.

## Files

- `hgss/compat/src/nns_volume_table.c` — the table.
- `tools/hgss_game.py` — removes the empty alias section, links the table,
  checks the linked size.
- `tests/test_hgss_game.py` — table/build tests.
- `3ds_port/nitro/include/nitro_port_gx3d.h`, `src/port_gx3d.c`,
  `src/port_gx3d_vertex.c`, `tests/test_gx3d.c` — the camera-point flag and
  GPU w.
- `docs/wiki/3DS-Pitfalls.md` — the symptom/cause/rule for both.
