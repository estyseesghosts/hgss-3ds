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

**Follow-up: the mix was still 6 dB hot.** The user reported that the notes
were right but the mix still sounded clipped, and that turning the console's
volume down did not remove it — so the damage was in the samples. A retail
title captured from melonDS, calibrated with a full-scale tone through the
same macOS output path, peaks at ≈0.42 of full scale (RMS 0.071); the port's
title peaks at 1.0 (RMS 0.139). `NitroPort_SoundRender` now trims 6 dB
(`>> 1`) at the shared output stage: the title is **0.067-0.088 RMS, peak
0.5, 0% of samples clipped**, matching the DS headroom. PSG duty 7 is also
fixed to GBATEK's 0% (silence) instead of the 100% DC level the mixer used to
emit; the sound-chip suite now checks it (52 checks).

## The 3D: the fan is a zero-area triangle

S11 gave the camera-plane vertex `clip[3] = 1` so the PICA would not divide by
zero, and S13 first tried a negligible `1/w`. Both still showed the fan on
hardware, and the user pointed out that the PC reference itself was deformed.
That was the key: the software renderer was drawing a fan the DS never draws.

A real DS title capture (melonDS 1.1, retail ROM, DS BIOS from the user's
Delta) shows Ho-oh intact with both wings. Reconstructing the transform shows
why: the zero vertex is on the **camera plane**, `(0,0,0,0)`. Both edges from
it to a rim vertex project to that rim vertex's screen point, so the triangle
has **zero screen area**; the DS's clipper stores no polygon for it. The
software rasteriser stored its screen `(0,0)` literally and gave the triangle
area, fanning the left wing out of the corner. Dropping exactly the
`(0,0,0,0)` triangles in `stage_triangle()` renders a complete Ho-oh in the PC
renderer (both wings, body, tail, no fan). The S10 workaround had dropped
every `w <= 0` triangle, which also removed real near-plane-crossing wing
geometry and left the gap.

**Fix.** `3ds_port/nitro/src/port_gx3d.c`: `camera_plane()` detects exact
`(0,0,0,0)`; `stage_triangle()` drops the polygon before staging. The
`w`-hack vertex changes in `port_gx3d_vertex.c` / the header are reverted, and
`to_screen()` is Platinum's again. The synthetic test
`test_camera_plane_triangle` checks `scene->count == 0`.

**Not yet run on hardware:** the PICA must be checked on the title, with the
3D slider both at 2D and 3D. The bird should be intact, no fan and no gap.

**Local artifact.** 3DS build 1092/1092, 0 unresolved; `.3dsx` **9,278,600 B**
(`sha256 4cecadd071b4aa4557ad651c0982255132672ae90b86c0f74ce6679c25d508ab`),
`3dsxdump` rc 0 (CODE 2126, RODATA 112, DATA 13, BSS 4446 pages). Staged as
`sdmc:/3ds/pokeheartgold.3dsx`; the audio-only build (`63d880e7…`) and the
previous `3f493661…` are kept beside it for rollback.

## Files

- `hgss/compat/src/nns_volume_table.c` — the table.
- `tools/hgss_game.py` — removes the empty alias section, links the table,
  checks the linked size.
- `tests/test_hgss_game.py` — table/build tests.
- `3ds_port/nitro/src/port_gx3d.c`, `tests/test_gx3d.c` — the camera-plane
  drop.
- `docs/wiki/3DS-Pitfalls.md` — the symptom/cause/rule for both.
