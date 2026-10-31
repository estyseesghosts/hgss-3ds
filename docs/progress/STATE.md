# Current state

Last verified: **2026-10-31 (session 5)**, inside the dev container (`/workspaces/pokeheartgold-3ds`), with the
code at working-tree commit `35f6235` and **61 patches**. Everything below that says "verified" was run on that
tree on that date. If you change the patch stack, rerun the commands and update this page; do not trust it blindly.

**One line:** Phase 1's test matrix is green (`make port-tests` exit 0, including `test_arm7` and
`test_nitrosystem`), the game compiles completely on host32, both port archives build, and the host link is
down to **34 symbols** from 966; the DS gate still has not passed (a debug-info shape pin is the last blocker),
and the 3DS side is still blocked by `std_sprintf`, the ARM7 C-file rule and ~45 devkitARM-only compile errors.

## Phases

| Phase | State | Evidence |
| --- | --- | --- |
| 0 Baseline | **Done**, open items | Matching HG/SS ROM SHA1s in the container. Never done: golden captures, fixture saves, the determinism run, a native-macOS control. [phase-0.md](phase-0.md) |
| 1 SDK patch stack | **Code complete, gate owed** | 61 patches apply from scratch; `make test` 204 tests + all SDK suites; `make port-tests` **exit 0** (pak, nitro, nitrosystem); `libnitro.a` + `libnitrosystem.a` build; DS gate not yet passed (last pass at 10 patches). [phase-1.md](phase-1.md), [agent-notes/p2-wave-a-session5.md](agent-notes/p2-wave-a-session5.md) |
| 2 Game code under GCC | **In progress, 34 symbols left** | 1092/1092 files compile, 150/150 `.s`, both archives link; **34 unique unresolved symbols** (MSL runtime, C++ runtime, MB stubs, 5 pokéwalker aliases); `--check-overlays` and `symbol_crosscheck.py` exist but no PC binary yet. [phase-2.md](phase-2.md) |
| 3 Data pipeline | **Partial** | `builder/pack.py` exists (used by the pak tests); the ROM extractor and manifest generation are not written. |
| 4-11 | Not started | See [../wiki/Architecture.md](../wiki/Architecture.md). |


## Verified today

Run from the repo root inside the container. `devcontainer exec --workspace-folder . bash -c 'cd
/workspaces/pokeheartgold-3ds && <command>'` runs any of them from the Mac.

| Check | Command | Result |
| --- | --- | --- |
| Patch stack applies from scratch | `python3 tools/bootstrap.py --clean` | `Done: build/upstream (61 patches applied)` |
| Host tests | `make test` | **exit 0**; **204 Python tests** + all 18 SDK suites (incl. the new arm9 flush suite) |
| Vendored port tests | `make port-tests` | **exit 0**: pak, all nitro suites, nitrosystem **17,122 checks 0 failed** |
| Port sources vs HGSS headers | `python3 tools/port_compile_survey.py` | **46/46** compile (**exit 0**) |
| Game compile (host32) | `python3 tools/hgss_game.py --target host32 --keep-going --out /work/main-g61` | **1092/1092 files compile, 0 failed**; 150/150 `.s`; both archives build |
| Game link (host32) | same command | **34 unique unresolved symbols** (was 966); no binary yet |
| 3DS compile | `python3 tools/hgss_game.py --target 3ds --keep-going ...` | ~1049/1094 compiled, ~45 fail (devkitARM-only); 3DS `libnitro.a` still blocked by `std_sprintf` |
| Save layout | `make layout` | last full probe: 0 differences in 804 values; layout patches did not change |
| DS-side gate | [wiki/Daily-Workflow.md](../wiki/Daily-Workflow.md#the-ds-side-gate) | **not passed** at the final stack. Last pass: 10 patches. 1601/1602 fixed five pin classes; a debug-info shape pin remains (§7 of the session note). |

### Vendored suites: the full matrix at 61 patches

`make port-tests` is now the single check (exit 0). It runs, in order: `pak`, the whole `nitro`
subproject, then `nitrosystem`. Every suite and its count:

| Suite | Result |
| --- | --- |
| `pak-backed NitroFS` | 120 checks, 0 failed |
| `test_nitro` | 702 checks, 0 failed |
| `test_fs_pak` | 120 checks, 0 failed |
| `test_threads` | 68 checks, 0 failed |
| `test_arm7` | **676 checks, 0 failures** (the old `test_sound` spin is fixed) |
| `test_sound_chip` | 51 checks, 0 failures |
| `test_sound_sequence` | 12 checks, 0 failures |
| `test_render` | 2071 checks, 0 failed |
| `test_gx3d` | 326 checks, 0 failures (+228 on its own thread) |
| `test_gx3d_plumbing` | 231 checks, 0 failures |
| `test_vram_transfer` | 24 checks, 0 failures |
| `test_upscale` | 160 checks, 0 failures |
| `test_lowermenu` | 246 checks, 0 failures |
| `test_options` | 49 checks, 0 failures |
| `test_xbrz` | 266 checks, 0 failures |
| `test_shadow` | 375 checks, 0 failures |
| `nitrosystem` | **17,122 checks, 0 failed** |

### What still fails, and why

**Phase 1 code failures: none.** The two blockers were real bugs, both documented with root causes in
[agent-notes/p2-wave-a-session5.md](agent-notes/p2-wave-a-session5.md):

- `test_arm7` was not a missing pump. The decompilation mixes two NitroSDK revisions: `SND_command.c`
  owns a 4-byte `sFreeList` pointer, `snd_flush_command.c` reads a 40-byte `CommandState` through it.
  The port links both, so no command reached the ARM7. Patch 1404.
- `test_nitrosystem` needed the compat headers fixed **and** a real `.bss` layout bug in
  `gfd_linked_list_tex_vram_man_reset.c` corrected (NitroSystem patch 0303).

The remaining failure class is the **DS gate** (§ next actions) and the **3DS build** (§9 of the
session note). The pattern behind the earlier runtime failures remains: **DS-side code that addresses
main/shared memory by literal address** (`0x027FFxxx`, `0x05000000`, `0x07000000`, and the sound
register block at `0x04000400`) now points nowhere, because patches 1301/1302 moved those regions into
the port's arrays. Fixed for RTC (1304), touch/PM/MIC (1305), the 2D VRAM loaders (1303), the sound
registers (1405) and the 3D texture loaders (1406). This is the first thing to grep for when a suite
crashes.

### Phase 2 state in one paragraph

The harness reads HGSS's own build files (1012 C, 56 C++, 150 ARM9 `.s`, 129 overlays), produces every
generated header without MWCC or a ROM, converts the `.s` files, builds both port archives, groups the
overlay sections, generates the Wi-Fi stand-ins and links. **1092 of 1092 game files compile** (patches
1509–1525), 150/150 `.s` assemble, and the host link is down to **34 unique unresolved symbols**:
MSL soft-float/string runtime (11), C++ runtime (6), multiboot stubs (12) and five Pokéwalker interior
aliases that need `tools/object_aliases.py`. `--check-overlays` and `tools/symbol_crosscheck.py` exist;
neither has run against a real linked binary yet.

## Next actions, in order

1. **Close the last 34 host32 symbols** ([phase-2.md](phase-2.md) has the grouped list):
   - run `tools/object_aliases.py` over game objects with an `.alias-map.json` (the five Pokéwalker
     aliases; the DS build does this and the harness does not);
   - add the MSL runtime compat (`_fadd`/`_ffix`/`_fflt`/`_f_lltof`/`_f_ulltof`/`_ll_mul`/`_ll_udiv`/
     `_s32_div_f`/`_fdiv`, `strnicmp`, `__lower_mapC`) with the *bit-pattern* ABI the game declares;
   - add the C++ runtime pieces (`Bytes::~Bytes()`, `Deque::~Deque()`, `operator new[]`/`delete[]`,
     `std::nothrow`, the vector throw helper) — link libstdc++ or define them explicitly;
   - add documented MB_* stand-ins (no download-play consumer).
2. **Link `pokeheartgold-pc`**, run `--check-overlays` on the real binary (expect all 129), then
   `tools/symbol_crosscheck.py` and refine the allowlist.
3. **Replace the two harness-weakened duplicate globals** (`FrontierScript_ReadByte`,
   `sListMenuTemplate`) with a real per-overlay/local fix.
4. **DS gate at 61+ patches, both versions**, from `/work/pokeheartgold` (the path is part of the pin).
   Regenerate the MSL `*-shape.json` debug-info pins first (see the session note §7); the last run
   stopped there, not on a code difference.
5. **3DS**: `hgss/std_sprintf.c` `va_list` arithmetic, the ARM7 rule rejecting
   `ds_sound_tables.c`, then the ~45 devkitARM-only compile errors; then the 3DS link.
6. **Close Phase 1/2 docs** once the gate and the two binaries are green.


## Open decisions

- **MB stub policy:** may the last 12 `MB_*` (multiboot / DS download play) symbols be documented
  no-op/error stand-ins? Nothing on PC/3DS uses them, and the allowlist already classifies MB as
  stubbed. Alternative: compile the multiboot sources.
- **C++ runtime:** link `libstdc++` for the `std::` pieces, or keep explicit small definitions for
  `Bytes`/`Deque` destructors and `throw_length_error` (the port builds with `-fno-exceptions
  -fno-rtti`)? Recommendation: explicit definitions, so the port does not grow a full C++ runtime for
  two symbols.
- **3DS depth for Phase 2:** is "the static link reaches zero unresolved" still the bar, or should
  `std_sprintf` and the ARM7 rule be fixed now so a real `.3dsx` is produced? The shell and the
  libctru link already exist.
- **MSL declaration mismatch:** `wifi_plaza_instrument.c` declares the soft-float helpers with
  bit-pattern types, `pokeathlon_relay_run.c` declares `f32 _fflt(int)`. The compat header plus a
  `SDK_PORT_3DS` patch must pick the bit-pattern form for both; do not implement a naive float C file.
- `g2d_CellTransferManager.h:24` stays fixed by patch 1101 (keep the patch, not a devkitARM pin).
- Port-side fixes go to the Platinum maintainer as upstream PRs where they are generic (the assert /
  math force-includes, the NNS table weak refs); HGSS-only shape changes stay in `hgss/`.
- Commit trailer is `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Resume in five minutes

```bash
cd <repo>
devcontainer up --workspace-folder .
devcontainer exec --workspace-folder . bash
# inside the container:
cd /workspaces/pokeheartgold-3ds
python3 tools/bootstrap.py            # "already at ... with 61 patches" if in sync
make test                            # exit 0 (204 tests + SDK suites)
make port-tests                      # exit 0 (pak + nitro + nitrosystem)
python3 tools/port_compile_survey.py # 46/46
python3 tools/hgss_game.py --target host32 --keep-going --out /work/main-g61 -j5
                                     # 1092/1092 compile, 34 unresolved, no binary yet
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
| devkitARM | 16.1.0 | layout probe ARM variant; future 3DS builds |
| host gcc | 13.3.0 (`gcc`) | surveys by default, most SDK suites |
| host gcc-10 | 10.5.0 (`gcc-10`), also `CC` in the image | the port's `host32` build (its make log shows gcc-10) |
| Python | 3.12.3 | all tools; `pyelftools`, `unicorn` when noted |
| Make | 4.3 | |
| devkitPro tools | `3dsxtool`, `smdhtool` present | Phase 5, not used yet |

The full toolchain/build reference is [wiki/Build-Reference.md](../wiki/Build-Reference.md).

### Log

Newest first. Each entry: what changed, what was run, what it proved.

### 2026-10-31 (session 5) — Phase 1 suites green, game compiles completely, link 966 → 34

- **Phase 1 fixed at the root.** `test_arm7` was two NitroSDK revisions disagreeing about `sFreeList`
  (patch 1404); `libnitrosystem.a`/`test_nitrosystem` needed the compat headers plus a real `.bss`
  layout fix in `gfd_linked_list_tex_vram_man_reset.c` (NitroSystem 0303). The newly reachable
  suites then exposed the sound register offsets/volume table (1405), 3D texture VRAM (1406) and the
  `g3imm` direct writes. **`make port-tests` exit 0; nitrosystem 17,122 checks, 0 failed.**
- **Phase 2 compile complete**: patches 1509–1525 (case labels, SDK prototype types, static linkage,
  static initializers, MSL/lobby `operator new`, PPW C++ names, `ball_capsule_editor`).
  **1092/1092 files compile, 0 failed** (was 1044/1094 with 50 failing); 150/150 `.s`.
- **Library set**: `libnitrosystem.a` links; all 21 `lib/NitroDWC/asm` data objects link (they have no
  `T` symbols) except four that reference a non-TLS `errno`; `lib/syscall/src/_secure_*.c` are
  excluded as unused DS secure-area firmware. Link: **34 unresolved** (was 966).
- **Infrastructure**: `main_3ds.c`, `--check-overlays` (129/0 on a temporary link) and
  `tools/symbol_crosscheck.py` landed earlier in the session (commit `07ca172`).
- **DS gate**: five tools pin inputs (1601/1602), the gate must run at `/work/pokeheartgold` because
  the producer pins embed the absolute source path, and a debug-info shape pin is still open. Last
  full pass remains 10 patches.
- **Process trap paid for**: a test re-pointed the shared `build/hgss-sdk` shim at `/up`, producing 763
  spurious failures in a concurrent harness; the test is now isolated.
- Full audit: [agent-notes/p2-wave-a-session5.md](agent-notes/p2-wave-a-session5.md).

### 2026-10-31 (session 4) — Phase 1 patch stack finished, vendored suites running, Phase 2 tooling + harness

- **Patches 1203, 0303, 1303, 1304, 1305, 1401-1403, 1501-1508, 1601** (39 total): GXFifoStat guard,
  `MATH_CountPopulation` 32-bit return, 2D VRAM loader literals, RTC/touch/PM/MIC shared-work
  literals, ARM7 sound driver + static init, the sound NULL-tag guard, the Phase 2 game GCC fixes,
  and the two alarm-partition header pin updates. Each has a `gcc -E -P` identity proof; the runtime
  ones are documented in [agent-notes/p1-runtime-bringup.md](agent-notes/p1-runtime-bringup.md).
- **Compat layer completed**: 65 files under `hgss/compat/` — missing ntrtwl headers, `sdk_data.c`,
  `sdk_standins.c`, ARM7 sound tables/PM pair, NNS data + GFD table, and the force-included MSL
  `assert.h` / `hgss_port_math.h`.
- **`tools/hgss_nitro.py` finished**: builds `libnitro.a` with the genuine ARM7 sound driver, and runs
  `pak`, `nitro` and `nitrosystem` with `--subproject`; `make port-tests` is real.
- **Measured**: `bootstrap --clean` 39 patches; `make test` exit 0 (182 Python tests + 18 SDK suites);
  `port_compile_survey.py` 46/46; `libnitro.a` 0 errors; vendored `pak` 120, `test_nitro` 702,
  `test_fs_pak` 120, `test_threads` 68 checks, 0 failed; `test_arm7` passes backup/RTC/power/touch and
  **hangs in `test_sound`** (open). DS gate not rerun at 39 patches.
- **Phase 2 started**: `game_sources.py`, `game_overlays.py`, `prepare_game_headers.py`,
  `s_to_gas.py`, `make_game_wifi_stubs.py`, `hgss/game/stubs`, `hgss_game.py`, `hgss/game/app`;
  game patches 1501-1508. 1044/1094 files compile, 150/150 `.s` assemble, 966 unresolved symbols at
  the host link. [phase-2.md](phase-2.md), [agent-notes/p2-slice-f.md](agent-notes/p2-slice-f.md).
- **Process traps recorded**: the Mac and container `build/` are different trees (verify in the
  container); patches exported without `--relative` land at doubled paths; the gate needs
  `make -C tools/mwasmarm_patcher` first; two build tools pin header hashes and refuse on drift.

### 2026-10-31 (session 3, Wave 1) — Compat headers, patch 1202, sdk-diff complete, 46/46 port compile & libnitro.a green

- Wrote 20 compat headers in `hgss/compat/include/` (`card_common.h`, `nitro/spi/common/type.h`, `nitro/rtc/common/{fifo,type}.h`, `nitro/snd/common/{command,main}.h`, `nitro/dgt/dgt.h`, `nitro/ioreg.h`, `nitro/cp/{divider,sqrt}.h`, `nitro/mi/{stream,uncompress,dma}.h`, `nitro/hw/ARM9/{mmap_global,mmap}.h`, `nitro/os/common/{interrupt,interrupt_shared}.h`, `nitro/gx/g3x.h`, `nitro/ctrdg.h`, `nitro/spi/userInfo.h`) and `hgss/compat/README.md`. Updated `tools/port_compile_survey.py` include order to match `tools/hgss_nitro.py`. [agent-notes/p1-compat.md](agent-notes/p1-compat.md)
- Added patch **1202** (`patches/pokeheartgold/1202-gcc-match-SDK-header-signatures-to-the-port-sources.patch`): `SDK_PORT_3DS`-guarded signature and layout-neutral field aliases for `MIi_Cpu*`, `PXI_*` (`PXIFifoEmtpyCallback`), `DC_StoreRange`, `NVRAMConfigOwnerInfo`/`Option` (`favoriteColor`/`favouriteColour`), `DGT_Hash*` + `DGTHash2Context` union, `CARDiCommandArg` (`dest`/`dst`), and `mi_dma_gxcommand.c` local enum guard. Extended `tests/sdk/gcc_fixes` to 11 groups. [agent-notes/p1-1202.md](agent-notes/p1-1202.md)
- Completed `docs/sdk-diff.md` and `docs/wiki/SDK-Differences.md`: verified RTC, TP, PM, and MIC PXI command numbers, framing bits, multi-word sequences, reply words, and shared buffers, plus `OSSystemWork`, `OSContext`, `OSThread`, IRQ table layouts, ARM7 sound mapping, and NitroSystem patch counterparts. [agent-notes/p1-sdk-diff.md](agent-notes/p1-sdk-diff.md)
- Verified in container: `python3 tools/bootstrap.py --clean` (16 patches), `make test` (exit 0), `python3 tools/port_compile_survey.py` (**46/46**, exit 0), `python3 tools/hgss_nitro.py make --target host32 --out /work/out/p1 lib -k -j8` (**exit 0**, builds `/work/out/p1/libnitro.a`).

### 2026-10-31 (later session) — W3 landed; 1302; host suites back on the port arrays; docs

- Landed the interrupted W3 slice as commit `7631efa`: patch **1201** (GCC-10 fixes: unnamed
  `SND_command.c` parameter, duplicate CRC header definitions, `OS_IrqHandler(void)`,
  `PXI_FIFO_TAG_CTRDG_PHI`), `tests/sdk/gcc_fixes`, the `irq_handler` call-syntax update, the W3
  agent note. Trailer normalised to Sonnet 5.5. Verified `git am` on a fresh clone; both suites pass.
- Rebootstrapped from scratch: **15 patches**. `make test` then failed: patch 1301 moved the
  registers into `NitroPort_IoReg`, and six suites still linked against the old fixed addresses.
- Added `tests/sdk/port_memory.c` (the eleven arrays), linked it into the suites, made
  `card_spi_wait` guard the whole I/O array with `mprotect`, and pointed `small` at the arrays.
  No assertion changed.
- Found a real gap in 1301: `REG_ADDR_DMA0CNT` and `REG_ADDR_DMA0_CLR_DATA` are absolute literals in
  `nitro/mi/dma.h` and the flat `MI_dma.h`, so they did not follow `HW_REG_BASE`; the DMA hook test
  segfaulted. Patch **1302** derives both from `HW_REG_BASE` under `SDK_PORT_3DS`; `memory_map`
  gains three checks (35 total). Commit `0abf623`.
- Measurements: `make test` exit 0 (11 suites + 19 Python tests); SDK survey 270/284 (gcc 13) and
  269/284 (gcc 10); port survey 29/46 (was 26); `host32` port build 44 errors in 9 objects (was 62
  in 10 before 1201/1301/1302).
- Not done: DS-side gate with the new patches; compat headers; port unit tests. Those are the next
  actions above.

### 2026-10-30 — Phase 0 closed, Phase 1 mostly built

- Phase 0: container, matching HG/SS builds, reference artifacts, savetool, Platinum sanity build.
  Open: golden captures, saves, determinism, native-macOS control. [phase-0.md](phase-0.md)
- Phase 1: patches 0101-0901, NNS 0301, 1001, 1101; survey 270/284; layout probe 0 differences;
  DS gate passed with 10 patches; 9 SDK suites green. Port wiring started: `hgss_nitro.py` shim,
  `port_compile_survey.py`, 1301 memory map. [phase-1.md](phase-1.md)
- W2 (compat headers) designed and measured (94 errors → 62 with the early fixes) but its headers
  were never committed. W3 (patch 1201) was authored and left uncommitted; this session finished it.
