# W4 extrema classifier: local validation, hardware pending

Candidate `584305e` replaces eight-corner hidden-box classification with an
extrema calculation. Its immediate comparison baseline is `c613b97`, which
includes the separate offscreen sun-shadow fix. This is an experimental
candidate, not evidence that the W4 geometry time target has been reached.

## Local checks

- Fresh HeartGold and SoulSilver 3DS builds linked and packaged; both pass
  `validate_3dsx` with `[]`. `tools/check_patches.py`: 87/87.
- `HGSS_GX3D_TEST_CFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all'
  python -m unittest tests.test_gx3d_clip_matrix`: 1,086,876 checks, zero
  failures. The fixture includes 200,000 randomized/boundary comparisons to
  the eight-corner classifier and packed-stream trace checks. These are host
  correctness checks, not Old 3DS timing.
- SoulSilver Azahar reached the frozen Goldenrod field through frame 5400;
  the captured state passed the forced-renderer fixture (67,421 checks, zero
  failures, plus flat-publish tests). A separate Azahar run reached wild battle
  and captured the battle and lower-screen item menu. HeartGold booted into
  its saved field and reached frame 1200. These are visual smokes, not matched
  scene-image goldens, audio, HOME, or hardware tests.
- `make test` did not pass: 471 tests, 11 errors and 18 skips; the errors
  include unavailable `gcc-10`. `make port-tests` failed at the pak link with
  the native macOS linker targeting armv4t. Host32, overlay and symbol gates
  remain pending; do not infer they passed from 3DS packaging.

## Artifact identity

| Build | Local path | SHA-256 |
| --- | --- | --- |
| Candidate HG | `build/w4-extrema-hg/pokeheartgold.3dsx` | `74c6626c52eaacafe981d1be8ad8cc7d1975361eda46a789b6710d0489b3edbe` |
| Candidate SS | `build/w4-extrema-ss/pokesoulsilver.3dsx` | `c6470c5212c79a83b7fda90804a681ee1c28f06b8115de828e5c5bbcf283d8eb` |
| Baseline HG | `build/w4-shadow-hg/pokeheartgold.3dsx` | `34b8ec30be2db788fd39d02eb7f77fd20186b06c10079be14eb9ed3ca596250f` |
| Baseline SS | `build/w4-shadow-ss/pokesoulsilver.3dsx` | `21420ea58bc3cbbaafeaed1e39d3660d8987fa5aaa720e92b0aee2a7f3755c1c` |

Each output directory contains its build-info sidecar. The Azahar field and
battle captures are under `build/w4-extrema-field` and
`build/w4-extrema-battle`, and the HG field capture under
`build/w4-extrema-hg-boot`; they are local, not committed goldens.

## Remaining gates

### Existing screenshot comparison (2026-11-05)

Compared the saved baseline and candidate 400x480 Azahar screenshots as RGB
pixels. SoulSilver's lower screens are identical; the upper screens differ at
2,248 pixels (all exceed 8/255), with visibly different NPC positions. HeartGold's
lower screens are identical, while all 96,000 upper pixels differ by more than
8/255: one capture includes a Route 30 banner and a different fade state. The
captures target the same log frames but are not synchronized scene goldens, so
neither comparison establishes 3D equivalence. Retain both captures and repeat
with a deterministic scene/capture boundary; do not bless these differences.

Re-ran the geometry fixture with ASan/UBSan: 1,086,876 checks, zero failures;
patch verification remains 87/87. These do not replace the scene or device gates.

### Additional Azahar replays

Serial SoulSilver candidate and baseline replays reached log frame 1200 in
`build/w4-recheck-azahar` and `build/w4-recheck-baseline`. Both show Goldenrod;
the captures differ by 349 upper pixels and 23,555 lower pixels, with the lower
screen in different UI phases. They are functional smokes, not matched goldens.
The emulator save and log were verified restored after both runs.

The forced-renderer fixture on the candidate's frame-1200 capture reported two
failures in its zero-scanline and cumulative-scanline assertions. Its pixel,
alpha and quad comparisons did not report failures. This fixture expects the
static field fast path; this capture is from a dynamic interval. The test run
failed and is not a substitute for the prior frame-5400 static-field pass.

HeartGold candidate and baseline also completed serial idle-title replays,
with the emulator-only save temporarily removed and restored. Candidate reached
frame 6600; baseline reached 5400. Both display Ho-Oh at frame 5400 without an
obvious missing model in these captures, and neither log contains an assertion,
exception or fatal message. Animation phases differ visibly: 2,373 upper and
25,123 lower pixels exceed 8/255, so this is not a matched visual golden. See
`build/w4-extrema-title-recheck` and `build/w4-baseline-title-recheck`.
The HeartGold save, script, log and Azahar config were verified restored; no
Azahar process remained. Audio and HOME/close remain untested.

Compare matching 3D scenes and relevant shadows against the baseline, including
interiors, Ho-oh, trainer battles and Poképics. Obtain matched Old 3DS runs
with frozen inputs and distinct moving/idle intervals, including geometry-stage
timing, display pacing, audio and memory; wall time is not CPU time. Run New
3DS, save, HOME/sleep/close and remaining build gates. Do not claim a speedup,
sub-16-ms geometry, or W4 completion without that evidence.
