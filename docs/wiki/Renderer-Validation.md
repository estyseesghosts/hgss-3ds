# Renderer validation: native proofs and serial Azahar runs

This is the reusable procedure learned during the 2026-11-04 W3/W4 work.
[Session evidence](../progress/agent-notes/o3ds-w3-w4-local-validation.md) records
the actual results; [STATE](../progress/STATE.md) owns candidate hashes and open
gates. See [Build-Reference](Build-Reference.md) for native build/test commands
and [Hardware-Testing](Hardware-Testing.md) for console acceptance.

## Prove behavior, not just a shortcut's existence

1. Capture the live topology before constructing a synthetic test. Goldenrod
   enables transparent BG3 in front of 3D as well as BG1/BG2 behind it; a two-BG
   fixture missed the real admission blocker. Registers alone are insufficient:
   replay current map/tile/palette/OAM bytes too.
2. Compare the actual optimized renderer with forced `NitroPort_Render2DSplit`.
   Keep the forced call independent: it must not take the shortcut under test.
   Compare under/over planes, alpha history, BG keys and quad metadata, and assert
   scanline counts separately. An emulator screenshot is a functional check,
   not that pixel-exact reference.
3. Seed identical previous buffers for both calls. An empty over pixel leaves
   alpha untouched; comparing different histories produces a false regression.
   Do not change renderer behavior merely to make the reference look initialized.
4. Establish the fast path **before** testing invalidations: text appears and
   disappears, tile zero becomes nonzero, GPU tiles change, scroll changes,
   OBJ is enabled/disabled, windows/H-blank/effects change, and buffers change.
   Include live and snapshot state, 4/8-bit tiles and larger maps.
5. Prove removed memory reads directly. The unused protected-VRAM-page fixture
   faults when either the reuse check or `RememberSplit` scans the whole region.
   It passes while readable active GPU tiles change. This is stronger than a
   source-string assertion; it is a test-only guard, not a hardware memory policy.

The flat path **freshly resolves**, it does not trust last frame's emptiness.
Each call rebuilds current keys, transparency, OBJ state and GPU admission and
refills the CPU planes, returning 1. Conservative hashing remains outside it;
no complete dirty-writer coverage is claimed.

## Exact geometry optimizations need adversarial math tests

`tests/fixtures/gx3d_clip_matrix.c` includes the real translator and compares
with an independent scalar formula. Cover each sparse-shape rejection, aliasing,
negative rounding, INT32_MIN/MAX, actual saturation, all matrix modes and lazy
invalidation. Random small operands did **not** produce saturation; the final
test safely enlarges operands and asserts that saturation actually occurred.
For view scaling, compare the 4/5 path and general fallback with the original
64-bit expression, then check the actual `ensure_clip` output.

The W4 changes preserve fixed-point shifts/saturation and retain general
fallbacks: 24 rather than 64 projection products, no clip invalidation for
texture/PUSH/STORE commands, and exact 4/5 scaling without common-path 64-bit
division. ARM disassembly confirms the division helper is only in the general
scale fallback. Host microbenchmarks and operation counts are not Old 3DS
scene-time evidence; clipping/order/shader changes were not part of these fixes.

## Battle take validation needs phase-matched full state

The W2 mirror reproduced only when the dialogue BG was actually taken, and
the take fires only in some phases, so the harness must reproduce the phase,
not just the bytes:

1. Record `BLDCNT`/brightness with the registers. Blend active at battle
   entry rejects takes (software draws, correctly); blend off at the command
   prompt fires takes. Comparing a blended frame with an unblended one
   compares two different renderers.
2. Capture the **full** state per frame: io, BG VRAM, OBJ VRAM, OAM, palettes
   **and LCDC VRAM**. An entry capture reused another phase's OAM made one
   take comparison meaningless; ObjVram/LcdcVram were never captured at all.
   Never mix phases across files.
3. Call `TakeState` before any snapshot-layout decode on host, or sizes and
   bases come out zeroed. A forced-software `Render2DSplit` needs the same
   setup as the take path it is compared against (width/origin/clip/
   anchorWindows from the real layout).
4. Prefer input-waiting screens (command menu) for screenshot↔VRAM pairs;
   auto-playing entry text skews across a multi-second RPC capture.
5. Keep the veto as the correctness reference while the quad path is under
   investigation: same save, same encounter, veto build vs take build. A
   transition frame showing a mirrored line beside a correct line is worth
   more than ten full-box screenshots — it rules out every global theory
   (UV direction, wrap, stale texture, per-frame PICA state) at once.

## Serial emulator procedure (macOS)

The tested installation is Azahar 2123.3 at
`/Users/terezi/Local Documents/azahar-2123.3-macos-universal/`.
Its `scripting/AzaharRPC.py` helper talks over loopback UDP port **45987**.
These are local setup details, not shipped project dependencies.
The session runner is ignored `build/emulator_probe.py`, not a supported tool
or a tracked reproducibility dependency. Recreate its safeguards if replacing it.

1. Refuse to start while **any other emulator session is running**. One shared
   SD tree and RPC endpoint means tests must be serial, including Old/New runs.
2. Create a unique output directory. Record exact binary path, SHA-256, matching
   ELF, game/profile, input script, target log frame and milestone captures.
   Resolve memory symbols from that ELF, not stale addresses from another build.
3. Back up configuration and every file to be touched, recording absence too:
   save, `port.ini`, scripts and existing game log. Freeze the benchmark save and
   input/profile bytes. A no-save scenario removes only the backed-up emulator
   copy, never the owner's original save. Truncate a log in place, not by
   unlinking a file a running service may still hold open.
4. Launch the app bundle, not its executable directly:

   ```bash
   open -W -n '/Users/terezi/Local Documents/azahar-2123.3-macos-universal/Azahar.app' \
       --args '/absolute/path/to/candidate.3dsx'
   ```

   Direct executable launch blocked at a terminal-launch warning before boot.
   A 90-second boot-banner deadline caught this instead of waiting indefinitely.
5. Record the **actual emulator PID**, not just `open`'s PID. Require the expected
   boot identity/profile. Read game logs and capture through RPC; no desktop
   clicking automation was used. Screenshots taken after a log threshold are
   not guaranteed to depict that exact game frame or animation phase.
6. On completion/failure, terminate only the test-owned emulator. Qt may retain
   it after TERM to ask for close confirmation. Use bounded TERM, then KILL if
   necessary, and verify exit **before** restoration or another launch. Never
   use broad `pkill` or mistake `open` exiting for emulator cleanup.
7. Save evidence, restore all backed-up bytes and originally absent files, and
   verify configuration/saves/scripts match. Check no test emulator remains.
   Keep private dumps/screenshots/logs in ignored outputs, not the repository.

Early probes overlapped because TERM had not stopped Qt; those runs were
discarded for acceptance. Only the corrected serial runs support session claims.
The final save/script/config restoration was verified byte-for-byte.

## Evidence boundaries

- Native fixtures: exact tested math/pixels/work avoidance, not full game ABI.
- Azahar: reached paths, visual smoke and exceptions, not console performance.
- Final **b93c6ad**: SS Old/New field to 5400; HG Old title to 6600.
- Earlier **1dc11dc**: also HG New new-game to bedroom at 19800. Keep identities
  separate even though the final added scaling is independently bit-exact.
- No DSP firmware was installed: audio was silent and remains untested.
- Forced test termination is not save round-trip, HOME/sleep/resume or clean APT
  close acceptance. Full golden and hardware lifecycle comparisons remain open.
- Reduced scanlines/hash reads and exact integer optimizations do not close W3/W4
  or prove normal game speed. Preserve the matched hardware benchmark and its
  scene boundaries; measure the ≤16-ms geometry target on hardware.
- The battle veto is verified on Azahar against the pre-fix build on the same
  save/encounter (entry + command correct, 0 battle takes, field takes resume).
  The exact quad-path defect is open with a recorded procedure above; the
  Old 3DS photo A/B is the arbiter. See the [battle validation notes](../progress/agent-notes/w2-battle-ui-veto-and-quad-hunt.md).
