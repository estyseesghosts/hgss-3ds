# Current state

Last verified: **2026-10-31 (session 4)**, inside the dev container (`/workspaces/pokeheartgold-3ds`), with the
code at commit `e242375` and **39 patches**. Everything below that says "verified" was run on that tree on that
date. If you change the patch stack, rerun the commands and update this page; do not trust it blindly.

**One line:** Phase 1 is one hang away from done (the vendored `test_arm7` `test_sound` spin); every other
vendored suite passes, `make test` is green and `libnitro.a` builds. Phase 2 has started: the game source
list, overlay table, generated headers, `.s` conversion, stubs and a build harness exist; 1044/1094 game
files compile and the link is a documented 966-symbol work queue. No game binary links yet.

## Phases

| Phase | State | Evidence |
| --- | --- | --- |
| 0 Baseline | **Done**, open items | Matching HG/SS ROM SHA1s in the container. Never done: golden captures, fixture saves, the determinism run, a native-macOS control. [phase-0.md](phase-0.md) |
| 1 SDK patch stack | **In progress, one hang left** | 39 patches apply from scratch; survey 270/284 (gcc 13) / 269/284 (gcc 10); port survey 46/46; `libnitro.a` builds with 0 errors; vendored `pak` 120 checks, `nitro` 702+120+68, all 0 failed; `test_arm7` hangs in `test_sound`; DS gate not rerun since patch 1601. The runtime debugging log is [agent-notes/p1-runtime-bringup.md](agent-notes/p1-runtime-bringup.md). [phase-1.md](phase-1.md) |
| 2 Game code under GCC | **In progress** | `tools/game_sources.py`, `game_overlays.py`, `prepare_game_headers.py`, `s_to_gas.py`, `hgss_game.py`; 1044/1094 game files compile, 150/150 `.s` assemble, 966 unresolved symbols at the host link. [phase-2.md](phase-2.md) |
| 3 Data pipeline | Not started | `builder/pack.py` exists (used by the pak tests); no ROM extractor. |
| 4-11 | Not started | See [../wiki/Architecture.md](../wiki/Architecture.md). |


## Verified today

Run from the repo root inside the container. `devcontainer exec --workspace-folder . bash -c 'cd
/workspaces/pokeheartgold-3ds && <command>'` runs any of them from the Mac.

| Check | Command | Result |
| --- | --- | --- |
| Patch stack applies from scratch | `python3 tools/bootstrap.py --clean` | `Done: build/upstream (39 patches applied)` |
| Host tests | `make test` | **exit 0**; 182 Python tests + all 18 SDK suites |
| SDK parses (gcc 13, default) | `make sdk-survey` | **270/284** parse, 14 fail (all port-replaced) |
| SDK parses (gcc 10, the port's compiler) | `python3 tools/sdk_compile_survey.py --m32 --gcc gcc-10` | **269/284**; the extra failure is `ctrdg/CTRDG_common.c` |
| Port sources vs HGSS headers | `python3 tools/port_compile_survey.py` | **46/46** compile (**exit 0**) |
| Vendored libraries (host32) | `python3 tools/hgss_nitro.py make --target host32 --out /work/out/x lib -k -j8` | **exit 0**, 0 errors, `libnitro.a` |
| Vendored suites | `python3 tools/hgss_nitro.py make --target host32 --out /work/out/x test` | **pak 120, test_nitro 702, test_fs_pak 120, test_threads 68 — all 0 failed; `test_arm7` hangs in `test_sound`** |
| `make port-tests` | `make port-tests` | real target now (runs the three subprojects); **fails on the same `test_arm7` hang** |
| Save layout | `make layout` | 0 differences in 804 values (last run before the 39-patch stack; layout patches did not change) |
| DS-side gate | [wiki/Daily-Workflow.md](../wiki/Daily-Workflow.md#the-ds-side-gate) | **not passed at 39 patches**. Last full pass: 10 patches. At 34 patches it got past `sub/` and stopped at the `tools/alarm-partition` header pins; 1601 re-pins them, so a rerun at 39 is owed and is the only strong proof. |

### Vendored suites: what has actually run at 39 patches

Verified in `/work/v18-test.log` and `/work/v19-test.log` (in this order; the run stops at the first
failure):

| Suite | Result |
| --- | --- |
| `pak-backed NitroFS` | 120 checks, 0 failed |
| `test_nitro` | 702 checks, 0 failed |
| `test_fs_pak` | 120 checks, 0 failed |
| `test_threads` | 68 checks, 0 failed |
| `test_arm7` | backup, RTC, power and touch pass; **hangs in `test_sound`** |

**Not yet run at 39 patches** (the suite order stops at `test_arm7`): `test_sound_chip`,
`test_sound_sequence`, `test_render`, `test_gx3d`, `test_gx3d_plumbing`, `test_vram_transfer`,
`test_upscale`, `test_xbrz`, `test_shadow`, `test_lowermenu`, `test_options` and `nitrosystem`. Earlier
sessions reported individual passes for several of them before the last patch wave (numbers in the
agent notes), but that is not a 39-patch run — treat them as "expected to pass, unverified". The
`nitrosystem` subproject also needs the `nns_all_headers.c` unity-file conflict fixed first
(see the next actions).

### What still fails, and why

Only one class remains in Phase 1: **`test_arm7` `test_sound` spins**. The genuine ARM7 sequencer is
linked in, the test drives the ARM9 API without a full boot, and the finished-command tag never
advances (nothing pumps `N7_SND_CommandProc` for it). Everything else that failed this session is
fixed; the full list with root causes is in
[agent-notes/p1-runtime-bringup.md](agent-notes/p1-runtime-bringup.md). The pattern behind most of
them: **DS-side code that addresses main/shared memory by literal address** (`0x027FFxxx`,
`0x05000000`, `0x07000000`) now points nowhere, because patches 1301/1302 moved those regions into the
port's arrays. Fixed for RTC (1304), touch/PM/MIC (1305) and the 2D VRAM loaders (1303). This is the
first thing to grep for when the next suite crashes.

### Phase 2 state in one paragraph

`tools/game_sources.py` + `game_overlays.py` read HGSS's own build files (1012 C, 56 C++, 150 ARM9
`.s`, 129 overlays); `prepare_game_headers.py` produces every generated header without MWCC or a ROM
(byte-identical against a matching build); `s_to_gas.py` assembles all 150 `.s` under both compilers
with no symbol/section/alignment difference; Wi-Fi/WM/VCT/dsprot stubs exist; `hgss_game.py` compiles
the game and reaches both links. 1044/1094 files compile (50 fail), 150/150 `.s` assemble, the host
link reports **966 unique unresolved symbols** — that list is the work queue and is in
[phase-2.md](phase-2.md).

## Next actions, in order

1. **Fix the `test_arm7` sound spin**, then `make port-tests` green: decide who pumps
   `N7_SND_CommandProc` for a test that calls the ARM9 API directly (synchronous process on push in
   `port_snd.c`, or a tick from the wait). Do not edit the vendored test.
2. **`test_nitrosystem`**: Slice F reported the generated `nns_all_headers.c` unity file failing on
   `NNSSndHandle` and conflicting SND prototypes from `hgss/compat/include/nitro/snd/ARM9/interface.h`;
   fix the compat header, then run the subproject.
3. **DS-side gate at 39 patches**, both versions, with `make -C tools/mwasmarm_patcher` first.
4. **Close Phase 1** in `phase-1.md`/STATE/wiki once 1-3 are green.
5. **Phase 2**: work the 966-symbol queue in `phase-2.md`; then `--check-overlays`,
   `tools/symbol_crosscheck.py`, and the docs.


## Open decisions

- **The `test_arm7` sound spin (the only Phase 1 blocker):** should the virtual ARM7 process a pushed
  command synchronously (`NitroPort_HandleSound` → `N7_SND_CommandProc`) so a test that calls the ARM9
  API without a full boot sees the tag advance, or should `port_snd.c` run its tick from `OS_Halt` and
  the wait? Both are port-layer; the vendored test must not change. Recommendation: synchronous
  processing — it matches "the ARM7 is always running" while keeping the audio thread optional.
- `test_nitrosystem`'s unity file conflicts: fix `hgss/compat/include/nitro/snd/ARM9/interface.h`
  (drop/forward the prototypes that clash with `SND_*`) — chosen; do not special-case the test.
- `g2d_CellTransferManager.h:24` stays fixed by patch 1101 (keep the patch, not a devkitARM pin).
- Port-side fixes go to the Platinum maintainer as upstream PRs where they are generic (the assert /
  math force-includes, the NNS table weak refs); HGSS-only shape changes stay in `hgss/`.
- The survey still defaults to gcc 13 but the port builds with gcc 10; both numbers are recorded.
- Commit trailer is `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` (earlier agent notes
  sometimes say Haiku 5.5).

## Resume in five minutes

```bash
cd <repo>
devcontainer up --workspace-folder .
devcontainer exec --workspace-folder . bash
# inside the container:
cd /workspaces/pokeheartgold-3ds
python3 tools/bootstrap.py            # "already at ... with 39 patches" if in sync
make test                            # exit 0
python3 tools/port_compile_survey.py # 46/46
python3 tools/hgss_nitro.py make --target host32 --out /work/out/try test   # all three subprojects
```

If `bootstrap.py` reports a patch-count mismatch, run `--clean`. If `build/` looks empty on the Mac,
that is expected: it is a volume (`hgss-build`) inside the container.

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
