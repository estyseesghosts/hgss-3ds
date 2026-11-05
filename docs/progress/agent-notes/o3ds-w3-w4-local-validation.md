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

Actual translator fixture: **740217 checks pass**, also ASan/UBSan. It compares
20,000 matrices with an independent scalar reference, including every sparse
guard violation, both aliases, signs, saturation, perspective and orthographic
shapes; checks full-range/random/boundary view scaling against the original
64-bit formula and the actual wide/tall `ensure_clip` outputs; tests mutation/
invalidation across all four matrix modes and stack/load/
multiply/scale/translate commands. The invalidation extension fails against the
preceding source. A host-only 500,000-product microbenchmark gives roughly
**3.2 ms general vs 2.2 ms specialized**, with identical checksums. This is ONLY
the matrix kernel on the Mac, not 3DS geometry CPU time or a scene-rate claim.

Test-only follow-up **7cf4761** forces genuinely saturated matrix outputs
(larger operands with all sums still inside signed 64-bit range), and asserts
that saturation actually occurred. The release game sources are unchanged.
ARM disassembly (`build/w3-w4-release-validation/scale-view-arm.txt`) confirms
the 4/5 branch uses UMULL/remainder/table lookup and returns without calling
`__aeabi_ldivmod`; only the general-ratio fallback calls that helper.

### Extended emulator checks

The pre-final-scaling build **1dc11dc** completed all four serial scenarios:

| Game/profile | Script | Last checked log frame | Visual observation |
| --- | --- | ---: | --- |
| SS / Old | frozen Goldenrod | 5400 | field and lower menu present; zero scanlines in static interval |
| SS / New | frozen Goldenrod | 5400 | field and lower menu present |
| HG / Old | boot-title | 6600 | Ho-oh/title, no camera-plane fan |
| HG / New | newgame-buttons, emulator-only save removed/restored | 19800 | Oak/dialogue/naming sequence reaches bedroom |

No assertions/exceptions were found in those logs. These are functional/visual
smokes, not full golden comparisons or hardware performance claims. The final
**b93c6ad** artifacts add the independently bit-exact view-scaling optimization;
their field/title smoke matrix completed separately under `build/w3-w4-release-*`:
SS Old/New Goldenrod to 5400, HG Old intro/title to 6600. Both final field
profiles show zero CPU scanlines/row copies in the static 5400 interval; their
field/lower UI and the title's Ho-oh were inspected. No logged assertions or
exceptions. Final Old-profile capture replay passes **1438 checks**, including
ASan/UBSan. Do not conflate the two binary identities or call screenshots with
different animation phases a pixel-exact golden comparison.

All final probe saves, script files and configuration were checked restored
byte-for-byte; no test-owned emulator remains running. No save round-trip,
DSP playback, HOME/sleep or actual hardware regression is claimed by these runs.

## Session issues and resolutions

| Issue encountered | Diagnosis and resolution |
| --- | --- |
| Previous enabled OBJ state blocked an empty frame after OBJ was disabled | Persistent sorted count survived the early return. `f481089` clears it; enabled-to-disabled transition tests cover live/snapshot paths. |
| Synthetic W3 success did not predict Goldenrod | Two-BG fixtures omitted enabled transparent front BG3. Captured registers plus map/tile/OAM data supplied the real topology; `a6f0375` proves current transparency and retains fallbacks. |
| Pixel-reference alpha mismatches on transitions | Empty over pixels intentionally retain alpha. Seed matching candidate/reference history, not a production alpha-clearing change. |
| Empty-plane optimization still scanned full VRAM | Fresh revalidation/refill (`e59fcac`) avoids asserting unchanged memory. A protected-unused-page test failed with SIGBUS before the fix and passes after, including active GPU writes. |
| Archive setup failure followed by hundreds of unresolved symbols | `DEVKITARM` was omitted. Export both devkit variables and rebuild fresh archives; no linker stand-ins or source workaround needed. |
| Potential stale vendored library despite edited source | Preserve old SDK outputs by renaming, or use entirely new build outputs. Final release HG/SS used new directories; sidecars and boot identity bind results to actual binaries. |
| Direct Azahar executable never booted | Terminal-launch warning blocked Qt. Process sampling showed a modal dialog; app-bundle launch via `open -W -n … --args …` resolved it. |
| Early emulator probes overlapped and contaminated shared SD/RPC evidence | TERM can leave Qt's close confirmation alive. Discard those probes; refuse existing emulators, record actual test-owned PID, bounded TERM/KILL and confirmed exit before restoration/relaunch. Corrected runs are serial. |
| Numerical fixture claimed saturation without producing any | Increase selected operands within safe 64-bit limits and assert saturation occurred (`7cf4761`). No game source or release binary change. |
| Native full-suite errors | 11 toolchain errors and 13 skips remain. Standalone actual-renderer/math fixtures provide bounded evidence, not a substitute for unavailable host32/SDK gates. |
| Emulator silence and forced process exit | Missing `dspfirm.cdc`; audio is not tested. Forced shutdown is not HOME/sleep/close acceptance. These remain explicit hardware gates. |
| Wiki timing schema and pacing interpretation were stale | Verify `NitroPort_OnFrame`: entry-to-entry `total_us`, prior `DrawFrame` state, residual `game_us`; no isolated game-CPU or visible-FPS claim. Run-27 ~3-ms samples despite a 60-Hz banner leave pacing unresolved. |

Durable procedures and proof boundaries are now in the wiki's
[Renderer-Validation](../../wiki/Renderer-Validation.md),
[Build-Reference](../../wiki/Build-Reference.md),
[Hardware-Testing](../../wiki/Hardware-Testing.md) and
[3DS-Pitfalls](../../wiki/3DS-Pitfalls.md). No emulator runner or private dump
was promoted into a tracked tool/fixture by this documentation update.

## Validation procedure and remaining gates

- Native toolchain only: devkitPro/devkitARM, no Docker or new dependencies.
- `make test`: **462 tests**, no assertion failures, the **11 known native
  toolchain errors**, 13 skips. `check_patches.py`: **86/86**, 0 missing.
- Renderer and geometry standalone harnesses pass ASan/UBSan; log/capture replay
  paths are under `build/` and are private local data, not new goldens.
- Fresh-output final HG/SS build **b93c6ad**: both **1092/1092 compiled, 0 failed**,
  clean links, fresh SDK archives, `validate_3dsx []`. Final emulator smoke and
  staging/full hashes are recorded in STATE. Only the versioned b93c6ad pair
  is the new hardware candidate, never an intermediate/dirty build.
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
