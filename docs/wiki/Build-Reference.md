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
| devkitPro tools | current | `3dsxtool`, `smdhtool` | Phase 5 packaging (`bannertool`/`makerom` are not confirmed in the image) |
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
| Patched decomp tree | `python3 tools/bootstrap.py [--clean]` | `build/upstream/`, `build/NitroSDK`, `build/NitroSystem` | works, 15 patches |
| Host test gate | `make test` | test results only | **green** |
| SDK syntax survey | `make sdk-survey` or `python3 tools/sdk_compile_survey.py --m32 -j8` | counts | 270/284 (gcc 13) |
| Port source survey | `python3 tools/port_compile_survey.py` | counts + first errors | 29/46 |
| Port layer host32 | `python3 tools/hgss_nitro.py make --target host32 --out /work/out/x lib -k -j8` | objects under `/work/out/x` | **fails**, 44 errors in 9 objects |
| Save-layout probe | `make layout` | differences | 0/804 (last run) |
| DS matching build | `tools/matching-build.sh /work/pokeheartgold HEARTGOLD` | ROM in `/work` | works; needs MWCC |
| Game for 3DS or PC | — | — | **does not exist yet** (Phase 2+) |

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

# 2. host tests (19 Python tests + 11 SDK suites)
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && make test'

# 3. surveys (progress measures, not gates)
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && make sdk-survey'
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && python3 tools/port_compile_survey.py'

# 4. the port layer against HGSS (currently expected to fail)
devcontainer exec --workspace-folder . bash -c \
  'cd /workspaces/pokeheartgold-3ds && python3 tools/hgss_nitro.py make \
     --target host32 --out /work/out/try lib -k -j8'
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

## 7. How the vendored port build is wired (and why it fails today)

The vendored Makefiles expect Platinum's layout (`libraries/<lib>/src`) and Platinum's
`include/nitro/**`. `tools/hgss_nitro.py` builds a shim under `build/hgss-sdk`:

- `build/hgss-sdk/libraries/hgss` → symlink to `build/upstream/lib/NitroSDK/src`, so the source list
  can name HGSS files;
- `build/hgss-sdk/include` → only `nitro*`, `nnsys*`, `cw*` from `lib/include` (HGSS's `stdarg.h`,
  `stddef.h` and `string.h` there would shadow the system ones), plus `hgss/compat/include`;
- a stand-in `gen_fx_const.py` that copies the generated `fx_const.h`;
- make overrides: `SDK=`, `SDK_SRCS=`, `SDK_INCLUDES=`, `ARM7_SOUND_SRCS=` (empty).

```bash
python3 tools/hgss_nitro.py list                                   # source list + exclusions
python3 tools/hgss_nitro.py shim                                   # create the shim, print overrides
python3 tools/hgss_nitro.py make --target host32 --out /work/out/x lib -k -j8
```

`--out` is important for parallel work: without it, objects go to the vendored `build/host32` and two
runs overwrite each other. `-k` keeps going after the first failing file so you see all errors.
`tools/port_compile_survey.py` is the fast per-file syntax measure; the make build is the real thing
(it catches link errors and missing symbols).

The current failures and their owners are listed in [../port-wiring.md](../port-wiring.md) and
[docs/progress/STATE.md](../progress/STATE.md).

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
