# W2: battle dialogue mirror — veto fix and GPU mechanism hunt

## Symptom

Wild-battle message text renders H-mirrored (every glyph flipped, order
preserved) whenever the dialogue BG is drawn as a GPU compositor quad.
Status boxes, sprites and the bottom-screen menu are unaffected. Seen on
Azahar and **confirmed by the owner on Old 3DS hardware** on the pre-fix
`b93c6ad` build, so this is a real port bug, not an emulator artifact.

## Root cause (verified)

`NitroPort_NominateGpuBgs` vetoed battles with
`NitroPort_OverlayLoadedNamed("battle")`, but no overlay named `battle`
(or `overlay5`) exists — `build/upstream/main.lsf` has 129 overlays
(`OVY_N` plus named apps like `field`). The name check is dead code, so
nomination was unconditional and battles took quads. Separately, no
overlay loads at battle start at all (field code already has OVY_12
resident, which also hosts the battle template), so overlay identity
cannot work as a battle signal — a game-state flag is required.

## Fix committed (`4ae58a6`, patch 1910, patch stack now 87/87)

- `Battle_Init` / `Battle_Exit` in `build/upstream/src/launch_application.c`
  set/clear `NitroPort_SetBattleActive` under `#ifdef SDK_PORT_3DS`
  (pure additions; matching build byte-identical). Every battle type runs
  through `gOverlayTemplate_Battle`, so wild, trainer, link, Safari, Pal
  Park, Bug Contest and Frontier battles are covered; `Battle_Exit` is the
  template's single terminal call per `OverlayManager_Run`.
- `NitroPort_NominateGpuBgs` returns 0 while the flag is set; the field
  keeps `0xE`.
- Tests: new `tests/test_wide_battle.py` (9 checks: default 0xE, battle 0,
  cleared 0xE); updated `GpuBgNominateTests` in `tests/test_hgss_game.py`,
  which had asserted the dead name check.

## Verification done

- Azahar grass-save repro (SS `b93c6ad` vs veto build, same save/encounter):
  entry ("Go! GADDAFI!") and command ("What will GADDAFI do?") both read
  correctly with the veto; pre-fix command text mirrors.
- Pre-fix log: battle intervals take ~380 quads/frame, `region 63–74/0`,
  zero rejects. Veto run: battle intervals take 0. Post-battle field takes
  resume (`taken 94, region 0/47`), proving `Battle_Exit` clears the flag.
- Native suite: 463 tests, no failures (11 known toolchain errors,
  13 skips). Both clean veto builds (`build/veto-ss`, `build/veto-hg`)
  compile 1092/1092 and pass `validate_3dsx []`; **not staged**.
- Hardware acceptance of the veto (photo A/B) is still pending.

## GPU mechanism hunt (open — veto is a workaround, not the fix)

For the eventual exact GPU take, what is established:

- The take/suppress boundary is the blend flag: entry runs with
  `BLDCNT=0x3f40` (blend reject → software, correct); command runs with
  `BLDCNT=0x0000` (take fires → mirror). Same DISPCNT/BGCNT/scrolls either
  side; only `BLDCNT` and VRAM content differ between phases.
- Host reproduction (throwaway harness linking the real renderer, wide
  layout and upscale code; **not** committed) shows the full take path
  correct for command-phase VRAM: `Render2DBgPicture` decodes the message
  correctly, `Double2xTiled16` output untile-verified, quad UVs
  `u=(-0.125,1.125)` increase left-to-right, compositor `quad4`/combiner
  code read clean, no hflip bits in the map.
- Sharpest exhibit: one transition frame shows the top line mirrored and
  the bottom line correct in the SAME box (`build/battle-w2-evidence/`,
  ignored `build/` dir — re-capture per the procedure in
  [Renderer-Validation](../../wiki/Renderer-Validation.md) if needed).
  No global theory (UV direction, wrap mode, whole-texture staleness,
  PICA state set once per frame) explains a per-row split, so the
  divergence must be live-only: texture bytes as uploaded, or state the
  host harness does not replicate (live 3D image, line regs, OAM anim
  phase — the harness zeroes/stubs these).
- Ruled out: flip-bit maps (absent), stale textures via key miss
  (`BgKey` hashes tile *pixels* and full-VRAM signatures gate reuse),
  UV direction (increasing by construction), PICA wrap/clamp mixup
  (field quads share the path and are correct).
- Unresolved puzzles, do not cite as evidence yet: BG VRAM md5-identical
  across "Go!" and command phases (message switching does not go through
  BG VRAM deltas — OAM/ObjVram/LcdcVram were never captured and may own
  it); a forced-software host render once showed entry text from a state
  whose BG picture decoded the command text (likely a harness artifact
  from approximated OAM, needs a full-state rerun).
- Recommended next step: temp instrumented build (revert before commit)
  logging per-take `(bg,W/H,wrap,key,takeHofs/takeVofs,nQuads,quad UVs)`
  plus FNV of slot pictures and uploaded textures, against a same-run
  full VRAM capture recomputed on host. If host and device agree
  everywhere, the remainder is PICA/GPU-side and Old 3DS hardware is the
  arbiter.

## What went wrong this session, and the fix for each

| Issue | Resolution |
| --- | --- |
| `~/maple/cpp-guide.md` does not exist; the actual guide targets a different C++ repo. | Stated explicitly and followed this repo's own conventions instead (local comment style, atomics pattern, `SDK_PORT_3DS` guard style, fixture tests). Never apply another repo's style guide blindly. |
| An existing test asserted the dead veto (`test_nominate_has_no_scene_vetoes`). | Tests can encode bugs. Updated it with the fix (`test_nominate_has_battle_veto_only`) instead of working around it. |
| First host picture harness decoded garbage (all-black layers). | It never called `NitroPort_Render2DTakeState`, so snapshot reads hit zeroed state and address bases were wrong. Always TakeState before snapshot reads. |
| OAM approximated from a different phase made one take comparison meaningless (entry `nGpuQuads=0` artifact). | Capture the FULL state (io+bg+obj+oam+pltt+LcdcVram) per frame; never mix phases. ObjVram/LcdcVram were never captured at all — the next hunt must include them. |
| Screenshot/VRAM captures skew several seconds on auto-advancing text; transition frames then mismatch their VRAM. | Pair captures only on static phases (input-waiting screens), or capture VRAM first and treat screenshots as approximate. |
| A-mash battle scripts hit the known tall-grass comm-error screen. | Pre-existing save-flow assert path (memory flat across the run, unrelated code); left out of scope, recorded here instead of chased. |
| Azahar executable launched directly blocks at a terminal warning; Qt survives TERM. | Launch the app bundle via `open -W -n ... --args`; bounded TERM then KILL by recorded PID; never broad `pkill`; refuse to start with another session live. |
| Emulator SD script files (`port.ini`, walk scripts) left in place would replay on the next boot. | Removed after scripted runs; verified absence. Saves (`soulsilver.sav` city `ecd65e77…`, grass `4e7aff60…`) preserved with hashes in `~/hgss-private/save-backups/`. |
