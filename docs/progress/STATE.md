# Current state

Last verified: **2026-11-01 (session 7)**, inside the dev container (`/workspaces/pokeheartgold-3ds`), with
the tree at **76 patches** (`NitroSDK` 15, `NitroSystem` 3, `pokeheartgold` 56). Everything below that
says "verified" was run on that tree on that date. If you change the patch stack, rerun the commands
and update this page; do not trust it blindly.

**One line:** Phase 2's exit criterion is met — the PC program links with **0 unresolved**, the overlay
self-check passes on the real binary (129/0), the symbol cross-check reports **0 missing**, and the 3DS
program compiles 1092/1092, builds both archives and packages `pokeheartgold.3dsx`. Phase 3's data
pipeline is complete: verify/extract/pack, the boot-time pak check and its tests, and the SD layout doc.
**Both halves of the Phase 1 DS gate pass** at 72 patches: the matching builds reproduce the
retail HeartGold and SoulSilver ROMs byte for byte.

## Phases

| Phase | State | Evidence |
| --- | --- | --- |
| 0 Baseline | **Done**, open items | Matching HG/SS ROM SHA1s in the container. Never done: golden captures, fixture saves, the determinism run, a native-macOS control. [phase-0.md](phase-0.md) |
| 1 SDK patch stack | **Done** | 72 patches apply from scratch; `make test` 277 Python tests + 21 SDK suites; `make port-tests` (see below); both archives build; the DS gate's three stale pin classes are fixed in 1603/1762/1763, and both matching builds reproduce the retail ROMs. [phase-1.md](phase-1.md) |
| 2 Game code under GCC | **Done for the exit criterion** | **0 unresolved** on host32 and 3DS; `--check-overlays` 129/0; `symbol_crosscheck.py` missing 0; `pokeheartgold-pc` and `pokeheartgold.3dsx` exist. [phase-2.md](phase-2.md) |
| 3 Data pipeline | **Done** (SS tested synthetically) | `builder/verify_rom.py`, `extract_rom.py`, `pack.py`; boot check with clear errors; `docs/wiki/SD-Card-Layout.md`; synthetic-image tests; real HG pack opens in the PC program. [agent-notes/p2-session6.md](agent-notes/p2-session6.md) |
| 4 Boot to Oak | **In progress** | PC build boots cold → logo → movie → title → main menu → Oak's speech → naming → first field frames, no crash through 20,000 frames. Runner parity, `boot_smoke.py`, patches 1801/1802 and `PM_KEEP_ASSERTS` landed. Goldens, save round-trip, sanitizer and PR drafts open. [phase-4.md](phase-4.md) |
| 4-11 | Not started | See [../wiki/Architecture.md](../wiki/Architecture.md). |

## Verified today

Run from the repo root inside the container. `devcontainer exec --workspace-folder . bash -c 'cd
/workspaces/pokeheartgold-3ds && <command>'` runs any of them from the Mac.

| Check | Command | Result |
| --- | --- | --- |
| Patch stack applies from scratch | `python3 tools/bootstrap.py --clean` | `Done: build/upstream (72 patches applied)` |
| Host tests | `make test` | **exit 0**; **312 Python tests** + **21 SDK suites** |
| Vendored port tests | `make port-tests` | **exit 0**: pak 120, the whole nitro sequence, nitrosystem **17,122 checks, 0 failed** |
| Port sources vs HGSS headers | `python3 tools/port_compile_survey.py` | last run 46/46 at 61 patches; sources unchanged since |
| Game compile (host32) | `python3 tools/hgss_game.py --target host32 --keep-going --out /work/final-pc -j5` | **1092/1092 compiled, 0 failed**; 150/150 `.s`; **linked, 0 unresolved** |
| Overlay self-check | `build/pokeheartgold-pc --check-overlays` | **exit 0**; `[check] 129 overlays (2 with static initializers), 0 failed` |
| Symbol cross-check | `python3 tools/symbol_crosscheck.py --pc build/pokeheartgold-pc --reference /work/pokeheartgold.ref/build/heartgold.us/main.elf` | matched 21,407 · allowlisted 3,328 · **missing 0** |
| Game compile (3DS) | `python3 tools/hgss_game.py --target 3ds --keep-going --out /work/final-3ds -j5` | **1092/1092, 0 failed**; 150/150 `.s`; **linked, 0 unresolved**; `pokeheartgold.3dsx` 9,103,728 B |
| 3DS archives | same command | `libnitro.a` + `libnitrosystem.a`, 0 errors |
| Data pipeline (real HG dump) | `builder/{verify_rom,extract_rom,pack}.py` | 513 files / 125,549,829 B extracted; 125,556,080 B pack; opens in `pokeheartgold-pc`; wrong build ID/version/corrupt/SHA-1 give the mapped messages and exit 2 |
| Save layout | `make layout` | last full probe: 0 differences in 804 values; layout patches did not change |
| DS-side gate (HG) | `tools/matching-build.sh /work/pokeheartgold HEARTGOLD` | **PASSED** at 72 patches: `build/heartgold.us/pokeheartgold.us.nds` SHA1 `4fcded0e2713dc03929845de631d0932ea2b5a37` (retail) |
| DS-side gate (SS) | same, `SOULSILVER` | **PASSED** at 72 patches: `build/soulsilver.us/pokesoulsilver.us.nds` SHA1 `f8dc38ea20c17541a43b58c5e6d18c1732c7e582` (retail) |

### Vendored suites: the full matrix

`make port-tests` runs, in order: `pak`, the whole `nitro` subproject, then `nitrosystem`. The
session-6 final run (72 patches): pak 120, `test_nitro` 702, `test_fs_pak` 120, threads 68, arm7 **676**,
sound_chip 51, sound_sequence 12, render 2071, gx3d 326 (+228), plumbing 231, vram 24, upscale 160,
lowermenu 246, options 49, xbrz 266, shadow 375, nitrosystem **17,122** — every suite 0 failed.

### What still fails, and why

**Nothing in the Phase 1/2/3 acceptance set.** The remaining gaps are deliberate deferrals or later
phases:

- **The PC program segfaults immediately after `NitroMain` starts** with a real pack (frame 0, under
  qemu). The pack opens, the firmware/overlay setup runs, then the game's own boot path hits code that
  Phase 4 has not ported. That is the Phase 4 work (headless boot to Oak's speech); it is not a Phase 3
  data-pipeline failure, and no missing-file or pack error is reported first.
- **SoulSilver has no ROM in the container.** `verify_rom.py` accepts its SHA-1, extraction/packing are
  image-driven, and the synthetic tests cover the layout; the real SS run is an open gap. There is no
  SS shell yet (`main_3ds.c` hardcodes the HG folder/name/game code).
- **3DS depth:** the `.3dsx` links and packages but nothing draws yet; screen routing, input mapping and
  the SMDH (icon/banner) are Phase 5.
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

## Next actions, in order

1. **Phase 4:** finish the exit items — a scripted save and its `savetool.py` check, the sanitizer
   replay, the golden comparison once the user's captures exist, and the upstream drafts. The boot
   itself now reaches Oak's speech and the first field (see [phase-4.md](phase-4.md)).
2. **Phase 5:** the 3DS shell's drawing, input, SMDH and a hardware run.
3. Re-run the SDK survey (`tools/sdk_compile_survey.py`); the port survey is 46/46 again at 72 patches.
4. SoulSilver: the builder accepts the retail SHA-1 and the tests are image-driven, but no SS ROM was
   available for an end-to-end pack/open; record it when a dump is available.

## Open decisions

- **Cross-check allowlist granularity:** it is now 3,398 explicit rows with reasons (no wildcards).
  The alternative (a curated wildcard table) is smaller but can hide a source file; the explicit form
  is kept and audited. Revisit with the cross-check's comparison model if the table becomes unwieldy.
- **PC program boot depth:** the Phase 2 binary is a link/overlay-check target, not a playable build;
  Phase 4 owns the first frame. No decision needed now.
- **SoulSilver folder and pak name** (`sdmc:/3ds/pokeheartgold-ss/`, `soulsilver3ds.pak`) are documented
  in `docs/wiki/SD-Card-Layout.md` and must be set together with a `soulsilver-nitrofs-1` build ID when
  the SS build exists.
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
python3 tools/bootstrap.py            # "already at ... with 72 patches" if in sync
make test                            # exit 0 (277 tests + 21 SDK suites)
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
| devkitPro tools | `3dsxtool`, `smdhtool` present | `.3dsx` packaging now, SMDH in Phase 5 |

The full toolchain/build reference is [wiki/Build-Reference.md](../wiki/Build-Reference.md).

### Log

Newest first. Each entry: what changed, what was run, what it proved.

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
