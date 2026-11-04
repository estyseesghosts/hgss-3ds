# Current state

Last verified: **2026-11-03 (session 19: Old 3DS performance waves W0-W2)**, natively on macOS
(`DEVKITPRO=/opt/devkitpro`, venv `/Users/terezi/hgss/bin/python`), with the tree at **86 patches**
(`NitroSDK` 16, `NitroSystem` 4, `pokeheartgold` 64). The last container run was 2026-11-02
(session 18). Everything below that says "verified" was run on the tree on the date given, and the
DS-side gate passed at 78 (HG and SS). If you change the patch stack, rerun the commands and update
this page; do not trust it blindly.

**One line:** Phase 2's exit criterion is met — the PC program links with **0 unresolved**, the overlay
self-check passes on the real binary (129/0), the symbol cross-check reports **0 missing**, and the 3DS
program compiles 1092/1092, builds both archives and packages `pokeheartgold.3dsx` (and, for
SoulSilver, `pokesoulsilver.3dsx`). Phase 3's data
pipeline is complete: verify/extract/pack, the boot-time pak check and its tests, and the SD layout doc.
**Both halves of the Phase 1 DS gate pass at 78 patches**; both matching builds reproduce the retail
ROMs byte for byte (HG `4fcded0e…b5a37`, SS `f8dc38ea…e582`). Patches 1407, 1904-1907, 0304 and 0605 are
`SDK_PORT_3DS`-only (`#else` text unchanged), and the gate is the evidence.

Session 15 fixed the save CRC table (1906) and migrates legacy substruct CRCs (1907); a legacy field save now reaches the wild battle on the PC. Session 18 implements **Phase A** of [3DS-Remaster-Audit](../wiki/3DS-Remaster-Audit.md): the HGSS shell now compiles `port_compose_pica.c`, presents both screens at 400x240/320x240 through the Platinum compositor, draws the 3D at native resolution, maps touch 1.25x, and has no classic 1:1 fallback on either console. Hardware validation on a real Old 3DS/New 3DS is the remaining check.

## Phases

| Phase | State | Evidence |
| --- | --- | --- |
| 0 Baseline | **Done**, open items | Matching HG/SS ROM SHA1s in the container. Never done: golden captures, fixture saves, the determinism run, a native-macOS control. [phase-0.md](phase-0.md) |
| 1 SDK patch stack | **Done** | 86 patches in the stack (the DS gate last ran at 78; 1904-1909 are port-only); the last full `make test` was 355 Python tests + 20 SDK suites, plus session 15's save tests; `make port-tests` green; both archives build; the DS gate at 78 reproduces both retail ROMs. [phase-1.md](phase-1.md) |
| 2 Game code under GCC | **Done for the exit criterion** | **0 unresolved** on host32 and 3DS, for HeartGold and SoulSilver; `--check-overlays` 129/0; `symbol_crosscheck.py` missing 0; `pokeheartgold` and `pokesoulsilver` programs exist. [phase-2.md](phase-2.md) |
| 3 Data pipeline | **Done** | `builder/verify_rom.py`, `extract_rom.py`, `pack.py`; boot check with clear errors; `docs/wiki/SD-Card-Layout.md`; both retail dumps verified, extracted and packed; the HG pack opens in the PC program and the SS program boots to its main menu. [agent-notes/p2-session6.md](agent-notes/p2-session6.md) |
| 4 Boot to Oak | **In progress** | PC build boots cold → logo → movie → title → main menu → Oak's speech → naming → first field frames. On a New 3DS the same sequence reaches the bedroom/field with 2D, audio and the PICA 3D layer. Goldens, save round-trip, sanitizer and PR drafts open. [phase-4.md](phase-4.md) · [phase-5.md](phase-5.md) |
| 5 3DS shell | **Phase A remaster in progress; hardware run pending** | The shell now uses Platinum's compositor, drawer threads and V-blank presentation at 400x240/320x240; 3D at native resolution; touch maps 1.25x; no classic fallback. Old 3DS uses `smooth3d = 0`. Builds and tests pass; the real-hardware run (Old and New 3DS) is the next owner check. Earlier shell work (60 Hz cap, NNS volume table, NDSP cache flush/native rate, camera-plane drop, battle Poképics 0304/0605, close-hang fix) remains. **Old 3DS performance plan (2026-11-03, plan only):** [agent-notes/s19-o3ds-performance-plan.md](agent-notes/s19-o3ds-performance-plan.md). |
| 6-11 | Not started | Graphics gaps, sound streams, gameplay, IR, wireless, GPU 2D. See [../wiki/Architecture.md](../wiki/Architecture.md). |

## Verified today

Run from the repo root inside the container. `devcontainer exec --workspace-folder . bash -c 'cd
/workspaces/pokeheartgold-3ds && <command>'` runs any of them from the Mac.

| Check | Command | Result |
| --- | --- | --- |
| Patch stack applies from scratch | `python3 tools/bootstrap.py --clean` | last clean run: 82; 1906/1907 were applied and built this session, clean bootstrap not re-run |
| Host tests | `make test` | last full run **exit 0**; **355 Python tests** + **20 SDK suites**; session 15 targeted: 4 save-CRC + 18 savetool + gcc_fixes |
| Vendored port tests | `make port-tests` | **exit 0**: pak 120, the whole nitro sequence (sound chip 81), nitrosystem **17,122 checks, 0 failed**; gx3d **327 + 229** |
| Port sources vs HGSS headers | `python3 tools/port_compile_survey.py` | last full run 46/46; `port_gx3d.c`'s camera-plane drop compiles in the port build and `make port-tests` |
| Game compile (host32) | `python3 tools/hgss_game.py --target host32 --keep-going --out /work/slice-3d-pc -j4` | **1092/1092 compiled, 0 failed**; 150/150 `.s`; **linked, 0 unresolved** |
| Game compile (host32, SS) | `python3 tools/hgss_game.py --game soulsilver --target host32 --keep-going --out /work/ss-pc -j8` | **1092/1092 compiled, 0 failed**; **linked, 0 unresolved**; `pokesoulsilver-pc` boots the SS intro, title and main menu |
| Overlay self-check | `build/pokeheartgold-pc --check-overlays` | **exit 0**; `[check] 129 overlays (2 with static initializers), 0 failed` |
| Symbol cross-check | `python3 tools/symbol_crosscheck.py --pc build/pokeheartgold-pc --reference /work/pokeheartgold.ref/build/heartgold.us/main.elf` | matched 21,407 · allowlisted 3,328 · **missing 0** |
| Settings open/close (PC) | scripted new game → bedroom → mother → X → OPTIONS → QUIT | **returns to the field at frame 35032**; two further cycles pass. Before patch 1904 it aborted at 35028 in `LoadBgVramScr` (`src=0x02C002C0`) |
| Raw DS addresses | `python3 -m unittest tests.test_hw_addresses` | **7 tests, OK**; 0 raw `0x02000000`-`0x07FFFFFF` pointer literals in `src/` (20 before patch 1905, including the battle read and the background copy) |
| Game compile (3DS) | `tools/hgss_game.py --target 3ds --keep-going -j 10 --out build/game-3ds` (native macOS) | **1092/1092, 0 failed**; **linked, 0 unresolved**; `pokeheartgold.3dsx` **5,775,356 B** (`sha256 7751bd1a…bfa5`); `validate_3dsx` **[]**; image **29.77 MiB** |
| Game compile (3DS, SS) | `tools/hgss_game.py --game soulsilver --target 3ds --keep-going -j 10 --out build/game-3ds-ss` (native macOS) | **1092/1092, 0 failed**; **linked, 0 unresolved**; `pokesoulsilver.3dsx` **5,775,412 B** (`sha256 fbed84ee…e998`); `validate_3dsx` **[]** |
| Battle Poképics (PC) | owner's `heartgold.sav` + the zig-zag input, `--frames 14000` | both Pokémon render; each battle scene is **720 tris (720 opaque), lost: 0 clipped**; before 0304/0605, `0 vtx` and then `360 clipped` |
| Close from HOME (3DS) | HOME menu → Close | fixed in the shell (`[quit]` steps); the console run is the owner's check |
| Patch stack complete | `python3 tools/check_patches.py` | **86/86 applied, 0 missing**; fails the build if any patch's `git am` subject is absent from `build/upstream` |
| 3DS archives | same command | `libnitro.a` + `libnitrosystem.a`, 0 errors |
| 3DS PICA/symbols | `arm-none-eabi-nm /work/slice-3d/pokeheartgold-3ds.elf \| grep -E 'NitroPort_Pica\|Gx3dThread'` | `T` for `NitroPort_PicaInit/Render/Exit`, `NitroPort_Gx3dThreadMain/Flush/ConvertPending`, and a strong `T NitroPort_Gx3dScenePublished`; the HGSS drop symbol is gone |
| Data pipeline (real HG dump) | `builder/{verify_rom,extract_rom,pack}.py` | 513 files / 125,549,829 B extracted; 125,556,080 B pack; opens in `pokeheartgold-pc`; wrong build ID/version/corrupt/SHA-1 give the mapped messages and exit 2 |
| Data pipeline (real SS dump) | `scripts/prepare_game.py /work/soulsilver.us.nds` | `f8dc38ea…` verified; 513 files / 125,549,877 B extracted; 125,556,128 B pack with build ID `soulsilver-nitrofs-1`; the SS program boots its intro/title/main menu with it |
| Save layout | `make layout` | last full probe: 0 differences in 804 values; layout patches did not change |
| DS-side gate (HG) | `tools/matching-build.sh /work/pokeheartgold HEARTGOLD` | **PASSED at 78**: `build/heartgold.us/pokeheartgold.us.nds` SHA1 `4fcded0e2713dc03929845de631d0932ea2b5a37` (retail), `sha1sum -c` OK, exit 0 |
| DS-side gate (SS) | same, `SOULSILVER` | **PASSED at 78**: `build/soulsilver.us/pokesoulsilver.us.nds` SHA1 `f8dc38ea20c17541a43b58c5e6d18c1732c7e582` (retail), `sha1sum -c` OK, exit 0 |

### Vendored suites: the full matrix

`make port-tests` runs, in order: `pak`, the whole `nitro` subproject, then `nitrosystem`. The
session-6 final run (72 patches): pak 120, `test_nitro` 702, `test_fs_pak` 120, threads 68, arm7 **676**,
sound_chip 51, sound_sequence 12, render 2071, gx3d 326 (+228), plumbing 231, vram 24, upscale 160,
lowermenu 246, options 49, xbrz 266, shadow 375, nitrosystem **17,122** — every suite 0 failed.

### What still fails, and why

**Nothing in the Phase 1/2/3 acceptance set.** The remaining gaps are deliberate deferrals or later
phases:

- **SoulSilver is built, but its hardware run is pending.** The retail US dump was verified, extracted
  and packed (`soulsilver3ds.pak`), and `pokesoulsilver.3dsx` boots the SS intro, title and main menu
  on the PC. Its 3DS hardware run (and the battle/close checks on the SS build) is the next owner run.
  Both games have separate apps, folders, packs and saves.
- **3DS depth:** the `.3dsx` runs on a New 3DS and draws both screens, audio and 3D; the battle Poképics render on the PC (720 tris/scene, 0 clipped) and the close path drains before joining. The hardware run of the latest artifact (60 Hz/audio-level/camera-plane, the battle look and a HOME-menu close) is still the owner's.
- **Web builder and CIA assets** are deferred (recorded in `docs/decisions.md` and the SD doc).

The old failure classes are documented with root causes in
[agent-notes/p2-wave-a-session5.md](agent-notes/p2-wave-a-session5.md) (the literal-address class:
`0x027FFxxx`, `0x05000000`, `0x07000000`, `0x04000400`) and
[agent-notes/p2-session6.md](agent-notes/p2-session6.md) (the 34 symbols, 3DS build, packaging, gate).

### Phase 2 state in one paragraph

The harness reads HGSS's own build files (1012 C, 56 C++, 150 ARM9 `.s`, 129 overlays), produces every
generated header without MWCC or a ROM, runs the decomp's `object_aliases.py` stage, converts the `.s`
files (with overlay IDs resolved as numbers), builds both port archives, groups the overlay sections,
generates the Wi-Fi stand-ins, compiles the program's MSL/C++/MB runtimes and its own libpak reader, and
links. Both binaries are clean. The last 34 symbols were: MSL soft-float/string (11, bit-pattern ABI),
C++ runtime (6, explicit MWCC-mangled definitions), multiboot (12, documented stand-ins) and five
Pokéwalker interior aliases (a missing build stage, now run).

## Hardware: it runs

Nine runs on a New 3DS. Current state: the game boots, plays the intro,
reaches Oak's speech and the player's house, draws both screens, plays audio
and (since the ninth run) shows the 3D: the title's Ho-oh, the room and the
player. The ninth run also exposed two defects, now fixed in the tree but not
yet re-run: the GPU worker was on the game's core and rendered about **1 image
a second** (105 images in 4,680 frames), and a `w = 0` vertex produced the
black fan over Ho-oh. The worker moved to core 2, the zero-area camera-plane
triangles are dropped by the port's clipper, and the audio thread moved above
the game. The run after those
fixes (log-9) then measured **87.0 Hz**: once the composite fit under one
refresh, `gfxSwapBuffers` stopped pacing the loop. The tree now caps frame
starts at 60 Hz (`[perf] frame cap 60 Hz`), so the next log's mean `total_us`
should be near **16,700**, not 11,490.

The scenes measured before the pipeline ran at **17 ms a frame (~58.7 Hz)**
with `logic_us` under 100 us, and the DSP reported **0.4% dropped frames**.
The ninth run's samples were 24.7 ms (40.4 Hz) overall, the field 19.8-20.9 ms
(48-50 Hz) and the intro 31-36 ms (28-32 Hz). Every bug found along the way —
and the rule that prevents it — is in [../wiki/3DS-Pitfalls.md](../wiki/3DS-Pitfalls.md);
[phase-5.md](phase-5.md) has the per-run numbers.

Still open on hardware: the re-run after these fixes (3D rate and no
title fan or gap, no hung/clipped/rough audio, 60 Hz frame times, Settings
open/close, a wild and a trainer battle with both Pokémon visible, and a
HOME-menu close), a full run through Oak's speech
at full speed, the save round-trip, the Old 3DS measurement, and the `.cia`
decision. The next log should show `[gx3d]
display on core 2`, a rising `3D images:` near 300 per 600 frames, no
`camera-plane drops`, and `audio: N dropped frames of M` near zero.

The first run's failure, for the record: the `.3dsx` never started. Luma wrote
a dump whose registers put the fault at a `udf #0` inside the console's
**`loader`** process, not in game code (our image is
`0x00100000`-`0x009263ec` and never appears; `log.txt` was never written).

One word in the `.3dsx` lied about being a pointer: patch 1406's `LcdcAddress()`
was `NitroPort_LcdcVram + (address - 0x06800000)`, which GCC reassociates to
`(NitroPort_LcdcVram - 0x06800000) + address`. The folded constant became an
ABS32 relocation with addend `-0x06800000`, resolving to `0xFA607000`
(ELF `0x008e5a20`); `3dsxtool` packs any relocated word at or above the image
base as an absolute pointer, so the Loader aborted. `3dsxdump` refuses both
earlier builds with `Cannot write segment data!`.

Fixed by patch **1903** (base read behind a `noinline` call) plus
`validate_3dsx()` in `tools/hgss_game.py`, which now fails the build when a
packaged `.3dsx` would be rejected. The rebuilt artifact passes `3dsxdump`
(`CODE 2087 pages, RODATA 109, DATA 13, BSS 3977`).

**The DS gate passed at 78 patches** for both games (HG `4fcded0e…b5a37`, SS
`f8dc38ea…e582`), including the `1407` audio patch.

## Next actions, in order

1. **Old 3DS 2D renderer.** Wave 1 code-complete, awaiting commit +
   validation build (running): S1 row cache, S2a unified keys, S2b
   word-copy hit path, S3 merge/finish/OBJ, S4 attribution + the 12-byte
   hbirq stride fix, o3dsched experiment, int-divide removal (154 tests).
   S2c stays deferred with data (setup 1–5%, rows 89–100%). Text 21–67%
   trips the plan's Wave-2-start (>15%). Wave 2 S5 started: GpuBgOf
   reject reasons + `[perf] gpubg` attribution (which check keeps text on
   software picks the next slice). S5b in tree (validation build running):
   any text map size routes through the quad (map-sized picture/upload/UV;
   256 path bit-identical), plus the no-blend flat quad (opaque replace,
   incl. the battles no-first-target case). S5+S6 bundled next (single
   stage): multi-BG quad slots (2-slot pool, front + behind-3D flat),
   affine-BG corner-UV quads, opaque-OBJ instanced quads from a lazy
   atlas, painter-ordered by the software interleave rule, all-quad or
   all-software per region. No staging until the bundle lands.
2. **Audio to core 1 — tried, reverted.** Log 9 proved core 1 oversubscribed
   (display down to 0.0 composes/s); default is back to core 0 with
   `audiocore=` kept for A/B. The `[perf] audio:` drop counter stays.
3. **Goldenrod outdoors** — still 5–6 composes/s: ~10,700 vertices and ~2,784
   clipped vertices per scene keep geometry at 19–29 ms/scene, and the display
   thread shares core 1 with it.
4. **Phase 4 exit items** — scripted save + `savetool.py`, sanitizer replay,
   goldens once captures exist, upstream drafts.
5. **Phase 5 hardware re-run** — 3D rate and title, audio by ear, frame times,
   Settings open/close, a wild encounter, the `.cia` decision.

## Open decisions

- **Cross-check allowlist granularity:** it is now 3,398 explicit rows with reasons (no wildcards).
  The alternative (a curated wildcard table) is smaller but can hide a source file; the explicit form
  is kept and audited. Revisit with the cross-check's comparison model if the table becomes unwieldy.
- **PC program boot depth:** the Phase 2 binary is a link/overlay-check target, not a playable build;
  Phase 4 owns the first frame. No decision needed now.
- **SoulSilver folder, pak and program** (`sdmc:/3ds/pokeheartgold-ss/`, `soulsilver3ds.pak`,
  `pokesoulsilver.3dsx`, `soulsilver-nitrofs-1`) are implemented: `heartgold.h` selects the names and
  `tools/hgss_game.py --game soulsilver` compiles the matching program. The two games stay fully
  separate so a save can never cross over.
- **Display remaster:** Phase A is implemented and mandatory. The HGSS shell compiles `port_compose_pica.c`, runs the drawer/display/V-blank threads, presents 400x240/320x240, draws the 3D at native resolution and maps touch 1.25x. There is no classic 1:1 path; a failed display shows `DisplayErrorScreen()`. An Old 3DS uses `smooth3d = 0`. Hardware validation (frame rate, memory, touch, stereo, audio, close) is pending on both models; Phases B-E (window hooks, pixel field, per-screen layouts, optional lower menu) are still in `docs/wiki/3DS-Remaster-Audit.md`.
- **Old 3DS vs New 3DS display profiles:** the two consoles get **separate
  fixed launch profiles**. Old 3DS: launch-time filtering only (1× texels, the
  runtime X+R cycle removed), widescreen kept as a launch option and on by
  default, the **lower screen's 2D at 30 Hz** while game logic stays 60 Hz, and
  `smooth3d = 0`. New 3DS keeps the runtime presets, the smoother filters and
  `smooth3d = 2`. Sharp 2× on Old 3DS is arithmetically impossible (its layer
  sets alone are 13,824 KB against a 15,732 KB linear heap). Plan and evidence:
  [agent-notes/s19-o3ds-performance-plan.md](agent-notes/s19-o3ds-performance-plan.md).
- Port-side fixes that are generic go to the Platinum maintainer as upstream PRs (assert/math
  force-includes, NNS table weak refs). HGSS-only shape changes stay in `hgss/`.
- Commit trailer is `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Resume in five minutes

```bash
cd <repo>
devcontainer up --workspace-folder .
devcontainer exec --workspace-folder . bash
# inside the container:
cd /workspaces/pokeheartgold-3ds
python3 tools/bootstrap.py            # "already at ... with 86 patches" if in sync
make test                            # exit 0 (355 tests + 20 SDK suites)
make port-tests                      # exit 0 (pak + nitro + nitrosystem)
python3 tools/hgss_game.py --target host32 --keep-going --out /work/pc -j5
                                     # 1092/1092, 0 unresolved, binary exists
python3 tools/hgss_game.py --target 3ds --keep-going --out /work/3d -j5
                                     # 1092/1092, 0 unresolved, .3dsx packaged
python3 tools/hgss_game.py --game soulsilver --target 3ds --keep-going --out /work/3d-ss -j5
                                     # the SoulSilver program, pokesoulsilver.3dsx
```

If `bootstrap.py` reports a patch-count mismatch, run `--clean`. If `build/` looks empty on the Mac,
that is expected: it is a volume (`hgss-build`) inside the container. Do **not** run
`bootstrap --clean` while a build or `make test` is reading the tree, and do not run `make test`
concurrently with a harness build: both touch the shared `build/hgss-sdk` shim (the test that used to
do it is fixed, but the rule is cheap to keep).

## Environment facts (measured in this container)

| Tool | Version | Used for |
| --- | --- | --- |
| Ubuntu | 24.04, amd64 | the container |
| Wine | WineHQ 10.0 | MWCC, `makelcf.exe`; pinned, do not bump |
| MWCC | 2.0/sp2p2 (patched sp2p3 for the assembler) | the DS-side matching build only |
| devkitARM | 16.1.0 | 3DS builds (`arm-none-eabi-gcc`, `3dsxtool`) |
| host gcc | 13.3.0 (`gcc`) | surveys by default, most SDK suites |
| host gcc-10 | 10.5.0 (`gcc-10`), also `CC` in the image | the port's `host32` build |
| Python | 3.12.3 | all tools; `pyelftools`, `unicorn` when noted |
| Make | 4.3 | |
| devkitPro tools | `3dsxtool`, `smdhtool`, `3dsxdump`, `picasso`, `bin2s` present | `.3dsx` packaging/validation and the 3D shader blob |

The full toolchain/build reference is [wiki/Build-Reference.md](../wiki/Build-Reference.md).

### Log

Newest first. Each entry: what changed, what was run, what it proved.

### Build identity + staging (standing rule, 2026-11-04)

- **Two builds never share a filename.** `tools/hgss_game.py` keeps the
  canonical `build/<out>/pokeheartgold.3dsx` (tests + HBL run path) and
  writes `build-info.txt` + `gen/build_id.h` (commit + `-dirty`) beside
  it; the boot banner logs `starting (build <id>)`, so any `log.txt`
  traces to its commit. `tools/stage_3ds.py` stages as
  `<base>-<shortsha>[-dirty].3dsx` (`-2`, `-3`, … on content collision,
  skip on identical bits) + `.build-info.txt` sidecar, `chmod 666`,
  `validate_3dsx`-gated. HG → `dist/sd-card/3ds/`; SS → both
  `dist/sd-card/3ds/` and `dist/soulsilver-sd-card/3ds/` (+ `--also`
  dirs). Stage command:
  `python3 tools/stage_3ds.py --hg build/game-3ds/pokeheartgold.3dsx --ss build/game-3ds-ss/pokesoulsilver.3dsx --also ~/hgss-private/sd-card/3ds`.
  Old same-named pairs are superseded — delete stale `.3dsx` from the
  console folder before copying the new pair.

### 2026-11-04 — wave 0 freeze (benchmark declaration; hardware runs pending owner)

- **Frozen:** `~/hgss-private/benchmark/goldenrod-bench.txt` (`sha256
  906d80bb…8925f8`, 471 entries) + `port.ini` (`sha256 c6b4a85a…cddf8`):
  A-mash 0–600 through boot/title/menu/continue, UP/RIGHT zigzag every 8
  frames to 4200; benchmark intervals are game frames 2400–4200. Config is
  shipped defaults made explicit (`filter=sharp stereo=on wide=on fps=60`
  + the script); per-console defaults (texScale, lower-30, cores) stay
  automatic. Owner procedure: copy both files into
  `sdmc:/3ds/pokeheartgold-ss/` (SS) alongside the current Goldenrod save,
  boot, and send back `log.txt` (needs ≥4200 frames: `[perf] display/2d`
  with setup % and rows hit/miss, `hb_*`, texture/settle counts,
  `linear_free`, `[gpu] clears`). Same on N3DS with the HG folder/save.
- **Staged for hardware (gate dispcnt log, 2026-11-04):** HG `pokeheartgold-75ddfe7-dirty.3dsx`
  `5,800,796 B` `sha256 2dbed6ca…0d1ca4`, SS `pokesoulsilver-75ddfe7-dirty.3dsx`
  `5,800,788 B` `sha256 c99211b8…75b602b` (`validate_3dsx` `[]`; in
  `dist/sd-card/3ds/`, `dist/soulsilver-sd-card/3ds/`,
  `~/hgss-private/sd-card/3ds/`). Supersedes all earlier pairs. Same
  Wave 0 benchmark procedure on o3DS (N3DS optional this round); watch
  for a one-shot `[2d] gate dsp: dispcnt=... powcnt=... isA=...` line
  near boot plus the full `gpubg` line. Report both plus
  `[perf] display/2d` (`rows hit/total`, `text %`, `setup %`),
  `[perf] hbirq`, `hb_*`, texture/settle counts, `linear_free`,
  `[gpu] clears`.
- **Staged (current):** same pair, in `dist/sd-card/3ds/`,
  `dist/soulsilver-sd-card/3ds/` and `~/hgss-private/sd-card/3ds/`.
- **Pending (owner-gated, not marked done):** the two 600-frame hardware
  logs. Owner procedure: copy `benchmark/goldenrod-bench.txt` (as
  `script.txt`) + `benchmark/port.ini` into `sdmc:/3ds/pokeheartgold-ss/`
  (SS; HG folder/save for N3DS), boot, play nothing (the script drives),
  and send back `log.txt` (needs ≥4200 frames).
- **Save bytes (closed):** `~/hgss-private/soulsilver.sav` (`sha256
  ecd65e77…91e74`, 524288 B, `savetool validate` IS_GOOD, newer mirror 0)
  is the frozen benchmark save — the owner's current Goldenrod file. The
  freeze is byte-complete.
- **Native gate status (no container):** runnable per slice are
  `test_hgss_game.py` (130), full `unittest discover -s tests` (416; 11
  pre-existing container-only errors), both 3DS builds + `validate_3dsx`,
  `check_patches.py`. NOT runnable natively and therefore pending with
  procedure: host32 links (game-pc, port-tests, sdk-tests need `-m32`,
  which cannot link on macOS), PC Goldenrod replay/goldens
  (`tools/golden/run.py` needs the host32 binary), `--check-overlays`,
  `symbol_crosscheck`, DS gate (`/work` absent). No patch-stack changes in
  Waves 0–1, so no DS-gate rerun is owed.

### 2026-11-04 — slice B: scroll-shifted text-BG row cache (all Old 3DS; New 3DS unchanged)

- **What.** Text pixels are a pure function of map coordinate, so a rendered
  map row serves every screen line showing it at any scroll offset. Per
  (engine, BG) direct-mapped cache of 256 rendered rows (`TextBgPixel`
  values), keyed by scroll-invariant `BgKey` + the wide-clear palette variant
  `BgKey` misses, tagged by map row. The plain tile loop becomes a span copy
  (`PUT` keeps front-layer/window semantics); all other paths and the
  malloc-failure fallback are byte-identical. Per-frame keys computed once per
  BG; captures set their own fresh keys.
- **Memory.** Lazy 134 KB tables from the game heap (12 MB, ~6 MB free), only
  for BGs actually drawn as text; linear/texture room untouched. The `[perf]
  mem:` line will show it.
- **Verdict lines.** `[perf] 2d:` gains `setup %` and `rows hit/total` — the
  hit rate judges the design on hardware.
- **Verified:** `tools/check_patches.py` 86/86; `validate_3dsx` `[]` on both;
  `tests/test_hgss_game.py` 130 tests, 126 pass (the 4 `ObjectAliasTests` need
  `gcc-10 -m32`, container-only) including 6 new `PerformanceSliceBTests`.
  Two build failures on the way, both mine: a `now[2]` array shadowing
  PerfLog's timestamp, and cache declarations used above their definition
  (fixed with forward declarations after `Engine`).
- **Staged:** HG `5,781,092 B` `sha256 606a4b4e…3106c65`, SS `5,781,084 B`
  `sha256 d97a214f…4f66fe` (in `dist/sd-card/3ds/`,
  `dist/soulsilver-sd-card/3ds/` and `~/hgss-private/sd-card/3ds/`).
- **Hardware run should report:** `rows hit/total` near 100% in the field and
  the `text %` line down proportionally. If hits are low, the scroll pattern
  defeats direct mapping and the design needs revisiting before any tuning.

### 2026-11-04 — wave 1 in flight: S2a + S3 + S4 staged; hbirq fixed, o3dsched + no-div experiment added

- **S2a** (unify 3 BgKey walks/BG/frame into one shared `frameKey`, reused by
  LookUpArt, the gpuBg path and the row cache; provably identical keys):
  implemented + tested, no behavior change by construction.
- **S4** (scoped implementer, disjoint files `port_os/thread/timer/dma` +
  additive `nitro_port.h` decls): per-IRQ-bit walk timing, SyncTimers
  count/cost, sound-pump tick source, H-blank DMA transfer count. Lead
  reviewed the diff + an independent scoped-explorer review: additions are
  host-safe, paired, non-dispatch-changing; the only behavior deltas in the
  diff are pre-existing session work (GxRefresh cadence, switch deferral),
  not S4. Integrated: `[perf] hbirq:` per-bit line (attribution, explicitly
  not a partition) + sound-pump timing split from game frame.
- **S3** (merge/finish/OBJ, lead-implemented): opaque-pair merge extended to
  exactly `T_LAYER` (proved flag-independent — no window bit, no prio filter,
  `layerOf` written exactly where PUT would; revives the wide path on all
  3D-screen lines); SnapshotOver alpha fill → memset when effect<2 or no
  first-target bits; finalize falls back to the plain resolve under the same
  guard (the brighten loop darkens even at effect 0/1 with target bits set,
  so the bldcnt guard — not effect — is the correct condition); OBJ art-line
  fast path (same ay/rows test the loop starts with). All provably
  pixel-identical; 4 new `PerformanceSliceS3Tests`.
- **Verified and staged (owner-requested test build):** both games build clean
  (`validate_3dsx` `[]`; HG 5,783,924 B `sha256 57ed38d6…a13ec2b`, SS
  5,783,980 B `sha256 8f58b099…560df4`); 141 tests, 137 pass (4
  container-only errors). Supersedes all earlier pairs.
- **This round's additions on top of Slice B:** hbirq last-walk fix (the
  per-bit table resets every walk — diffing it underflowed, log-11's garbage
  line), `o3dsched=display-first` experiment (opt-in, Old 3DS only: demote
  the always-ready geometry worker below the event-driven display on core 1;
  same-core threads don't preempt, so the lowest-priority ready thread
  starves — log-9's 0.0 composes/s), integer divide/modulo removal in hot
  2D paths (mask/shift, power-of-two only, provably identical).
- Next: owner's benchmark run (scripted), then S2b/S2c granularity decision
  from `rows hit/total` + `setup %`.

### 2026-11-04 — slice A: 2D per-phase counters + empty-line cut (all Old 3DS; New 3DS unchanged)

- **Counters.** `DrawEngineT` now accumulates per-engine ticks for text BGs,
  other BG kinds (`LayerLine`), OBJs (sort + per-line), `Merge`, `BlendLine`,
  windows, and finalize (backdrop/brighten/split fixups), plus line/draw
  counts — via `NitroPort_Render2DProfile`, declared in
  `nitro_port_render2d.h`. Ticks use `svcGetSystemTick` behind `#ifdef __3DS__`
  (port_os.c's pattern; host builds read 0, counts stay exact). `PerfLog`
  prints `[perf] 2d:` per interval (both engines combined, % of a core).
  Overhead is ~14–26 tick reads/line (~0.5 ms/frame worst case); the hardware
  numbers include it, so Slice B must beat them by more than that.
- **Cut.** Backdrop-only lines (`t.filled == 0`, provably all zero: the line is
  memset above and no `Merge`/`PUT` wrote into it) fill with the backdrop in
  one pass instead of zero + resolve. Plain path only; brighten/blend paths
  untouched.
- **Verified:** `tools/check_patches.py` 86/86; `validate_3dsx` `[]` on both;
  `tests/test_hgss_game.py` 124 tests, 120 pass (the 4 `ObjectAliasTests` need
  `gcc-10 -m32`, container-only) including 5 new `PerformanceSliceATests`.
  One build failure on the way: a `now[2]` array shadowed PerfLog's `now`
  timestamp — renamed to `cur`.
- **Staged:** HG `5,778,848 B` `sha256 d15ed488…ed4ead`, SS `5,778,904 B`
  `sha256 330a8e84…84fde2` (in `dist/sd-card/3ds/`,
  `dist/soulsilver-sd-card/3ds/` and `~/hgss-private/sd-card/3ds/`).
- **Hardware run should report:** the first `[perf] 2d:` lines from the field —
  which phase owns the 22–33 ms render. That decides the Slice B cache design
  (row cache vs per-BG band cache, and the key granularity).
- **Follow-up (`log-o3ds-soulsilver-10.txt`):** attribution is unambiguous.
  Field intervals read `text 21–25%, affine 0.0%, obj 2–5%, merge ~1%, blend
  1–3%, window 0.0%, finish 9–15%` of core 0 — i.e. text BGs are ~60% of the
  attributed 2D time, finish (backdrop/split fixups) second, everything else
  noise. Affine BGs don't occur in the field; windows cost nothing. ~1/3 of
  render is unattributed (per-frame setup: keys, art lookups, palettes — Slice
  B should profile that too). `emptyLines` 0–8% of lines. Also seen again:
  GPU `clears` at 20–30 ms in-field (the 167 ms Goldenrod line is the same
  cost scaled by scene — a later target, not this round).

### 2026-11-04 — wave 3: float in-between matching, audio to core 1, launch-only filter, lower 30 Hz (all Old 3DS; New 3DS unchanged)

- **In-between-scene matching is now float.** `ndc_of` and `make_mid_scene`'s
  `hx/hy/fx/fy` in `3ds_port/nitro/src/port_gx3d.c` were the last `double`
  (soft-float) hot path; they are `float` with `fabsf`/`floorf`, mirroring the
  landed clipping/projection conversion (screen coords to ~1/256 px, depth to
  ~1–2 of 24 bits). The quarter-screen cut-off needs ~1e-7, not double.
- **Audio moved to core 1 on Old 3DS** (`main_3ds.c` `AudioStart`: core 1,
  priority still `0x2e` above game and geometry; New 3DS keeps `-2`).
  `audiocore=0|1` in `port.ini` overrides either console. The per-interval
  `[perf] audio:` line now names the core it measured and counts NDSP dropped
  frames per interval (`%u dropped of %u`), not just at quit — that is the
  underrun counter the move is judged by.
- **X+R filter cycle is launch-only on Old 3DS.** The combo detection and
  masking are untouched, but on an Old 3DS it never fires (`if (!sNew3ds)
  filterCombo = false`): three of the five presets are identical at 1 texel a
  pixel, and the `filter3d` presets would 2–4x texture memory mid-run with under
  a megabyte free. New 3DS keeps the runtime presets and toast.
- **Lower screen's 2D at 30 Hz on Old 3DS** (`lower30=on/off`, default on for
  Old, off for New). `DrawerMain` skips engine B on odd stamps unless a redraw
  is forced (`sMustDraw`, pending toast); a skipped frame publishes nothing so
  the display reuses the last set. Only engine B ever skips (it cannot carry
  the 3D); game logic and the upper screen stay 60 Hz. Fades still publish
  through the brightness-only path when the picture is unchanged.
- **Verified:** `tools/check_patches.py` 86/86; `validate_3dsx` `[]` on both;
  `tests/test_hgss_game.py` 119 tests, 115 pass (the 4 `ObjectAliasTests` need
  `gcc-10 -m32`, container-only) including 4 new `PerformanceWaveThreeTests`.
- **Staged (revised twice):** HG `5,776,012 B` `sha256 31228ded…86725c`, SS
  `5,776,068 B` `sha256 97d17359…6d199` — reverts the audio move to the core-0
  default (see log-9 finding below). Supersedes both earlier wave-3 pairs
  (`7d3986fc`/`9967fe33` and `9b0bcb26`/`58f556c3`); run this one.
- **Follow-up from the owner's runs.** `log-o3ds-soulsilver-8.txt` (first wave-3
  pair) printed audio `on core 0`: `AudioStart()` ran before
  `RequestCore1Share()`, so the core-1 `threadCreate` failed into the fallback.
  Fixed by requesting the share first (a test pins the call order in `main`).
  `log-o3ds-souldilver-9.txt` (second pair, audio confirmed `on core 1`) then
  ran **slower**: in the same field (same OAM, same verts, same scenes/s),
  core 1 went geometry 61–79% → 84–100% busy for identical throughput, audio
  9–28% → 24–40% (wall-clock inflation under preemption), and the display
  starved as far as 0.0 composes/s. Core 1's 80% share fits geometry + display
  but not audio on top — priority cannot create time on an oversubscribed
  core. Default is back to core 0 on Old 3DS (`audiocore=` kept for A/B).
  Also confirmed: `3D images: 0` matches logs 6/7 (not a regression), 0
  dropped audio frames in all runs, and past frame ~4200 the heavy area still
  saturates geometry at 99–100% (the known Goldenrod wall).
  Still open: row-level 2D dirty tracking + per-BG band caching (the 39–59 ms
  wall), the 167 ms Goldenrod `clears` line, publish beyond band-diff.

### 2026-11-03 — the H-blank walk's scheduler switch (Old 3DS: 0.3 → 12–14 composes/s)

- **Symptom and measurement.** `FRAMETIME` on an Old 3DS reported `hblank_us` of
  9.5–18.7 ms whenever the game armed H-blank, and the split showed `hb_irq_us`
  11.8–12.8 ms of it (`hb_keep_us` 0.45–0.7 ms, `hb_dma_us` 0). The H-blank
  callback itself is `HBlankSystem_Callback` with idle tasks, and hoisting
  `NitroPort_GxRefresh` out of the per-line raise changed nothing, so the cost
  was neither the game's effect nor the FIFO refresh.
- **Cause.** `NitroPort_IrqThreadSwitch` ran once per scanline — 192 scheduler
  entries a frame — because the synthetic walk raises the interrupt 192 times.
- **Fix.** The switch is deferred while the walk runs
  (`NitroPort_IrqSwitchDeferred`): each raise still dispatches its handler and
  does its bookkeeping, the switch request is noted, and **one** switch runs
  when the walk ends, at the same point in the frame the game sees it.
- **Result (Old 3DS, `log-o3ds-soulsilver-6.txt`):** `hb_irq_us` 12,751 → **411**;
  `hblank_us` 18,737 → 2,234; game frame (`total_us`) 41,218 → **2,300–8,100 µs**;
  drawer 1.1 fps / 721 ms → **12.1 fps / 54.8 ms** (38.7 render, 16.0 publish);
  composes 1–2 → **9.9–13.7/s**. That single change took the port from a
  slideshow to roughly 10–14 fps on an Old 3DS.
- **Also in this build:** the two safe parts of the 3D command-build slice
  (per-batch C3D state caching and alpha-31 texture hoisting) and the two
  memory wins the revert had cost (lazy classic readbacks, opt-in shadow room:
  `linear_free` back to 7.6–7.8 MB). Opaque batch merging and the stencil
  change are permanently dropped — they caused duplicated geometry and
  rectangular halos on hardware.
- **Where the Old 3DS stands.** Field: game 2–8 ms, drawer 55 ms (39 render,
  16 publish), composes 10–14/s. Goldenrod outdoors is the outlier: ~10,700
  vertices and ~2,784 clipped vertices per scene, geometry busy 97% of core 1
  (32.8 ms/scene), display build 490–768 ms — the display thread now shares
  core 1 with geometry, so it starves there. The clipping/projection path is
  written in `double` (soft-float on this CPU); converting it to `float` is the
  next slice.

### 2026-11-03 — the five-slice performance wave (core 1, publish, 2D, 3D, audio)

Ran as five parallel slices on disjoint files, then integrated in one build.

- **Core layout (`main_3ds.c`).** The display/compose thread runs on **core 1**
  at `0x32` on an Old 3DS (New 3DS byte-identical); `displaycore=0|1` in
  `port.ini` overrides it. The APT core-1 share is now requested **before any
  core-1 thread exists** (`RequestCore1Share`), because the display thread is
  created before the geometry start-up used to ask.
- **Publish (`port_compose_pica.c`).** A 1x band-diff (`publish_bands`) against
  the same per-target generations the 2x publisher uses, so unchanged rows are
  not re-converted, re-tiled or re-flushed; a brightness-only publish that
  reuses a certified set's textures with no conversion at all; arithmetic
  RGB565 (verified identical over all 32,768 table inputs) with the table left
  only for the 2x path.
- **2D renderer (`port_render2d.c`).** Empty-row skips (one full-row memcpy,
  alpha fill and memset per upper line when the 3D is at priority 0),
  span-limited OBJ clears, wide fills, and the per-line window memcpy removed.
  The agent also corrected the brief: with `split.snapshot = 1` the windows were
  already fetched once a frame, so the per-line cost was the copy, not the lock.
- **3D command build (`port_gx3d_pica.c`).** C3D state cached across batches,
  alpha-31 textures hoisted out of the second loop, safe opaque-batch merging
  (LEQUAL and translucent excluded), and the stencil test dropped when edges and
  shadows are both off.
- **Audio (`port_sound_chip.c`).** Silent-channel skip, hard-pan hoisting and a
  full-volume shortcut, all bit-exact. The requested `step == PHASE_ONE` path
  was **proved unreachable** at 32,768 Hz (`SOUND_CLOCK / 32768` is not
  integral) and deliberately not added.
- **H-blank (this session's own slice).** The walk is split into
  `hb_keep_us` / `hb_dma_us` / `hb_irq_us`. It is 8 us when the game does not
  arm H-blank and **9.5-13.8 ms when it does**, which is what collapses
  `log-o3ds-soulsilver-3.txt` from 17 fps to 0.3 composes/s.
- **Build hazard found:** an edited port source (`port_os.c`) did not trigger a
  rebuild of `libnitro.a` in an incremental run, so the link failed on the new
  symbol. Removing `build/<out>/sdk` forced a correct rebuild. Worth knowing:
  the app objects are compiled by the Python tool and always fresh, the vendored
  archives are not guaranteed to be.
- **Verified:** both games compile 1092/1092, link clean and package
  (`validate_3dsx` `[]`); 115 tests, 111 pass (the 4 `ObjectAliasTests` need
  `gcc-10 -m32`).
- **Staged:** HG `5,774,652 B` `sha256 9e642cad…b7bb`, SS `5,774,708 B`
  `sha256 d51e463a…bd45`.

### 2026-11-03 — native tree short five patches; W0 phases and explicit texScale

- **Missing patches:** `build/upstream` was not a complete application of the
  patch stack. Five had never been applied: **1904** (Settings close),
  **1905** (hardware addresses use the port's macros), **1909** (PC-box mode 4),
  **NitroSDK 0605** (ortho projection/rotations) and **NitroSystem 0304**
  (software-sprite commands, the battle Poképics fix). The symptom on hardware
  was a data abort in `WirelessStatusIcon_LoadChar`, which still read the raw
  `0x04000000` that 1905 replaces. All five are applied now (86/86; 87 commits,
  byte-identical subject set to the container's tree), and
  `tools/check_patches.py` fails the build if any patch's `git am` subject is
  missing from the tree's history -- wired into `make check-bootstrap` and into
  `tools/hgss_game.py`, since the native workflow invokes it directly.
- **W0 phases:** `FRAMETIME` now carries `hblank_us` and `handoff_us`, the two
  phases of `DrawFrame`, because the audits attributed its 13.5 ms on an Old
  3DS to three different things. The accepted core-1 APT share is logged
  (`APT_GetAppCpuTimeLimit`) -- every core-1 placement decision depends on it
  and it had never been read back.
- **W2 `texScale`:** `ComposeConfig.texScale` (and a `texscale=` launch key)
  lets an Old 3DS ask for 1 texel a pixel outright. The Old 3DS was walking the
  2x attempt (13,824 KB of layer sets), failing, and dropping the lower menu
  and the weather BG on the way down; it now allocates 1x directly. The banner
  prints the effective scale, not the requested filter.
- **Verified:** both games compile and link; `validate_3dsx` `[]`; 115 tests,
  111 pass (the 4 `ObjectAliasTests` need `gcc-10 -m32`).
- **Staged:** HG `5,764,600 B` `sha256 fb95ace2…3f99`, SS `5,764,656 B`
  `sha256 e2d2e16d…52d0`.

### 2026-11-03 — dead on both consoles: a live thread was freed at teardown

- **Symptom:** the first W0-W2 build (`sha256 2fefabad…c75d`) died on both an
  Old and a New 3DS. `log-luma-crash.txt` shows the path:
  `[0] the game's thread ended at frame 0`, then the game keeps running
  (overlays load, SoulSilver starts), then `[110] [quit] closing` — exactly
  2 s later.
- **Cause:** a pre-existing race, exposed by the timing change rather than
  introduced by it. `WaitForGame` loops on `sGameThreadAlive`, which the game
  thread sets as its **first instruction**. A newly created thread has not
  necessarily run yet when `threadCreate` returns, so the loop saw `false`,
  reported "the game's thread ended at frame 0", and fell through to
  `threadJoin(thread, 2000 ms)` — which blocked for its full 2 s while the
  game reached frame ~110 — then `threadFree`d a thread that was still
  running. The teardown then unhooked APT and exited NDSP/gfx underneath live
  game code, and the process faulted.
- **Fix:** `sGameThreadAlive = true` is set **before** `threadCreate` (and back
  to false if creation fails), so "not started" is no longer read as "ended";
  and `threadFree` now runs only when the join succeeded. A timed-out join
  sets `sThreadsStuck`, which makes the caller `svcExitProcess` instead of
  tearing the runtime down around a live thread.
- **Tests:** `ThreadLifecycleTests` (2 tests) pin both halves; 110 tests, 106
  pass, the 4 `ObjectAliasTests` still need `gcc-10 -m32`.
- **Staged:** HG `5,763,136 B` `sha256 728d16c2…4462`, SS `5,763,128 B`
  `sha256 ab2e23ff…2211`; `validate_3dsx` `[]`.

### 2026-11-03 — Old 3DS performance waves W0-W2 (compiler, memory, instruments)

- **W1 compiler:** `tools/hgss_game.py:port_cflags()` now adds `-O2` for the
  1,092 game objects (they were at GCC's default `-O0`; only the app and the
  vendored port libraries had `-O`). The sanitizer build keeps its own level.
  Measured: game `.text` 8,146,868 → 4,512,808 B (−44.6%); image
  **33.26 → 29.84 MiB**; artifact **9,403,452 → 5,763,136 B**. On a 64 MiB Old
  3DS process that returns **3.42 MiB** to the heaps.
- **W2 memory (partial):** `__system_allocateHeaps` now takes an explicit
  small-process split (`remaining <= HEAP_SMALL_TOTAL` → game 12 MB, display the
  rest) instead of the old `remaining > 16 MB * 2` guard that a 30.73 MiB Old
  3DS failed, silently half-splitting. With W1 the linear heap is
  **15.73 → 22.16 MB**. Two dead allocations are gone: the classic readback
  images (1,152 KB) are made by `classic_images_ready()` on the first
  `NitroPort_PicaRender`, and the sun-shadow vertex room (1,438 KB) is opt-in
  through `NitroPort_PicaSetShadowRoom`, which the shell calls with
  `sCompose.shadows` = 0.
- **W0 instruments:** `LOG_LINE` 220 → 512 (every `[perf] display` line in the
  Old 3DS log was truncated mid-field), a 32 KB stdio buffer flushed once a
  second plus explicit flushes at the `[perf]` block and on terminate/reset, and
  `total_us`/`state_us` now measured from one anchor (frame 1800 reported
  `total_us` 10,810 < `state_us` 14,118, which is impossible). New line:
  `[perf] mem: linear_free=… KB game_heap_used=… KB`.
- **Verified:** both games compile 1092/1092 and link clean natively;
  `validate_3dsx` `[]`; `tests/test_hgss_game.py` 108 tests, 104 pass and the 4
  `ObjectAliasTests` errors are the `gcc-10 -m32` object tests that cannot run
  on macOS.
- **Staged:** `dist/sd-card/3ds/`, `dist/soulsilver-sd-card/3ds/` and
  `~/hgss-private/sd-card/3ds/`.
  HG `5,763,136 B` `sha256 728d16c2…4462`;
  SS `5,763,128 B` `sha256 ab2e23ff…2211`.
- **Not yet done:** W3-W6 (game-thread frame cost, 2D renderer, publish,
  threading) and the rest of W2 (command-list sizing, lower menu, texture
  floor, explicit `texScale`). Hardware runs pending.

### 2026-11-03 — Old 3DS performance investigation (plan only, no code changed)

- **Trigger:** the owner reports the New 3DS does not hold 60 fps and drains
  the battery; the Old 3DS does not reach 30 fps and plays "incredibly slow".
- **Evidence:** `log-o3ds-soulsilver.txt` (149 lines) and
  `log-soulsilver-normal-playthrough.txt` (5,229 lines), plus five read-only
  audits (compiler, CPU, GPU/compositor, memory/VRAM, threading) and source
  reads by the lead.
- **Three measured facts the plan rests on:**
  - the Old 3DS process is exactly 64 MiB — image 33.26 MiB (code 8.41,
    rodata 0.44, bss 24.40) plus heaps 30.73 MiB — so there is no larger
    memory mode to request; the `.3dsx` header was parsed to prove it;
  - `__system_allocateHeaps` never takes its small-process branch on Old 3DS
    (`remaining` 30.73 MiB < the `16 MB * 2` lower bound), so it half-splits
    15.7 MB game / 15.7 MB linear although the game heap used 798 KB;
  - the 3D texture cache ends with **408 KB**, which is why a 512×512 texture
    can never be resident and why `[hitch] ... 20 textures converted: 8907.4 ms`
    happened at frame 1204.
- **Compiler finding:** all 1,092 game objects are at **`-O0`**
  (`port_cflags()` has no `-O`; only the app objects add `-O2`). Measured
  out of tree: `-O2` shrinks game `.text` 8,146,868 → 4,512,808 B (−44.6%) and
  the packaged artifact 9,403,804 → 5,762,900 B. Unvalidated artifacts exist at
  `build/game-3ds-o2/` and `build/game-3ds-o2-ss/`.
- **Not settled:** the average DS clock is 39-46 Hz by the audio counters, but
  sampled frame bodies cost 14.0-14.7 ms (~70 Hz) and frame 1800 reports
  `total_us` 10,810 < `state_us` 14,118 — so the existing counters cannot
  attribute the deficit. W0 fixes the instruments before anything else.
- **Output:** the wave plan is
  [agent-notes/s19-o3ds-performance-plan.md](agent-notes/s19-o3ds-performance-plan.md).
  **No source was changed and no artifact was restaged.**

### 2026-11-03 — black screens after HOME/sleep (shell suspend handshake)

- **Symptom:** closing the lid or opening HOME and resuming SoulSilver left
  both screens black; HOME/exit became unresponsive. `log-black-screen.txt`
  ends at `paused (APT hook 0)` with no `resumed (APT hook 1)`.
- **Cause:** `GpuComposeLoop` could pass its one `sSuspended` check and then
  spend 5-90 ms building a frame. `NitroPort_ComposeGpuBusy()` only sees
  submitted GPU work, so `AptHook` returned while a frame was about to
  submit. APT took the GPU and the GX queue never completed.
- **Fix:** shell-only suspend handshake:
  - `sComposing` bracketed around both compose calls (busy-first, check
    `sSuspended` after; Dekker ordering);
  - `AptHook` waits for `sComposing` before `NitroPort_ComposeHold(1)`, then
    for drawers/GPU;
  - `ONRESTORE` only sets `sResumePending`; the real resume happens in
    `NitroPort_OnFrame` after `aptMainLoop` returns, so citro3d's own
    `ONRESTORE` hook has restored PICA state;
  - `sAptHome`/`sAptAsleep` guards from Platinum.
- **Tests:** 102 shell tests; full `make test` green. Both `.3dsx` rebuilt.

### 2026-11-03 — PC box MOVE POKEMON crash (patch 1909)

- **Symptom:** choosing MOVE POKEMON in the PC box crashed immediately on
  entering OVY_14. Deposit/withdraw worked.
- **Cause:** the box can launch as mode 4; `PCBox_State_Start` had cases only
  for modes 0-3 and left `nextState` uninitialised. The fade returned the
  previous frame's `PCBoxWork *` as the state index, and
  `PCBox_RunStateMachine` faulted on `gPCBoxStateTable[heap pointer]`
  (`*state == work == 0x01243658`).
- **Fix:** patch 1909 initialises/defaults the start state (mode 4 -> move
  layout), defaults the mode-grid layout, and bounds-checks the dispatch
  under `SDK_PORT_3DS`. Tests 102/102, full `make test` green; both `.3dsx`
  rebuilt.
- **Trace:** [agent-notes/s18-3ds-remaster-phase-a.md](agent-notes/s18-3ds-remaster-phase-a.md),
  `log-move-pc-crash.txt`.

### 2026-11-02 (session 18) — Phase A: the 3DS remaster display

- **The display is Platinum's compositor, and the only one.** `main_3ds.c`
  links `port_compose_pica.c`, runs display/drawer/V-blank threads, presents
  400x240/320x240, draws the 3D at native resolution, maps touch 1.25x and
  keeps H-blank/weather handling. There is no 1:1 path; a failed GPU setup
  shows `DisplayErrorScreen()`.
- **Old 3DS is the same layout** with `smooth3d = 0`; the New 3DS averages a
  2x render. Platinum's `__system_allocateHeaps` reserves linear memory.
- **Tests:** `test_hgss_game.py` updated away from the classic display; 85/85
  pass in the container. Both `.3dsx` files build with the remaster objects.
- **Pending:** the real Old/New 3DS run (frame rate, memory, touch, stereo,
  audio, HOME-menu close). Full record:
  [agent-notes/s18-3ds-remaster-phase-a.md](agent-notes/s18-3ds-remaster-phase-a.md).

### 2026-11-01 (session 9, 3D) — the camera-point vertex and the title gap

> **Superseded in session 10.** The `(0,0,0,0)` vertex is a zero-area
> camera-plane triangle, not a wing. The final fix drops exactly those
> triangles in `stage_triangle()`; the `clip[3] = 1` / large-`w` workarounds
> are reverted. A real DS capture shows Ho-oh intact. See the session 10 note
> and [agent-notes/s13-nns-volume-table.md](agent-notes/s13-nns-volume-table.md).

- **The title gap was our drop, not a missing near plane.** The dropped
  triangles are real quad/quad-strip geometry with one vertex at clip
  `(0,0,0,0)`; the G3 primitive counts in the title were quads 17,016 and
  quad-strips 22,688, with zero triangle-strip/list occurrences, so a strip
  sentinel is ruled out. The vertex has distinct model-space coordinates and
  valid fixed `s/t/colour` — it is the camera point. The PC reference and the
  DS put it at screen `(0,0)` and draw the fan from that corner; the PICA
  cannot divide by `w = 0`, which made it black.
- **Fix in the port:** `3ds_port/nitro/src/port_gx3d.c` `to_screen()` stores
  `clip[3] = 1` for the exact `(0,0,0,0)` vertex and leaves its screen
  position, depth and `inv_w` alone, so the GPU divides it to the same corner
  with valid texture/colour. The PC software frame at title frame 4800 is
  byte-identical to the clean reference. A viewport-centre mapping was
  rejected: it moved the fan and cut Ho-oh's left wing off.
- **Workaround removed:** `hgss/game/app/pica_scene.c`, its app-source line,
  the `GpuMain` call, the `camera-plane drops` log fields and
  `tests/game/pica_scene/` are gone. `tests/test_hgss_game.py` asserts the
  port hunk and the absence of the drop.
- **Regression test:** `nitro/tests/test_gx3d.c` `test_camera_point_vertex`
  emits a triangle with a `(0,0,0,0)` vertex, checks the stored `clip[3] == 1`
  and screen `(0,0)`, and checks the batch builder gives the GPU `w > 0`.
- **Local:** `make test` **338 Python tests, OK** + 20 SDK suites;
  `make port-tests` gx3d **337 + 239**, nitrosystem **17,122**, 0 failed;
  host32 1092/1092, 0 unresolved; 3DS 1092/1092, 0 unresolved, `.3dsx`
  **9,278,120 B** (`sha256 3f493661…b7f00`), `3dsxdump` rc 0 (CODE 2126,
  RODATA 112, DATA 13, BSS 4446 pages). `3ds_port/` is deliberately no longer
  byte-identical; the PR content is the one `to_screen()` hunk plus the test.
  Hardware is the owner's run: the title fan must be textured, not black, and
  the gap gone (commit `920c573`).
  [agent-notes/s11-3d-camera-point.md](agent-notes/s11-3d-camera-point.md)
- **DS gate at 78 passed** for both games (HG `4fcded0e…b5a37`, SS
  `f8dc38ea…e582`), so patch 1407's `#else` text is unchanged.

### 2026-11-02 (session 15) — the save CRC table, and the tall-grass "communication error"

- **The port did not build the CRC-16 table it used.** `GF_CRC16Init` called
  `MATH_CRC16InitTable`, and `GF_CalcCRC16` used `MATH_CalcCRC16CCITT`. The
  DS's name builds the CCITT table; the port's `nitro/math/crc.h` remaps it to
  ntrtwl's reflected table, so the port built a reflected table and updated it
  MSB-first. Every real save failed all four footer checks; port-written saves
  still loaded because writer and reader shared the mistake. The retail
  save's slot 0 stores `0xa9ab`; the old pair computes `0x2b01`, exactly what
  `Downloads/heartgold-1.sav` stores. Patch **1906** keeps HGSS's CCITT
  builder for `math_util.c` under `SDK_PORT_3DS`.
- **Tall grass showed the communication-error screen because of a save
  assert.** The old builds also wrote each substruct's CRC with the wrong
  table. The loader only checked the slot footer, so the save loaded; the
  first `Save_*_Get` on a substruct asserted. `PrintErrorMessageAndReset`
  uses `msg_0041_00003`, the same text as the communication-error screen, and
  the main menu's beacon watch leaves a `CommState` alive, so the assert drew
  it. Patch **1907** detects the old deterministic substruct CRC, replaces it
  with the CCITT value and continues; anything else still asserts. PC
  verification: the legacy field save now reaches the wild battle with
  `720 tris (720 opaque, 0 translucent)`, `lost: 0 clipped`.
- **Save tooling.** `tools/savetool.py` gained `repair IN -o OUT`, which
  recomputes structurally valid slot-footer CRCs and repairs saves from the
  early 3DS builds. It cannot know substruct boundaries; the port migrates
  those on first read.
- **Tests.** `tests/test_save_crc.py` (4), `tests/test_savetool.py` (18),
  `tests/sdk/gcc_fixes/run.sh` group (l), and the PC encounter above.
- **Artifacts.** HG `9,280,164 B`, `sha256 c87eaeb8…1103fa40`; SS
  `9,280,204 B`, `sha256 32b5f3f7…c42565`.
  [agent-notes/s17-save-crc-and-tall-grass.md](agent-notes/s17-save-crc-and-tall-grass.md)

### 2026-11-02 (session 14) — the SoulSilver program

- **The shell was HeartGold-only.** `main_3ds.c` hardcoded
  `sdmc:/3ds/pokeheartgold/`, `heartgold3ds.pak` and `heartgold.sav`, and
  `heartgold.h` hardcoded the `IPKE` game code and `heartgold-nitrofs-1`.
  Both now select from the decomp's own game define: `SOULSILVER` gives
  `IPGE`, `POKEMON SS`, `soulsilver-nitrofs-1`, `sdmc:/3ds/pokeheartgold-ss/`,
  `soulsilver3ds.pak` and `soulsilver.sav`; the default `HEARTGOLD` is
  unchanged.
- **`tools/hgss_game.py --game {heartgold,soulsilver}`** adds the define to
  the game and app compile and names the program (`pokeheartgold` or
  `pokesoulsilver`; the `.3dsx` drops `-3ds`). The SMDH title changes too.
  `make game-pc-ss` and `make game-3ds-ss` are the shortcuts.
- **The real SS dump** (`/Users/terezi/hgss-private/soulsilver.us.nds`,
  SHA-1 `f8dc38ea…e582`) was verified, extracted (513 files /
  125,549,877 B) and packed (`soulsilver3ds.pak`, 125,556,128 B, build ID
  `soulsilver-nitrofs-1`) with `scripts/prepare_game.py`.
- **PC check.** `pokesoulsilver-pc` builds 1092/1092 with 0 unresolved and
  boots the SS intro, title and main menu (the "CONTROL INFO / ADVENTURE
  INFO / NO INFO NEEDED" screen) with the SS pack. The screenshot is the
  end-to-end proof that the code variant and the pack agree.
- **3DS builds.** Both variants compile 1092/1092 and link with 0
  unresolved: HeartGold `pokeheartgold.3dsx` 9,279,628 B (`sha256
  92381334…5285`) and SoulSilver `pokesoulsilver.3dsx` 9,279,668 B
  (`sha256 c2e9a50e…e4cf`), both `3dsxdump` rc 0.
- **Staged.** `dist/sd-card/3ds/` has both programs, both packs and both
  manifests; the SS pair is `pokesoulsilver.3dsx` plus
  `pokeheartgold-ss/{soulsilver3ds.pak,manifest.json}`. The scripts pick the
  game from the pack's name and stage the right names automatically.
- **Checks.** `make test` **355 tests** (a shell-variant test was added);
  port tests unchanged at 17,122 nitrosystem checks. The SS hardware run is
  the owner's next step.

### 2026-11-02 (session 13) — invisible battle Pokémon; the close hung

- **The Pokémon were invisible.** Log-12's battle runs (the fight completes,
  the UI works) but every 3D scene is `0 vtx, 0 polys` except for a flicker
  during attacks. The battle's Pokémon are **Poképics**: 3D quads drawn by
  NitroSystem's software-sprite path (`NNS_G2dDrawSpriteFast`,
  `NNS_G2dSetupSoftwareSpriteCamera`) under a one-time screen-space ortho
  projection, rotated with `G3_RotX/Y/Z`. Those files wrote the G3 command
  registers directly; the port's GXFIFO is ordinary memory and only sees
  writes through `NitroPort_GxDirectWrite`. The quads never entered the scene;
  once the sprite path was converted, they showed up but were all clipped
  (`lost: 360 clipped`) because `g3_util.c`'s `MTX_MODE = PROJECTION` and the
  rotations were still raw, leaving the field camera's projection in place.
  Patches **NitroSystem 0304** and **NitroSDK 0605** convert the remaining
  writes. The PC battle (the owner's `heartgold.sav`, the scripted zig-zag in
  the grass) now renders both Pokémon: `720 tris (720 opaque), lost: 0`.
- **The close hung.** Log-12 ends at `closing at frame 15296` and the console
  has to be power-cycled. `GpuStopGeometry` stopped and joined the geometry
  thread and then called `NitroPort_Gx3dStopThread()`; its `drain()` waits for
  the command ring to empty, but the close path leaves `NitroPort_OnFrame`
  before `Gx3dFlush`, so the frame's last commands were still in the
  producer's buffer and the worker that would have consumed them had already
  exited. Drain first, then stop the worker; the shutdown now logs each
  `[quit]` step and ends the process at once if a thread will not stop.
- **Trace.** [agent-notes/s16-pokepics-and-quit.md](agent-notes/s16-pokepics-and-quit.md)
  has the two causes, the PC reproduction (the owner's save plus the zig-zag
  input) and the log lines.
- **Checks.** `make test` 354 tests; `make port-tests` **exit 0** (the pak,
  nitro and nitrosystem suites; nitrosystem **17,122 checks, 0 failed**);
  `bootstrap` 82 patches; the PC battle and the 3DS build below. The close and
  the battle's look on hardware are the owner's run.
- **Artifact.** `.3dsx` **9,279,628 B**, `sha256
  6bad8620e3fcb01b482ad0087b820f19e13ea7b8e1e05746ad6a4224ef70303f`; 3DS
  build **1092/1092**, 150/150 `.s`, **0 unresolved**; `3dsxdump` rc 0
  (CODE 2126, RODATA 112, DATA 13, BSS 4446). The non-`SDK_PORT_3DS` text of
  0304/0605 is unchanged, but the DS gate has not been re-run with them (the
  last DS gate was at 78 patches).

### 2026-11-01 (session 12) — the tall-grass wild encounter crashed

- **Log-10.** `=== exception (type 1) pc 008bcf0c ... r3 04000000 ===` right
  after OVY_6/OVY_7 (the battle overlays). `pc` maps to
  `WirelessStatusIcon_LoadChar`; the faulting instruction is
  `ldr r3, [r3]` with `r3 = 0x04000000` (DISPCNT), and `lr` is the return
  into `WirelessStatusIcon_New`. `Battle_CreateWirelessStatusIcon` runs at
  every battle setup, so wild, trainer and link battles all hit it.
- **Log-11.** With the DISPCNT fix the battle now reaches its background, then
  faults in `MIi_CpuCopy32` (`pc 008f0e24`, `r0 06010000`, `lr` in
  `BattleSystem_SetBackground`): `MIi_CpuCopy32((void *)0x6010000,
  battleSystem->bgTiles, 0x10000)` copies 64 KB from BG VRAM by literal, and
  the terrain pass reads OBJ VRAM `0x6400000`. Both are now `HW_BG_VRAM +
  0x10000` and `HW_OBJ_VRAM`. The opponent sprite showed and the player's did
  not because the copy runs during the battle background setup.
- **Cause.** The game read DISPCNT through a literal. The port's DS memory is
  ordinary arrays (patch 1301), so the literal is unmapped on the 3DS and the
  PC; the DS build works because it is a real register. The decomp is matching:
  this is game code, not a port defect, that only the port can trip.
- **The class.** A preprocessor-aware scan of the game sources (evaluating
  `SDK_PORT_3DS`, so DS-only `#else` copies are ignored) found 20 literals in
  code the port compiles: the DISPCNT/DB_DISPCNT pair in the wireless icon and
  the spin trade app, the G3 swap-buffers request in the Pokémon summary,
  nine DTCM IRQ-check writes, the encounter effect's LCDC VRAM clear, the
  battle background's two VRAM reads, and two shared-main-memory offsets.
  Patch **1905** converts all of them to the macros the engine already uses;
  each hunk is `SDK_PORT_3DS`-only. The first sweep matched only 8-digit
  spellings and missed `battle_system.c`'s `0x6010000`/`0x6400000`; the
  scanner now matches the value, which is how log-11's two sites were found.
- **Guard.** `tests/test_hw_addresses.py` models the port's preprocessor and
  fails on a raw DS address in the compiled sources, 7- or 8-digit (it reports
  the pre-1905 files and does not report the `#else` copies). It is part of
  `make test`: **345 tests** now.
- **Checked.** The 3DS ELF's `LoadChar` loads `NitroPort_IoReg` (0x00d889e0)
  from the literal pool instead of moving 0x04000000, and
  `BattleSystem_SetBackground` uses the port's VRAM arrays; the scanner
  reports 0.
  The battle itself could not be replayed on the PC yet: the scripted new game
  has no starter (the west exit of New Bark Town is blocked), so reaching tall
  grass needs a field save. The fault site and the replacement are verified;
  the hardware run is the end-to-end check.
- **Trace.** [agent-notes/s15-ds-address-literals.md](agent-notes/s15-ds-address-literals.md)
  has the log decode, the site table, the preprocessor method and what is unverified.
- **Counts.** **82 patches**; the DS gate last ran at 78 and 1905's `#else`
  text is unchanged. Artifact **9,278,840 B**
  (`sha256 168bc1a2f49becc022cad3d017feff62191308e4242d49cdec142a24feac76b5`),
  1092/1092, `3dsxdump` rc 0 (CODE 2126, RODATA 112, DATA 13, BSS 4446 pages).

### 2026-11-01 (session 11) — closing the in-game Settings crashed

- **Symptom.** In the field, X → **OPTIONS** → **QUIT** crashed instead of
  fading back to the field. Every other start-menu app returned.
- **Reproduction.** No field save existed, so the PC runner played the new
  game through the intro (scripted: bedroom → downstairs → mother's dialogue)
  to the field, then X → OPTIONS (touch `125,115`) → QUIT (touch `222,183`).
  It aborted at frame 35028, `rc 139`, fault pc in `Transfer()`
  (`port_dma.c`), with `src=0x02C002C0`, `dest=0x0E8A1D80`, `count=352`.
  A temporary guard in `Transfer()` plus `backtrace()` (the container forbids
  `ptrace`, so gdb cannot attach) gave the stack:
  `OptionsApp_OnVBlank → DoScheduledBgGpuUpdates → LoadBgVramScr →
  GXi_DmaCopy16 → MIi_DmaSetParams_wait → NitroPort_DmaStart → Transfer`.
- **Cause.** `OptionsApp_Free` runs twice: `setupAndFreeState == 0` frees the
  `BgConfig` and the app's buffers, and only `== 1`, one frame later, stops
  `Main_SetVBlankIntrCB`. The port raises that frame's V-blank in between, so
  `OptionsApp_OnVBlank` used the freed config; the NNS free-list header had
  scribbled `bufferTransferScheduled` and the `bgs[]` tilemap pointers, and a
  "scheduled" layer started a DMA from a bogus pointer. The DS reads that RAM
  (one frame of garbage into VRAM); the port's DMA treats it as a host
  pointer. The decomp is matching — this is a latent game bug the DS survives.
- **Fix.** Patch **1904** clears the callback in the same call that frees the
  buffers (port-only `#ifdef SDK_PORT_3DS`; `#else` unchanged). The PC repro
  now returns to the field at frame 35032 and survives two more open/close
  cycles. Details: [../wiki/3DS-Pitfalls.md](../wiki/3DS-Pitfalls.md#the-v-blank-survives-the-app-that-owned-its-buffers).
- **Counts.** The tree is at **79 patches**; the DS gate last ran at 78 and
  1904's DS text is unchanged. The full trace and the reproduction are in
  [agent-notes/s14-options-vblank.md](agent-notes/s14-options-vblank.md).
- **Artifact.** 3DS build **1092/1092**, 0 unresolved; `.3dsx`
  **9,278,712 B** (`sha256 18833d57c6a84dee990008ddabef5b443207c01d59193d284f46c58553bc9ebb`),
  `3dsxdump` rc 0 (CODE 2126, RODATA 112, DATA 13, BSS 4446 pages). In the
  ELF, `OptionsApp_Free` calls `Main_SetVBlankIntrCB` before
  `GF_DestroyVramTransferManager`, as intended.

### 2026-11-01 (session 10, audio + 3D) — the output level, NDSP staging and the camera-plane drop

- **Audio.** After 1407 the note still held until the next one and the mix
  clipped. The cause was not the ARM7 or the mixer: NitroSystem's
  `_0210D750` (player volume 0..127 -> decibels) is an empty alias section in
  the decomp (`lib/asm/nitro_sdk_data.s`), which the DS link order resolves to
  `SNDi_DecibelTable` but the port's order landed on a pointer table. NNS read
  pointer bytes as decibels: the external fader became +25.6 dB, every channel
  clamped to 0 dB, and `SND_ExChannelMain`'s release stop (`volume <= -723`)
  could never fire. A PC trace showed 14 channels at 0 dB and 4 natural ends
  in 90 s; after the fix, volumes 41-103, **192** ends, no saturated 50 ms
  window, and real quiet gaps.
- **Fix:** `hgss/compat/src/nns_volume_table.c` supplies the real table;
  `tools/hgss_game.py` drops `.rodata.alias_d750` from the SDK data object and
  checks the linked symbol (512 bytes, entry 128 = 0 dB; see
  [decisions 2026-11-02](../decisions.md)). `tests/test_hgss_game.py` keeps
  entries 0..127 equal to `SNDi_DecibelTable`, 128..255 equal to the DS tail,
  and checks the build. The DS gate is untouched.
- **Still clipped at low volume.** The notes were right, but the user heard
  clipping that survived turning the console down, so it was in the samples.
  A retail title captured from melonDS and calibrated with a full-scale tone
  peaks at ≈0.42 (RMS 0.071); the port's title peaked at 1.0 (RMS 0.139), about
  6 dB hot. `NitroSound_Mix` now applies the 6 dB headroom **before its 16-bit
  saturation** (`outputShift`); the earlier `NitroPort_SoundRender` trim ran
  after saturation and left 18-44 flat-top runs at ±16384 in the render.
  PSG duty 7 is also silence now (GBATEK's 0%), not a 100% DC level.
- **Still rough after the level fix.** The corrected mix was still "rough,
  crunchy" on hardware. `trim.wav` (the same mixer bytes) measured clean
  except those flat tops, so the roughness was in the NDSP staging: HGSS's
  shell never flushed a rendered block before `ndspChnWaveBufAdd` (the DSP's
  DMA read stale FCRAM), and it asked the 32728.5 Hz DSP to resample the
  32768 Hz stream (libctru's 2-tap interpolator AMs the top octave at
  39.5 Hz). `main_3ds.c` flushes each 2048-byte block and announces
  `NDSP_SAMPLE_RATE`; the price is 0.12% / two cents flat. An independent
  ADPCM/interpolation audit found no mixer defect and added exact tests.
- **3D.** A real DS title capture (melonDS, retail ROM) showed Ho-oh intact;
  the PC reference itself was deformed. The `(0,0,0,0)` vertex is on the
  camera plane: both edges to a rim vertex project to that rim point, so the
  triangle has **zero screen area** and the DS stores no polygon. The software
  rasteriser took its screen `(0,0)` literally and fanned the wing. Dropping
  only those triangles in `stage_triangle()` renders a complete bird (both
  wings, body, tail); the earlier `w <= 0` drop also removed real geometry.
  The `w = 1` / `w = 2^18` vertex workarounds are reverted; the new test
  `test_camera_plane_triangle` asserts no polygon is stored.
- **Hardware is still the owner's run:** the bird must be intact (no fan, no
  gap) and notes must fade without clipping or roughness. Local artifact:
  `.3dsx` **9,278,696 B** (`sha256 fc4472b5…68a7`), 3DS build **1092/1092**, 0
  unresolved, `3dsxdump` rc 0 (CODE 2126, RODATA 112, DATA 13, BSS 4446
  pages); staged as `sdmc:/3ds/pokeheartgold.3dsx` (the previous `4cecadd0…`,
  the audio-only `63d880e7…` and the original `3f493661…` are beside it for
  rollback).
  [agent-notes/s13-nns-volume-table.md](agent-notes/s13-nns-volume-table.md) ·
  [3DS-Pitfalls](../wiki/3DS-Pitfalls.md)

### 2026-11-01 (session 9, audio) — the hung noise note

> **Follow-up in session 10.** Patch 1407 fixed a real noise-register bug but
> was not the held-note cause: NitroSystem's `_0210D750` volume table was
> linked to a pointer table, adding +25.6 dB and making the release stop
> unreachable. See the session 10 note and
> [agent-notes/s13-nns-volume-table.md](agent-notes/s13-nns-volume-table.md).

- **Symptom:** a note sustained until the next note, and individual sounds and
  slower songs/percussion distorted. The cause was in the ARM7 driver, not the
  mixer: patch 1405 made `NitroPort_SoundRegisters` absolute (channel *c* at
  `0x400 + 16c`, timer at `0x408 + 16c`) but missed the two direct writes in
  `SND_SetupChannelNoise` (`sound_channel.c:127,133`), which still used the
  pre-1405 relative index. A noise note on 14/15 therefore wrote
  `0x040000E0`/`0x040000E8`, an unused hole, and left the channel's previous
  PCM control word (enable + repeat) for the mixer to keep looping until the
  next PCM setup.
- **Fix:** patch **1407** writes `0x400 + channelOffset` and
  `0x408 + channelOffset`, matching the Psg branch. The `#else` DS text is
  unchanged; `gcc-10 -E -P` without `SDK_PORT_3DS` is byte-identical (0 diff
  lines). A grep confirms no other `NitroPort_SoundRegisters` access lacks its
  `0x400`/`0x500` base.
- **Test:** `tests/sdk/arm7_sound` leaves a running looped PCM on channels
  14/15, calls the noise setup, and checks `0x4E0/0x4E8` and `0x4F0/0x4F8`
  hold the noise word (format 3, enable and repeat clear) while `0x0E0/0x0E8`
  and channel 0 keep their sentinels. Without 1407: 24 failures, including the
  stale `0x88400040` control; with it: 122 checks, 0 failures.
- **Local (commit `2110dc8`):** `bootstrap --clean` applies **78 patches**; `make test` exit 0
  (**329 Python tests** + 20 SDK suites); 3DS build **1092/1092**, 0
  unresolved, `.3dsx` **9,278,592 B** (`sha256 d6c56cf5…c4b`), `3dsxdump` rc 0
  (CODE 2126, RODATA 112, DATA 13, BSS 4446 pages). **No hardware result:** the
  next log and ears should show no note hanging into the next one and
  `audio: N dropped frames of M` near zero.
  [phase-5.md](phase-5.md) · [3DS-Pitfalls](../wiki/3DS-Pitfalls.md)

### 2026-11-01 (session 8, follow-up 2) — the 60 Hz frame cap

- **The log-9 run ran at 87.0 Hz.** `tools/frametime_report.py` over 40
  samples: mean `total_us` **11.49 ms**, `logic_us` under 1 ms. The emulated
  DS's V-blank is raised once per `NitroPort_OnFrame`, and `gfxSwapBuffers`
  only waits while the previous frame is still being presented; once the
  composite fit under one refresh the loop free-ran, so the game and its
  cutscenes ran 1.45x fast.
- **The shell caps frame starts at 60 Hz (commit `952bdf2`).** `WaitForFrameCap` holds a
  `SYSCLOCK_ARM11 / fps` tick grid with `svcGetSystemTick()` and sleeps only
  the remainder; a frame that already overran waits for nothing, and one more
  than a period behind re-anchors the grid. `DrawFrame` and its swap stay
  unconditional and before the wait. `port.ini`'s `fps` takes 60 (default) or
  30 (comparison: half game speed, because the game clock is per frame);
  invalid values use 60. Startup logs `[perf] frame cap 60 Hz`.
- **Expected next log:** mean `total_us` near **16,700** (the period is
  4,468,530 ticks = 16,667 us) rather than 11,490, with `draw_us` unchanged.
- **Local:** `make test` **329 tests, OK** plus the `tests/sdk/*` suites, exit
  0; 3DS build 1092/1092, 0 unresolved; `.3dsx` **9,278,584 B**; `3dsxdump`
  rc 0 (CODE 2126, RODATA 112, DATA 13, BSS 4446 pages). Hardware timing is
  the owner's run.
  [phase-5.md](phase-5.md) · [3DS-Pitfalls](../wiki/3DS-Pitfalls.md)

### 2026-11-01 (session 8, follow-up) — the 3D works; the worker was starved

- **Ninth hardware run (log-8).** The staged pipeline works: the lower-screen
  title shows Ho-oh, the game reaches the field, and the player walks through
  the house and outside to Lyra and Marill. Two defects and a frame rate
  problem showed in the same log.
- **The 3D updated at ~1 fps.** `3D images: 105` over 4,680 frames, while the
  geometry thread made 120 scenes per 240 frames. The GPU thread was on the
  game's core below the game, so the 20-30 ms software composite left it only
  the V-blank wait, and a long frame has none. It moved to **core 2** on a New
  3DS (core 0 otherwise), and the app objects now build at `-O2`.
- **Ho-oh's black fan.** The port's six-plane clip keeps a `(0,0,0,0)` vertex;
  the PICA's divide by `w` then puts it at the screen centre. HGSS drops the
  affected stored triangles in `hgss/game/app/pica_scene.c` (`camera-plane
  drops` in the log) until the port's clipper gets a `w > 0` near plane.
  **(Superseded twice: the drop left a transparent gap; the `w = 1` fix only
  changed the fan's shading; the final fix drops the zero-area camera-plane
  triangles in the port's clipper, session 10.)**
- **Audio.** `82 dropped frames of 23821` (0.34%) and a slow intro; the audio
  thread was below the game and shared core 0 with the starved GPU worker. It
  is now priority `0x2e`, above the game and geometry.
- **Local:** build 1092/1092, 0 unresolved, `.3dsx` 9,277,656 B, `3dsxdump`
  rc 0; `make test` 327 tests, OK; `make port-tests` 17,122 checks, 0 failed;
  the host `tests/game/pica_scene/run.sh` passes. The re-run is the owner's.
  [agent-notes/s10-gpu-followup.md](agent-notes/s10-gpu-followup.md)

### 2026-11-01 (session 8) — the 3D GPU pipeline

- **The black 3D had a missing worker, not a missing composite.** On the 3DS
  `NitroPort_Gx3dAcquireImage` only converts an image a GPU worker handed over;
  with none, `sPublished` stayed `-1` and engine A drew 2D only. The shell now
  runs Platinum's classic path: a geometry thread (`NitroPort_Gx3dStartThread`
  + `NitroPort_Gx3dThreadMain`, one `NitroPort_Gx3dFlush` per frame) and a GPU
  thread (`NitroPort_PicaInit`, acquire → `NitroPort_PicaRender` → release →
  `NitroPort_Gx3dConvertPending`), woken by a strong
  `NitroPort_Gx3dScenePublished`. Both failures log and fall back to 2D.
- **Build:** `app_sources("3ds")` now carries `port_gx3d_pica.c` and
  `port_pica_abi.c` (the latter without `-fno-short-enums`), and the harness
  builds the three `.v.pica` shaders with `picasso` + `bin2s` +
  `arm-none-eabi-as`. Both tools were already in devkitPro: no new dependency.
  `pica_classic_stubs.c` supplies the two compose-only profiler hooks.
- **Verified:** `make test` **327 tests, OK** plus 21 SDK suites;
  `make port-tests` **17,122 checks, 0 failed**, exit 0; 3DS build
  1092/1092, 0 unresolved, `.3dsx` **9,300,752 B**, `3dsxdump` **rc 0**
  (CODE 2131, RODATA 113, DATA 13, BSS 4446 pages); the PICA symbols are `T`.
- **Pending the owner's hardware run:** whether the room/player sprite and the
  intro 3D now draw, a rising `3D images:` counter, and the after frame-time
  log (before: log-7, 2 samples, 24.7 ms / 40.5 Hz, 3D absent).
  [agent-notes/s9-gpu-pipeline.md](agent-notes/s9-gpu-pipeline.md) ·
  [phase-5.md](phase-5.md)

### 2026-11-01 (session 7) — PC build reaches Oak's speech

- **Four boot defects fixed** at the layer that explains them: patch 1801 (literal DS header-mirror
  addresses), patch 1802 (main-menu work-area BSS alias), `PM_KEEP_ASSERTS` in the harness (GF_ASSERT
  side effects), and the runner's own crash/watch plumbing. Each patch has a `gcc -E -P` identity
  proof for the `#else` text.
- **Runner parity:** `--stop-at-missing-file`, `--stall-seconds`, `--screenshots`,
  `--screenshot-every`, `--press`, `--touch`, `--wav`, `--dump-3d`, `--trace-pxi`, `--trace-irq`,
  `--events`. `pc_trace.c` uses linker wraps so `3ds_port/` stays byte-identical.
- **`tools/boot_smoke.py` + tests**: synthetic pak reports `missing_file a/0/1/6` at frame 0; the
  real pack reports `frame_limit` with boot stage `overlay:intro_title`.
- **Golden runner**: `tools/golden/run.py` replays a scene, aligns on the first matching frame and
  writes JSON/HTML/heat maps; 2D exact, 3D 8/255 up to frame 6600. No melonDS goldens yet, so a run
  without a reference is a regression baseline only.
- **Sanitizer**: `--sanitize` builds; the replay found two real out-of-bounds reads (patches 1901,
  1902) and the ASan link needed the MSL `std::__new_handler`. The overlay loader's bulk `.data`
  restore is a known ASan global-redzone artifact; the full replay runs with `report_globals=0` while
  heap/stack stay on.
- **3DS shell**: Phase A remaster display (400x240/320x240 compositor, native-resolution 3D,
  1.25x touch mapping, no 1:1 fallback), D-pad/circle-pad input, APT pause, crash log, optional
  NDSP, scripted-input replay and a generic SMDH. `.3dsx` packages; `.cia` is blocked on
  makerom/bannertool (absent; user decision). Frame-time lines and `tools/frametime_report.py`
  exist; the Old/New 3DS hardware budget table is pending.
- **End-to-end:** real HG pack, no save, 20,000 frames: logo, full movie, title, main menu, Oak's
  speech (portrait/text/touch buttons), gender, naming keyboard, confirmation, first bedroom. No
  crash or stall. Calibrated `tests/inputs/{boot-title,main-menu-nosave,newgame-buttons}.txt`.
- Open: golden captures (user), a written save + melonDS load, upstream drafts (none new: the fixes
  are HGSS game code, not generic port code), 3DS hardware runs and the CIA tool decision.
  [phase-4.md](phase-4.md) · [phase-5.md](phase-5.md)

### 2026-10-31 (session 6) — PC and 3DS link clean, Phase 3 complete

- **The last 34 host symbols closed.** `object_aliases.py` stage (Pokéwalker five), bit-pattern MSL
  runtime + patch 1760 for the three float-form TUs, six explicit C++ runtime definitions, the MB
  stand-ins, and patch 1761 for the two duplicate globals. **0 unresolved**; duplicate census 0.
- **Real-binary gates:** `--check-overlays` exit 0 (129 overlays, 0 failed); `symbol_crosscheck.py`
  matched 21,407 / allowlisted 3,328 / **missing 0**, with an audited, wildcard-free allowlist.
- **3DS:** `std_sprintf`/`wm_core` va_arg fixes (1701/1702), the GX type fix (1703), the ARM GAS
  `%progbits`/`%object` compat fix, 41 devkitARM-only files fixed as types (1710-1712), overlay IDs
  assembled as numbers and `.version` merged into `.rodata` for 3dsxtool. **1092/1092 compile, both
  archives build, 0 unresolved, `pokeheartgold.3dsx` packaged (9,103,728 B).**
- **Phase 3:** `verify_rom.py`, `extract_rom.py`, `pack.py` (HG/SS US, build ID
  `heartgold-nitrofs-1`), the `tests/sdk/pak_boot` suite against the real reader, the SD layout doc,
  and synthetic-image tests. The real HG pack (513 files, 125,556,080 B) opens in `pokeheartgold-pc`;
  every boot error maps to words. The vendored reader's Platinum-only SHA-1 list is handled by a
  generated build-private copy, so `3ds_port/` stays byte-identical.
- **DS gate:** patch 1603 regenerates the MSL shape fixtures and re-pins them; patch 1762 re-pins the
  `g3imm.h` hash after 1703; patch 1763 re-pins the MSL selectors' producer-source hashes. At 72
  patches both matching builds reproduce the retail ROMs: HeartGold `4fcded0e2713dc03929845de631d0932ea2b5a37`
  and SoulSilver `f8dc38ea20c17541a43b58c5e6d18c1732c7e582`. **Phase 1's gate is passed.**
- Full audit: [agent-notes/p2-session6.md](agent-notes/p2-session6.md).

### 2026-10-31 (session 5) — Phase 1 suites green, game compiles completely, link 966 → 34

- **Phase 1 fixed at the root.** `test_arm7` was two NitroSDK revisions disagreeing about `sFreeList`
  (patch 1404); `libnitrosystem.a`/`test_nitrosystem` needed the compat headers plus a real `.bss`
  layout fix in `gfd_linked_list_tex_vram_man_reset.c` (NitroSystem 0303). The newly reachable
  suites then exposed the sound register offsets/volume table (1405), 3D texture VRAM (1406) and the
  `g3imm` direct writes. **`make port-tests` exit 0; nitrosystem 17,122 checks, 0 failed.**
- **Phase 2 compile complete**: patches 1509–1525. **1092/1092 files compile, 0 failed**; 150/150 `.s`.
- **Library set**: all 21 `lib/NitroDWC/asm` data objects link except four that reference a non-TLS
  `errno`; `lib/syscall/src/_secure_*.c` are excluded. Link: **34 unresolved** (was 966).
- **DS gate**: five tools pin inputs (1601/1602), the gate must run at `/work/pokeheartgold`, and a
  debug-info shape pin was still open.
- Full audit: [agent-notes/p2-wave-a-session5.md](agent-notes/p2-wave-a-session5.md).

### 2026-10-31 (session 4) — Phase 1 patch stack finished, vendored suites running, Phase 2 tooling + harness

- **Patches 1203, 0303, 1303, 1304, 1305, 1401-1403, 1501-1508, 1601** (39 total): GXFifoStat guard,
  `MATH_CountPopulation` 32-bit return, 2D VRAM loader literals, RTC/touch/PM/MIC shared-work
  literals, ARM7 sound driver + static init, the sound NULL-tag guard, the Phase 2 game GCC fixes,
  and the two alarm-partition header pin updates.
- **`tools/hgss_nitro.py` finished**; **Phase 2 started**; 1044/1094 files compile, 966 unresolved.
- **Process traps recorded**: Mac vs container `build/`; patches without `--relative` double paths;
  the gate needs `mwasmarm_patcher` first; two build tools pin header hashes.

### 2026-10-31 (session 3, Wave 1) — Compat headers, patch 1202, sdk-diff complete, libnitro.a green

- Wrote 20 compat headers; added patch **1202**; completed `docs/sdk-diff.md` and the SDK difference
  wiki. Verified `bootstrap --clean` (16 patches), `make test`, `port_compile_survey.py` 46/46 and the
  host32 `libnitro.a`.

### 2026-10-31 (later session) — W3 landed; 1302; host suites back on the port arrays; docs

- Landed patch **1201**; rebootstrapped; fixed the DMA register literals (1302); `make test` green.
- Measurements: `make test` 11 suites + 19 Python tests; host32 port build 44 errors in 9 objects.

### 2026-10-30 — Phase 0 closed, Phase 1 mostly built

- Phase 0: container, matching HG/SS builds, reference artifacts, savetool, Platinum sanity build.
  Open: golden captures, saves, determinism, native-macOS control.
- Phase 1: patches 0101-0901, NNS 0301, 1001, 1101; survey 270/284; layout probe 0 differences;
  DS gate passed with 10 patches; 9 SDK suites green. Port wiring started.
