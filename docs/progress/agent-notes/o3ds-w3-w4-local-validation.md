# W3/W4: live field reproduction and bounded geometry work avoidance

## Run 27 verdict

Hardware build **96b3131**, same frozen script/profile. Mean interval-end
samples 2400–4200: compositions **8.075 → 8.250/s**, drawer **1.575 → 1.900/s**,
render wall **419.935 → 285.720 ms/frame** versus run 26. This is not a Goldenrod
FPS breakthrough. At 5400, **5760 empty lines = 30 × 192** still accompany 30
committed behind regions. Audio ends **0 dropped of 47100**; HOME-close settles
and tears down cleanly. Later 20–21.5-compose samples have different geometry
and are not counted as the frozen Goldenrod result.

## W3 — the missing live-path case

Azahar RPC captures the real field's DISPCNT **0x00011f18**, BG controls
**9e01 / 0013 / 0117 / 0208**, BLDCNT **0**, and no circles. This enables
**BG3 in front of 3D**, in addition to the two BG1/BG2 GPU takes behind it.
The previous synthetic fixtures had only two enabled text BGs. Even with no
visible BG3 pixels, the strict all-taken gate could not admit this real scene.

Commit **a6f0375** proves a plain text BG transparent by reading its CURRENT
map and every referenced tile (each tile once). All texel indices must be zero;
tile *number* zero is not assumed empty. Both 4-bit and 8-bit formats, full map
sizes, VRAM wrapping and palette/flip/scroll independence follow the existing
text addressing. There is no new cached emptiness, dirty generation, allocation
or unhooked-writer assumption. Existing hashes remain conservative.

Only ordinary text with no artwork/parts/pictures, no windows/H-blank/circles
and the standard canvas can qualify. Proven-empty maps need no side-band pixel
walk; after existing GPU validation/commit, they also satisfy empty-software-plane
admission. Nonempty maps, dialogue writes, sprites and unsupported scenes retain
the software reference. Forced full renders remain the independent reference.
GPU slot selection and the region rule are unchanged.

Executable tests now include three BGs, a transparent front map, direct tile/map
writes, text appearing/disappearing, nonempty tile zero, 4/8-bit tiles, both live
and captured state and map-size variants. **1428 synthetic checks pass**. Optional
private capture replay adds **10 checks**: CPU planes/alpha and GPU quad/key
metadata match the forced renderer exactly, while the optimized call walks zero
scanlines and the full reference walks 192. Captures are never committed.

Serial Azahar candidate run through frame 5400, with the original frozen input:
at 5400 **208 behind regions, 416 BG takes, zero rejects, 0 CPU scanlines,
0 empty lines, 0 row copies**. The image shows the field and lower UI. This proves
the live-path work avoidance is reached; it is NOT an Old 3DS speed measurement.
Moving-path frames still have legitimate software work. No hardware gain is
claimed until a corresponding console run exists.

The serial baseline at frame 5400 has **57408 empty lines / 299 regions**,
exactly 192 per region. The candidate's zero is therefore a reproduced work
elimination, not an inference from unrelated scene counters.

### W3 — no full-VRAM hash for freshly resolved planes

An additional slice removes full-VRAM checks for the established flat path.
It does NOT reuse cached empty pixels or infer that a writer did nothing:
every call reruns current setup, map/tile keys, transparency proof, OBJ sort,
GPU validation and quad construction, then freshly fills/clears CPU planes.
Dialogue, sprites, effects, layouts and new buffers therefore fall back or
refresh from their actual current state. Forced draws and other scenes retain
the conservative hashing/reference path. No generation hooks are introduced.

A protected-unused-VRAM-page test faults on the old path's actual full-region
read, and passes on the fresh path, including a simultaneous visible GPU-tile
write and scrolling. It also guards against a full hash in `RememberSplit`.
Transition tests establish the fresh path before changing OAM, windows,
H-blank state, nomination, brightness/effects and buffers. Static fresh calls
now return 1 because they refresh metadata/planes, not an unchanged-memory 0.
This avoids the old one/two **864-KB** signature reads on those field calls;
BG-specific key reads remain. The two per-engine records gain one integer
each; no heap/linear allocation or framebuffer ownership change.

## W4 — exact matrix cost reduction

**bcad322:** `ensure_clip` uses a checked sparse projection product. The field
projection printed by the existing hardware logs has only six potentially
nonzero entries. A runtime shape guard selects **24 products rather than 64**;
all other matrices use the original general multiply. Signed 64-bit sums,
one fixed-point shift, saturation, aliasing and perspective/orthographic behavior
are preserved. No vertex/culling/clipper, float precision, ordering, shader or
geometry/display queue change.

**2013709:** texture-matrix commands and PUSH/STORE no longer dirty a clean
position×projection result. They cannot change position/projection/vector results;
their own matrix/stack operations still execute. POP/RESTORE and actual matrix
mutations retain invalidation, and an already-dirty result is never cleared.

**fe8e7d1:** the shipped 256/320 wide-view scale (and 192/240 tall scale)
uses an exact unsigned quotient/remainder 4/5 calculation rather than signed
64-bit multiplication/division for each matrix row. It handles INT32_MIN
without signed absolute-value overflow; fifths never hit a rounding half.
All other ratios keep the original 64-bit path. The shell explicitly requests
the 320-wide geometry view, so this is a real default-path operation, not a
scene-name guess or an approximation to projection math.

Actual translator fixture: **740216 checks pass**, also ASan/UBSan. It compares
20,000 matrices with an independent scalar reference, including every sparse
guard violation, both aliases, signs, saturation, perspective and orthographic
shapes; checks full-range/random/boundary view scaling against the original
64-bit formula and the actual wide/tall `ensure_clip` outputs; tests mutation/
invalidation across all four matrix modes and stack/load/
multiply/scale/translate commands. The invalidation extension fails against the
preceding source. A host-only 500,000-product microbenchmark gives roughly
**3.2 ms general vs 2.2 ms specialized**, with identical checksums. This is ONLY
the matrix kernel on the Mac, not 3DS geometry CPU time or a scene-rate claim.

## Validation procedure and remaining gates

- Native toolchain only: devkitPro/devkitARM, no Docker or new dependencies.
- `make test`: **462 tests**, no assertion failures, the **11 known native
  toolchain errors**, 13 skips. `check_patches.py`: **86/86**, 0 missing.
- Renderer and geometry standalone harnesses pass ASan/UBSan; log/capture replay
  paths are under `build/` and are private local data, not new goldens.
- Fresh-archive final HG/SS build identities/hashes and final emulator smoke are
  recorded in STATE once complete. No intermediate or dirty candidate is a
  hardware recommendation.
- Azahar must be launched via `open -W -n <Azahar.app> --args <binary>`: direct
  execution presents its terminal-launch warning and blocks before boot. RPC is
  enabled in the existing config. The probe has a 90-second boot deadline.
- Early probe attempts were INVALID for A/B: Qt can ignore TERM to ask for close
  confirmation, leaving a test-owned process alive. Those runs are not used as
  performance/visual acceptance evidence. The corrected probe refuses an existing
  emulator, records and stops its exact PID (KILL after bounded TERM), checks one
  shared SD/RPC session at a time and restores backed-up emulator files. Only
  `w3-serial-*` runs are used above. No owner emulator was running before testing.
- Emulator has no `dspfirm.cdc`, so audio testing is pending, not passed silently.
  Test process termination is not an APT HOME/sleep/close test.
- Full real goldens, host32/overlay/symbol gates, hardware/New 3DS smoke and true
  Old 3DS geometry CPU budget remain pending. No patch-stack/layout changes.

**Wave status:** these are tested W3/R3, fresh-path hash avoidance and W4
work-avoidance slices, not closure of entire waves. W3/R2 writer-generation
coverage remains deferred with the old hash fallback outside the freshly
resolved path; lower-screen/publish tuning is not declared done.
W4 still needs per-stage/on-core hardware attribution and the ≤16-ms target or
the plan's conditional small GPU feasibility prototype. W1 pacing and W2 battle
UI also remain open. Do not turn kernel operation counts or emulator FPS into
claims that the 30-visible-fps Old 3DS finish line has been met.
