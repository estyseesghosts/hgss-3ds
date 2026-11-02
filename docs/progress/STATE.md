# Current state

Last verified: **2026-11-02 (session 13: the battle's Poképics and the close hang)**, inside the dev container
(`/workspaces/pokeheartgold-3ds`), with the tree at **82 patches** (`NitroSDK` 16, `NitroSystem` 4,
`pokeheartgold` 62). Everything below that says "verified" was run on that tree on that date, and the
DS-side gate passed at 78 (HG and SS). If you change the patch stack, rerun the commands and update
this page; do not trust it blindly.

**One line:** Phase 2's exit criterion is met — the PC program links with **0 unresolved**, the overlay
self-check passes on the real binary (129/0), the symbol cross-check reports **0 missing**, and the 3DS
program compiles 1092/1092, builds both archives and packages `pokeheartgold.3dsx`. Phase 3's data
pipeline is complete: verify/extract/pack, the boot-time pak check and its tests, and the SD layout doc.
**Both halves of the Phase 1 DS gate pass at 78 patches**; both matching builds reproduce the retail
ROMs byte for byte (HG `4fcded0e…b5a37`, SS `f8dc38ea…e582`). Patches 1407, 1904, 1905, 0304 and 0605 are
`SDK_PORT_3DS`-only (`#else` text unchanged), and the gate is the evidence.

## Phases

| Phase | State | Evidence |
| --- | --- | --- |
| 0 Baseline | **Done**, open items | Matching HG/SS ROM SHA1s in the container. Never done: golden captures, fixture saves, the determinism run, a native-macOS control. [phase-0.md](phase-0.md) |
| 1 SDK patch stack | **Done** | 82 patches apply from scratch (the DS gate last ran at 78; 1904/1905 are port-only); `make test` 354 Python tests + 20 SDK suites; `make port-tests` green; both archives build; the DS gate at 78 reproduces both retail ROMs. [phase-1.md](phase-1.md) |
| 2 Game code under GCC | **Done for the exit criterion** | **0 unresolved** on host32 and 3DS; `--check-overlays` 129/0; `symbol_crosscheck.py` missing 0; `pokeheartgold-pc` and `pokeheartgold.3dsx` exist. [phase-2.md](phase-2.md) |
| 3 Data pipeline | **Done** (SS tested synthetically) | `builder/verify_rom.py`, `extract_rom.py`, `pack.py`; boot check with clear errors; `docs/wiki/SD-Card-Layout.md`; synthetic-image tests; real HG pack opens in the PC program. [agent-notes/p2-session6.md](agent-notes/p2-session6.md) |
| 4 Boot to Oak | **In progress** | PC build boots cold → logo → movie → title → main menu → Oak's speech → naming → first field frames. On a New 3DS the same sequence reaches the bedroom/field with 2D, audio and the PICA 3D layer. Goldens, save round-trip, sanitizer and PR drafts open. [phase-4.md](phase-4.md) · [phase-5.md](phase-5.md) |
| 5 3DS shell | **In progress; runs on hardware** | GPU worker, 60 Hz cap, the NNS volume table, pre-saturation 6 dB headroom, NDSP cache flush/native rate, the camera-plane drop, the battle Poképics (0304/0605) and the close-hang fix are in the latest artifact; hardware re-run pending. |
| 6-11 | Not started | Graphics gaps, sound streams, gameplay, IR, wireless, GPU 2D. See [../wiki/Architecture.md](../wiki/Architecture.md). |

## Verified today

Run from the repo root inside the container. `devcontainer exec --workspace-folder . bash -c 'cd
/workspaces/pokeheartgold-3ds && <command>'` runs any of them from the Mac.

| Check | Command | Result |
| --- | --- | --- |
| Patch stack applies from scratch | `python3 tools/bootstrap.py --clean` | `Done: build/upstream (82 patches applied)` |
| Host tests | `make test` | **exit 0**; **354 Python tests** + **20 SDK suites** |
| Vendored port tests | `make port-tests` | **exit 0**: pak 120, the whole nitro sequence (sound chip 81), nitrosystem **17,122 checks, 0 failed**; gx3d **327 + 229** |
| Port sources vs HGSS headers | `python3 tools/port_compile_survey.py` | last full run 46/46; `port_gx3d.c`'s camera-plane drop compiles in the port build and `make port-tests` |
| Game compile (host32) | `python3 tools/hgss_game.py --target host32 --keep-going --out /work/slice-3d-pc -j4` | **1092/1092 compiled, 0 failed**; 150/150 `.s`; **linked, 0 unresolved** |
| Overlay self-check | `build/pokeheartgold-pc --check-overlays` | **exit 0**; `[check] 129 overlays (2 with static initializers), 0 failed` |
| Symbol cross-check | `python3 tools/symbol_crosscheck.py --pc build/pokeheartgold-pc --reference /work/pokeheartgold.ref/build/heartgold.us/main.elf` | matched 21,407 · allowlisted 3,328 · **missing 0** |
| Settings open/close (PC) | scripted new game → bedroom → mother → X → OPTIONS → QUIT | **returns to the field at frame 35032**; two further cycles pass. Before patch 1904 it aborted at 35028 in `LoadBgVramScr` (`src=0x02C002C0`) |
| Raw DS addresses | `python3 -m unittest tests.test_hw_addresses` | **7 tests, OK**; 0 raw `0x02000000`-`0x07FFFFFF` pointer literals in `src/` (20 before patch 1905, including the battle read and the background copy) |
| Game compile (3DS) | `python3 tools/hgss_game.py --target 3ds --keep-going --out /work/final3-3ds -j8` | **1092/1092, 0 failed**; 150/150 `.s`; **linked, 0 unresolved**; `pokeheartgold.3dsx` **9,279,628 B** (`sha256 6bad8620…303f`); `3dsxdump` rc 0 (CODE 2126, RODATA 112, DATA 13, BSS 4446 pages) |
| Battle Poképics (PC) | owner's `heartgold.sav` + the zig-zag input, `--frames 14000` | both Pokémon render; each battle scene is **720 tris (720 opaque), lost: 0 clipped**; before 0304/0605, `0 vtx` and then `360 clipped` |
| Close from HOME (3DS) | HOME menu → Close | fixed in the shell (`[quit]` steps); the console run is the owner's check |
| 3DS archives | same command | `libnitro.a` + `libnitrosystem.a`, 0 errors |
| 3DS PICA/symbols | `arm-none-eabi-nm /work/slice-3d/pokeheartgold-3ds.elf \| grep -E 'NitroPort_Pica\|Gx3dThread'` | `T` for `NitroPort_PicaInit/Render/Exit`, `NitroPort_Gx3dThreadMain/Flush/ConvertPending`, and a strong `T NitroPort_Gx3dScenePublished`; the HGSS drop symbol is gone |
| Data pipeline (real HG dump) | `builder/{verify_rom,extract_rom,pack}.py` | 513 files / 125,549,829 B extracted; 125,556,080 B pack; opens in `pokeheartgold-pc`; wrong build ID/version/corrupt/SHA-1 give the mapped messages and exit 2 |
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

- **SoulSilver has no retail ROM in the container.** `verify_rom.py` accepts its SHA-1,
  extraction/packing are image-driven, and the synthetic tests cover the layout; the real SS run is
  an open gap. The shell is a compile-time `HGSS_SS_VARIANT`, but there is no SS data pack/ROM to
  exercise it here.
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

1. **Phase 4:** finish the exit items — a scripted save and its `savetool.py` check, the sanitizer
   replay, the golden comparison once the user's captures exist, and the upstream drafts. The boot
   itself now reaches Oak's speech and the first field (see [phase-4.md](phase-4.md)).
2. **Phase 5:** confirm the GPU pipeline, the camera-plane 3D fix, the 60 Hz frame cap and the
   NNS-table/6 dB-trim audio fixes on hardware (room, player sprite and intro 3D with no gap/fan;
   rising `3D images:`; mean `total_us` near 16,700; no clipped/hung notes; `audio: N dropped
   frames` near zero), then the remaining shell items (input, SMDH and a hardware run).
3. Re-run the SDK survey (`tools/sdk_compile_survey.py`); the port survey is 46/46 at 78 patches.
4. SoulSilver: the builder accepts the retail SHA-1 and the tests are image-driven, but no SS ROM was
   available for an end-to-end pack/open; record it when a dump is available.

## Open decisions

- **Cross-check allowlist granularity:** it is now 3,398 explicit rows with reasons (no wildcards).
  The alternative (a curated wildcard table) is smaller but can hide a source file; the explicit form
  is kept and audited. Revisit with the cross-check's comparison model if the table becomes unwieldy.
- **PC program boot depth:** the Phase 2 binary is a link/overlay-check target, not a playable build;
  Phase 4 owns the first frame. No decision needed now.
- **SoulSilver folder and pak name** (`sdmc:/3ds/pokeheartgold-ss/`, `soulsilver3ds.pak`) are documented
  in `docs/wiki/SD-Card-Layout.md` and implemented by `scripts/prepare_game.py` (build ID
  `soulsilver-nitrofs-1`); the 3DS shell still hardcodes the HeartGold `DATA_DIR` and constants, so it
  must gain the SS names when that build exists.
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
python3 tools/bootstrap.py            # "already at ... with 82 patches" if in sync
make test                            # exit 0 (354 tests + 20 SDK suites)
make port-tests                      # exit 0 (pak + nitro + nitrosystem)
python3 tools/hgss_game.py --target host32 --keep-going --out /work/pc -j5
                                     # 1092/1092, 0 unresolved, binary exists
python3 tools/hgss_game.py --target 3ds --keep-going --out /work/3d -j5
                                     # 1092/1092, 0 unresolved, .3dsx packaged
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
  checks the linked symbol (`[sound] _0210D750 -> nns_volume_table.c
  (256 bytes)`). `tests/test_hgss_game.py` keeps it equal to `SNDi_DecibelTable`
  and the build. The DS gate is untouched.
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
- **3DS shell**: centered 1:1 display, D-pad/circle-pad input, APT pause, crash log, optional NDSP,
  scripted-input replay and a generic SMDH. `.3dsx` packages; `.cia` is blocked on makerom/bannertool
  (absent; user decision). Frame-time lines and `tools/frametime_report.py` exist; the hardware
  budget table is pending.
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
