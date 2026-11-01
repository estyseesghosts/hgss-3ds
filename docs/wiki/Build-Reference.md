# Build reference

Everything about the toolchain, the build setup and how to build each part of the project. This page
is the lookup table; [Getting-Started](Getting-Started.md) is the guided first run and
[Daily-Workflow](Daily-Workflow.md) is the loop once you are set up. The current results are in
[docs/progress/STATE.md](../progress/STATE.md).

Terms: **DS build** = the decompiled game built with MWCC, which must match the retail ROM.
**Host build** = 32-bit Linux x86 code compiled with GCC that models the DS and runs on the PC.
**Port layer** = `3ds_port/`, vendored from pokeplatinum-3ds.

## 1. Where builds run

| Thing | Where | Notes |
| --- | --- | --- |
| The repo | `/workspaces/pokeheartgold-3ds` in the container; your checkout on the host | bind mount |
| `~/hgss-private` | `/private` in the container | read-only; MWCC, Nitro SDK 3.2, optional ROMs/saves |
| `/work` | named volume `hgss-work` | `pokeheartgold` clone, matching builds, scratch, ccache |
| `<repo>/build` | named volume `hgss-build`, mounted over the workspace's `build/` | `build/upstream`, outputs; appears **empty on the Mac** |
| Emulators, BizHawk, the 3DS | the host | never in the container |

All builds are amd64 Linux. On Apple silicon OrbStack runs them with Rosetta; keep Rosetta on. The
container is `--platform=linux/amd64` and is not built for arm64.

```bash
cd <repo>
devcontainer up --workspace-folder .          # or: docker compose-like via the CLI
devcontainer exec --workspace-folder . bash   # interactive shell
devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && make test'
```

## 2. Toolchains in the image

| Tool | Version | Command | Used by |
| --- | --- | --- | --- |
| host GCC | 13.3.0 | `gcc` | default of every survey/test runner |
| host GCC 10 | 10.5.0 | `gcc-10` | the port's `host32` build; `CC` is set to it in the image |
| host G++ 10 | 10.5.0 | `g++-10` | same |
| devkitARM | 16.1.0 | `arm-none-eabi-gcc` | layout probe ARM variant; future 3DS builds |
| devkitPro tools | current | `3dsxtool`, `smdhtool` | `.3dsx` packaging after the 3DS link; SMDH/icon is Phase 5 (`bannertool`/`makerom` are not confirmed in the image) |
| Wine | WineHQ **10.0** | `wine` | MWCC (`mwccarm.exe`) and the SDK tools (`makelcf.exe`) |
| MWCC | 2.0 tree (`sp2p2`, and the patched `sp2p3` assembler) | `wine .../mwccarm.exe` | the DS matching build only |
| Nitro SDK | 3.2-060901 | copied by `tools/install-ds-toolchain.sh` | LCF templates, `makelcf.exe` |
| Python | 3.12 | `python3` | all tools; `pyelftools`, Pillow, numpy in the image; `unicorn` only in a venv |
| Make | GNU 4.3 | `make` | repo and vendored Makefiles |

Pinned on purpose: **Wine 10.0** (11.0 aborts under emulation; distro Wine 9.0 cannot load the SDK
tools), **Ubuntu 24.04** (devkitARM needs glibc 2.36; 22.04 has 2.35). Do not bump either to "fix" a
build without reading [../decisions.md](../decisions.md).

MWCC and the Nitro SDK are never in the image or the repo. They enter through `/private` and are
copied into a git-ignored decomp checkout by `tools/install-ds-toolchain.sh`.

### Environment variables

| Variable | Value | Effect |
| --- | --- | --- |
| `CC` | `gcc-10` (image) | vendored port Makefiles use `CC ?= cc`, so they get gcc-10; `tests/sdk/small/run.sh` also reads it |
| `CXX` | `g++-10` (image) | |
| `HGSS_PRIVATE` | `/private` (image) | default private folder for `install-ds-toolchain.sh` |
| `CCACHE_DIR` | `/work/.ccache` | only if a build opts into ccache |
| `GCC` | unset → `gcc` | per-run override for the SDK suites and surveys (`GCC=gcc-10 bash tests/sdk/...`) |
| `PY` | unset → `python3` | Python with `pyelftools` for the layout probe |

The SDK survey and most test runners deliberately use plain `gcc` (13), matching Platinum's CI. The
port's own build uses gcc-10. A file can pass with 13 and fail with 10 (this happened with
`SND_command.c`); record **both** numbers when you change SDK code.

## 3. What can be built today

| Target | Command | Output | State |
| --- | --- | --- | --- |
| Patched decomp tree | `python3 tools/bootstrap.py [--clean]` | `build/upstream/`, `build/NitroSDK`, `build/NitroSystem` | works, **61 patches** |
| Host test gate | `make test` | test results only | **green** (204 Python tests + 18 SDK suites) |
| SDK syntax survey | `make sdk-survey` or `python3 tools/sdk_compile_survey.py --m32 -j8` | counts | 270/284 (gcc 13), 269/284 (gcc 10) |
| Port source survey | `python3 tools/port_compile_survey.py` | counts + first errors | **46/46, exit 0** |
| Port layer host32 libraries | `python3 tools/hgss_nitro.py make --target host32 --out /work/out/x lib -k -j8` | `libnitro.a`, `libnitrosystem.a` under `/work/out/x` | **both build, 0 errors** |
| Vendored port suites | `python3 tools/hgss_nitro.py make --target host32 --out /work/out/x test` or `make port-tests` | test output | **exit 0**: pak 120, the whole nitro sequence (arm7 676, sound_sequence 12, render 2071, gx3d 326+228, vram 24, shadow 375, …), nitrosystem **17,122** — 0 failed |
| Save-layout probe | `make layout` | differences | 0/804 (last run) |
| DS matching build | `tools/matching-build.sh /work/pokeheartgold HEARTGOLD` | ROM in `/work` | works; needs MWCC; **not passed at 61 patches** (last pass 10) |
| Game source list / headers / `.s` | `python3 tools/game_sources.py`, `tools/prepare_game_headers.py`, `tools/s_to_gas.py` | lists + generated headers | works; 150/150 `.s`, headers byte-identical |
| Game for PC | `make game-pc` | `build/pokeheartgold-pc` | **1092/1092 compile, 34 unresolved; no binary yet** ([phase-2.md](../progress/phase-2.md)) |
| Game for 3DS | `make game-3ds` | `build/game-3ds/pokeheartgold-3ds.elf` + `pokeheartgold.3dsx` | 1092/1092 compile, both archives build, 0 unresolved; 3dsxtool packages the `.3dsx` (SMDH is Phase 5) |

There is no `make game`, `make matching` or `builder/` in this repo. See
[Tools-Reference](Tools-Reference.md#named-in-the-docs-but-not-in-the-repo).

## 4. First build, from a clean clone

The full guided version is [Getting-Started](Getting-Started.md). The order matters because each
step produces what the next one reads.

```bash
# 0. private files in ~/hgss-private (MWCC 2.0, Nitro SDK 3.2-060901); see Getting-Started step 3
devcontainer up --workspace-folder .

# 1. patched decomp tree (needs GitHub on the first run)
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && python3 tools/bootstrap.py'

# 2. host tests (204 Python tests + 18 SDK suites)
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && make test'

# 3. surveys (progress measures, not gates)
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && make sdk-survey'
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && python3 tools/port_compile_survey.py'

# 4. the port layer against HGSS: libraries, then all three vendored test suites
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && python3 tools/hgss_nitro.py make \
     --target host32 --out /work/out/try lib -k -j8'
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && make port-tests'
```

### The DS side (MWCC), separately

This is the "the patches did not change the retail ROM" gate. It needs the full decomp clone and the
DS toolchain, and it is slow (about 18 minutes for the first HeartGold build):

```bash
devcontainer exec --workspace-folder . bash -c \
  'git clone https://github.com/ctrl-cheeb-del/pokeheartgold /work/pokeheartgold && \
   git -C /work/pokeheartgold checkout "$(sed -n "s/^commit=//p" /workspaces/pokeheartgold-3ds/upstream.lock)"'
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && tools/install-ds-toolchain.sh /work/pokeheartgold'
devcontainer exec --workspace-folder . bash -c \
  'cd /work/pokeheartgold && make patch_mwasmarm MWCCVER=2.0/sp2p3'
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && tools/matching-build.sh /work/pokeheartgold HEARTGOLD'
```

Use `tools/matching-build.sh`, **not** plain `make`: it restores five sections the container's linker
strips. The detailed gate (a clone of `build/upstream` with the patches, so the patch stack is what is
tested) is in [Daily-Workflow](Daily-Workflow.md#the-ds-side-gate).

### The layout probe

`make layout` needs the decomp's generated `.naix` headers, which come from a decomp build in
`/work/pokeheartgold`, plus `pyelftools` (in the image). The MWCC reference values are committed in
`tools/layout_probe/mwcc.txt`; do not regenerate them (`--update-mwcc`) unless you mean to.

## 5. Compiler flags and ABI (why they matter)

The port's flags are in `3ds_port/common.mk` (`PORT_CFLAGS`) and are repeated by the survey:

```
-std=gnu99 -fgnu89-inline -fno-strict-aliasing -fno-short-enums -fsigned-char -fwrapv
-fno-aggressive-loop-optimizations -fno-delete-null-pointer-checks
-m32 -malign-double
```

- **`-m32 -malign-double`** keeps pointers 4 bytes so host structs match the DS. Patch 1001 makes
  `u64`/`s64` 4-byte aligned; `-malign-double` is only safe *because* of 1001.
- **`-fno-short-enums`** keeps enums 4 bytes.
- **`-fsigned-char`, `-fwrapv`, `-fno-strict-aliasing`, `-fno-delete-null-pointer-checks`,
  `-fno-aggressive-loop-optimizations`** reproduce the assumptions the MWCC code was written under.
  Do not drop them: the save layout and some game logic depend on them.
- **The 64-bit `host` target is not supported.** Patch 1301 turns `HW_*` into `(u32)array + offset`
  expressions, which static initializers can only use on a 32-bit target. Use `host32`.
- **`SDK_PORT_3DS`** is defined for every port build and never by MWCC. All SDK/decomp changes are
  behind it.

## 6. Build outputs and cleanup

| Path | Contents | Safe to delete? |
| --- | --- | --- |
| `build/upstream/` | the pinned decomp with the patch stack applied as commits | yes; `bootstrap.py --clean` rebuilds it (needs network) |
| `build/NitroSDK`, `build/NitroSystem` | symlinks to the patched SDK sources | yes; bootstrap recreates |
| `/work/pokeheartgold` | full decomp clone + matching build outputs + ROM | yes, but re-cloning and rebuilding is slow; the ROM is game content and must stay there |
| `/work/out/<name>/` | private port-build output directory (the `--out` flag) | yes |
| `/work/.ccache` | cache | yes |
| `tests/golden/` | captured frames (game art) | git-ignored, never commit |
| `build/reference/` | Phase 0 reference ELF/map/nm | keep for Phase 2 symbol comparison |

`make clean` is `rm -rf build` and deletes **everything** under `build/`, including `build/upstream`
and `build/reference`. Prefer `bootstrap.py --clean` when you only want a fresh upstream.

## 7. How the vendored port build is wired

The vendored Makefiles expect Platinum's layout (`libraries/<lib>/src`) and Platinum's
`include/nitro/**`. `tools/hgss_nitro.py` builds a shim under `build/hgss-sdk` and `build/hgss-nns`:

- `build/hgss-sdk/libraries/hgss` → `build/upstream/lib/NitroSDK/src`, so `SDK_SRCS` can name HGSS
  files (`hgss/os/os_thread.c`); `build/hgss-sdk/libraries/hgss_sub` → `sub/lib/src` for the genuine
  ARM7 sound driver, which the Makefile renames to `N7_*`; `build/hgss-sdk/libraries/hgss_compat` →
  `hgss/compat/src` for the SDK stand-ins and data.
- `build/hgss-sdk/include` → only `nitro*`, `nnsys*`, `cw*` from `lib/include` (HGSS's `stdarg.h`,
  `stddef.h` and `string.h` there would shadow the system ones), plus `hgss/compat/include`.
- `build/hgss-nns` mirrors `lib/NitroSystem/src` as `hgss_nns/` and exposes the two files the nitro
  tests compile by their Platinum paths (`g3d/src/gecom.c`, `gfd/.../gfd_VramTransferManager.c`).
- make overrides: `SDK=`, `SDK_SRCS=`, `SDK_INCLUDES=`, `ARM7_SOUND_SRCS=`, `NNS=`, `NNS_SRCS=`, `OUT=`.
- every compile also force-includes HGSS's MSL `lib/include/assert.h` and
  `hgss/compat/include/hgss_port_math.h`. Both are needed because vendored sources call `assert` and
  C99 math without including the headers, and the project's include path shadows the system ones.

`--subproject {pak,nitro,nitrosystem,all}` selects what runs; with the goal `test` and no flag it runs
all three, which is what `make port-tests` does.

```bash
python3 tools/hgss_nitro.py list                                   # source lists + exclusions
python3 tools/hgss_nitro.py shim                                   # create the shims, print overrides
python3 tools/hgss_nitro.py make --target host32 --out /work/out/x lib -k -j8
python3 tools/hgss_nitro.py make --target host32 --out /work/out/x test
```

`--out` is important for parallel work: without it, objects go to the vendored `build/host32` and two
runs overwrite each other. `-k` keeps going after the first failing file so you see all errors.
`tools/port_compile_survey.py` is the fast per-file syntax measure; the make build is the real thing.

Two `--out` details matter since session 5:

- `NNS_INLINE_SYMS` was hardcoded in `3ds_port/common.mk`; `nitrosystem_overrides()` now passes
  `NNS_INLINE_SYMS=<out>/gen/nns_inline.syms` and creates the directory, so a private output is
  really private.
- HGSS's NitroSystem headers are all `static inline`, so the `nns_all_headers.o` symbol list is
  legitimately empty, and binutils 2.42's `objcopy --weaken-symbols=` rejects an empty file. The
  harness passes an `NM` wrapper that appends one harmless nonexistent `T` name for that object.

Current state: both libraries build with zero errors and **every suite passes**, including
`test_arm7`, `test_sound_sequence` and `test_nitrosystem`. The failure history and root causes are in
[../progress/agent-notes/p1-runtime-bringup.md](../progress/agent-notes/p1-runtime-bringup.md) and
[../progress/agent-notes/p2-wave-a-session5.md](../progress/agent-notes/p2-wave-a-session5.md).

## 8. Troubleshooting

These are the build failures this repo has actually hit. Setup-level problems are in
[Getting-Started](Getting-Started.md#troubleshooting).

### `undefined reference to 'NitroPort_IoReg'` in a host test

Patch 1301 points the SDK's `HW_*` macros at the port's arrays. A test that links an SDK object
touching a register must also link `tests/sdk/port_memory.c` (the eleven array definitions). The
suites in `tests/sdk/` already do.

### A register write segfaults at `0x04000000`

An SDK header may spell a register as a literal DS address instead of `HW_REG_BASE + offset`. Patch
1302 fixed the two in `nitro/mi/dma.h`; if another appears, fix it the same way (guarded, DS branch
unchanged) and add a `memory_map` check rather than mapping the address in the test.

### `fx_const.h` warnings (`extra tokens at end of #ifndef directive`)

The decomp generates the include guard from the absolute path and the path contains `-`; every
compile warns. Harmless, known, and not a reason to edit a generated file. See
[../progress/phase-1.md](../progress/phase-1.md).

### A fresh clone of `build/upstream` does not compile the tests

It has no generated `lib/include/nitro/fx/fx_const.h`. Copy it from a bootstrapped tree or run the
decomp's generator (`make -C <tree> lib/include/nitro/fx/fx_const.h`). `bootstrap.py` already does
this for `build/upstream`.

### gcc-10 disagrees with gcc-13

Record both. The survey defaults to 13; the port builds with 10. A patch that only compiles under 13
is not done. `python3 tools/sdk_compile_survey.py --gcc gcc-10` gives the port's number.

### The port build uses the wrong compiler

`CC` is `gcc-10` in the image on purpose. If you unset it, the Makefile falls back to `cc` (also
gcc 13) and you may see different errors from the ones the port is designed for. Use the image's
default.

### `bootstrap.py` refuses to run

It compares the number of applied commits with the number of patch files and does not notice edits
to a patch's contents. After adding, removing or editing a patch: `python3 tools/bootstrap.py --clean`.

### The DS gate stops with "physical input drift", "production closure drift" or "header drift"

Five decomp tools verify their inputs before building, not two:

- `tools/arm7-alarm-partition/build.py` — `physical input drift`, `C_header_pins`;
- `tools/alarm-partition/build.py` — `production closure drift`, `closure`;
- `tools/msl-new-partition/build.py`, `tools/msl-d1-partition/build.py`,
  `tools/dwc-iterator-advance/build.py` — `header drift`, `unregistered source`, `complete producer
  section/symbol/relocation/payload drift`.

A patch that edits one of those headers (all inside `#ifdef SDK_PORT_3DS`) makes the check fail and
names the file. Update the pins in the `profile.json` files — patches `1601` and `1602` are the
examples — and rerun. The MSL tools pin more than headers: `producers.*.source_sha256` (patches 1505
and 1519 changed those sources) and the complete producer object. The pin is provenance, not ROM
content; the ROM SHA1 is the proof.

**Run the gate from `/work/pokeheartgold`.** The MSL/DWC producer pins embed the absolute source
directory (`Z:\work\pokeheartgold\src\…`) in an object payload, so a clone at `/work/patched` fails
with `complete producer … payload drift` even though the code is identical. Rename the old reference
tree aside and clone into that path; the recipe in
[Daily-Workflow](Daily-Workflow.md#the-ds-side-gate) does.

**A debug-info shape pin can still be stale after a `#ifdef`-only patch.** Adding lines under
`#ifdef SDK_PORT_3DS` shifts source line numbers, so MWCC's debug sections change even though the
generated code does not. `msl-new-partition` compares the whole object, including those sections.
Regenerate `scalar-shape.json`/`array-shape.json` with the tool's normalizer; do not hand-edit them.
This was open at session 5 (observed: two payload hashes, a section length 238 vs 226, a symbol size
48 vs 36).

### `link failed (0 unresolved symbols)` but there is no binary

`ld` can abort on an earlier error before it finishes symbol resolution; the harness parses only the
lines `ld` emitted. Read the whole `link.log`. The session-5 example was a four-object NitroDWC data
group that references `errno` as a non-TLS symbol, clashing with glibc's TLS definition
(`errno: TLS definition in /lib32/libc.so.6 section .tbss mismatches non-TLS reference`). Those four
(`dwc_gamespy_bss.o`, `dwc_lib_bss.o`, `dwc_lib_data.o`, `dwc_lib_rodata.o`) are DWC-internal libc
state and are excluded from the link.

### `make test` and a harness run disagree (hundreds of spurious compile failures)

Both paths use the shared `build/hgss-sdk` and `build/hgss-nns` symlinks. A test that called
`make_shim()` with a fake root re-pointed them at a nonexistent tree, and a concurrent harness then
failed in 763 files with NNS incomplete-type errors. The test now keeps its shim in a temp `BUILD`;
still, do not run `make test` concurrently with a harness build, and do not run
`bootstrap --clean` while anything reads the tree.

### The 3DS `libnitro.a` fails in `hgss/std_sprintf.c`

devkitARM rejects the `va_list` arithmetic (`invalid operands to binary + (va_list and int)`). This is
unrelated to the host32 build (which uses glibc's `va_list`); it blocks the 3DS archive and therefore
the 3DS link. Fix at the source under `SDK_PORT_3DS`.

### The 3DS ARM7 rule rejects `hgss/compat/src/ds_sound_tables.c`

The vendored rule feeds the file to the assembler path (`junk at end of line`, `unrecognized symbol
type ""`) because it is an ARM7 compat C file. Give it a `.S`/`.c` distinction or build it as C with
the ARM7 defines and rename it like the other compat files, in the `tools/hgss_nitro.py` override.

### `make patch_mwasmarm` says `tools/mwasmarm_patcher/mwasmarm_patcher: No such file or directory`

The patcher binary is git-ignored, so a fresh clone does not have it. Run
`make -C tools/mwasmarm_patcher` first; the gate recipe in
[Daily-Workflow](Daily-Workflow.md#the-ds-side-gate) does.

### A vendored test segfaults, or hangs at 100 % CPU

These are almost always host-memory or protocol issues, not logic bugs:

- **Literal DS addresses.** Patches 1301/1302 moved main/shared memory and the I/O registers into the
  port's arrays. Any remaining `0x02…` (main/shared RAM), `0x05…`/`0x06…`/`0x07…` (palette/VRAM/OAM)
  pointer in compiled code points at unmapped memory. Patches 1303-1305 fixed the classes found so far
  (2D VRAM loaders, RTC, touch/PM/MIC). Grep before assuming anything else.
- **A NULL shared-work pointer.** The ARM7 driver only learns `SNDi_SharedWork` from the setup command;
  a test that calls the ARM9 API directly can push another command first (patch 1403 guards the tag).
- **Missing `assert`/math declarations.** An implicit `assert` leaves an undefined symbol; an implicit
  `double` function returns garbage. Both are handled by the force-includes in
  [section 7](#7-how-the-vendored-port-build-is-wired); if a new vendored file needs more, add the
  declaration to `hgss/compat/include/hgss_port_math.h` rather than a no-op stub.

`gdb` cannot attach under the container's emulation (`PTRACE_GETREGS: Input/output error`) and cores
are disabled (`ulimit -c` is 0). Copy the test to `/tmp`, insert
`fprintf(stderr, "TRACE <line>\n"); fflush(stderr);` after each statement, and rebuild it against the
same `libnitro.a`; the last line printed is the crashing statement. The vendored file is never edited.

### A header that certainly exists is reported missing mid-build

`bootstrap.py --clean` deletes `build/upstream`. If a build was reading it at the same time, the
missing-header error is a race, not a real problem. Serialise bootstrap against builds.

## 9. Changing the toolchain

The image pins Wine and Ubuntu for reasons recorded in [../decisions.md](../decisions.md); MWCC and
the Nitro SDK versions are pinned by `docs/BASE.md`. A toolchain change is a task of its own:

1. Change the pin in the Dockerfile (or the private folder layout).
2. Rebuild the image and the container, rerun `onCreate.sh`.
3. Re-run `make test`, both surveys, `make layout`, the DS-side gate for **both** versions, and the
   Platinum sanity build.
4. Record the versions and results in `docs/progress/STATE.md` and the decision log.

Do not move `CC` from gcc-10 or Wine from 10.0 as part of an unrelated change.

## See also

- [Getting-Started](Getting-Started.md): first run, with expected output at every step.
- [Daily-Workflow](Daily-Workflow.md): the edit → test → gate loop, and patch authoring.
- [Tools-Reference](Tools-Reference.md): every script and test runner.
- [Architecture](Architecture.md): what the layers are and what exists.
- [docs/progress/STATE.md](../progress/STATE.md): what is green today.
