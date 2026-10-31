# Daily workflow

How to work on the port once [Getting-Started](Getting-Started.md) is green. The short version:

1. Make sure `build/upstream` matches the pin and the patches (`bootstrap.py`).
2. Change code: a patch in `patches/`, or a file in `3ds_port/`, `tools/` or `tests/`.
3. Run `make test`. Run the survey if you touched SDK code.
4. If you touched the port layer, build it with `hgss_nitro.py` (it is expected to fail today; the error count must go down, not up).
5. If you touched a decomp or SDK patch, run the DS-side gate before it goes in.

Almost everything runs inside the container. Use `devcontainer exec --workspace-folder . bash -c 'cd /workspaces/pokeheartgold-3ds && ...'`, or open a shell with `devcontainer exec --workspace-folder . bash`.

## Rebuild `build/upstream`

`build/upstream/` is the decomp at `upstream.lock`, with the patches applied as commits. Nothing edits the decomp in place.

```
python3 tools/bootstrap.py
```

With no flag, it does nothing when the checkout is at the pin and the number of applied patches equals the number of patch files. It then prints `upstream: already at ... with N patches`.

Use `--clean` when:

- you added or removed a patch file. Bootstrap sees the count differs and stops with a message that says so;
- you edited an existing patch file. **Bootstrap does not notice.** It compares counts, not contents;
- bootstrap reports local changes or a checkout that does not descend from the pin.

```
python3 tools/bootstrap.py --clean
```

`--clean` deletes `build/upstream/` and the two SDK symlinks, then fetches again from GitHub. It does not touch `/work` or `/private`.

## Survey tools

Two measures show how much of the SDK and the vendored port layer compiles with GCC. They do not pass or fail on their own.

```
make sdk-survey
python3 tools/port_compile_survey.py
```

- `make sdk-survey` runs `tools/sdk_compile_survey.py` over `lib/NitroSDK` and `lib/NitroSystem`. Last run: 270 of 284 files parse, 14 fail (gcc 13); with `--gcc gcc-10`, the port's compiler, 269 of 284. The Makefile target exits 1 while any file fails, so use it as a count, not a gate. For the first errors, run `python3 tools/sdk_compile_survey.py --m32 -j 10 -v`. Add `--json /work/survey.json` to keep the per-file results outside the repo.
- `tools/port_compile_survey.py` checks the vendored `3ds_port/nitro/src` against the HGSS headers. Last run: **29 of 46** compile. The causes are in [docs/port-wiring.md](../port-wiring.md). This number is the fast measure; the real build is `hgss_nitro.py` below.

Both numbers are progress measures, not pass/fail gates. Record both compiler versions when you change SDK code: a file can parse under 13 and fail under 10.

## Host test suites

`make test` is the gate, and CI runs it. It runs two groups:

Python tests (`make tool-tests`, 19 tests):

- `tests/test_savetool.py` (15 tests): normalize, validate and diff for `tools/savetool.py`, on synthetic save images.
- `tests/test_bootstrap.py` (4 tests): `tools/bootstrap.py` against a synthetic upstream repository.

SDK host suites (`make sdk-tests`, eleven suites). Each one compiles a patched SDK function with GCC for 32-bit hosts, links it into a test program, and runs it on synthetic inputs:

- `fx`: the eight matrix functions in `fx_mtx*.c`, against a 428-case reference (NitroSDK 0101).
- `small`: C versions of small asm functions: lock IDs, CLZ, GX FIFO NOP clear, `OS_Init`, `OS_TerminateProc`. Two more files compile only (NitroSDK 0201).
- `nns_clz`: the three count-leading-zeros helpers in NitroSystem's g2d code (NitroSystem 0301).
- `os_system`: the CPSR interrupt functions in `os_system.c` (NitroSDK 0401).
- `cp_context`: OS and CP context save and restore, and the divider and square-root hooks (NitroSDK 0501).
- `port_hooks`: IRQ request clearing, software DMA start hooks, and matrix result sync (NitroSDK 0601).
- `card_spi_wait`: the `CARD_SpiWait*` stub, status `0xAA` with no peer (NitroSDK 0701).
- `math_qsort`: the C version of `MATH_QSort` (NitroSDK 0801).
- `irq_handler`: the C version of `OS_IrqHandler` (NitroSDK 0901).
- `gcc_fixes`: the GCC-only header/source fixes from patch 1201: unnamed parameter, CRC headers, PXI tag.
- `memory_map`: the `SDK_PORT_3DS` memory map from patches 1301/1302: every `HW_*` area and register macro lands in the port's arrays at the DS offsets (35 checks).

The eleven suites all pass as of 2026-10-31 (`make test` exit 0). `tests/sdk/port_memory.c` is not a suite: it defines the port's eleven memory arrays for the suites that link SDK objects touching registers (patch 1301).

To run one suite on its own:

```
bash tests/sdk/fx/run.sh build/upstream
```

`nns_clz` and `small` take a patched tree path as their argument. Pass `build/upstream` there too.

`make port-tests` runs the vendored `3ds_port` suites (`pak`, `nitro`, `nitrosystem`) against HGSS's
SDK. It is its own target, not part of `make test`, because it needs a full `host32` build. Current
state (61 patches): **exit 0**; every suite passes, including `test_arm7` (676), `test_sound_sequence`
(12) and `nitrosystem` (17,122 checks). See [../progress/STATE.md](../progress/STATE.md).

## The port layer

This is the Phase 1 work in progress. The vendored `3ds_port/nitro` Makefiles expect Platinum's SDK layout; `tools/hgss_nitro.py` builds shims and passes make overrides so the vendored tree compiles HGSS's SDK sources. The full design is in [Build-Reference](Build-Reference.md#7-how-the-vendored-port-build-is-wired).

Fast measure (per-file syntax):

```
python3 tools/port_compile_survey.py
```

Real build (compiles and links the port's objects, then the suites):

```
python3 tools/hgss_nitro.py make --target host32 --out /work/out/<name> lib -k -j8
python3 tools/hgss_nitro.py make --target host32 --out /work/out/<name> test
```

- Always pass `--out`: objects then go to that private directory instead of the vendored `build/host32`, so two runs do not collide.
- `-k` keeps going after a failing file; the tail of the log is not the whole story, count `error:` lines.
- `--subproject {pak,nitro,nitrosystem,all}` narrows `test`; the default for `test` is all three.
- `python3 tools/hgss_nitro.py list` prints the HGSS source lists and the excluded files; `shim` recreates the shims and prints the overrides.

Current state (2026-10-31, 61 patches): `libnitro.a` and `libnitrosystem.a` build with 0 errors;
`make port-tests` exits 0 — pak 120, the whole nitro sequence (arm7 676, sound_sequence 12, render
2071, gx3d 326+228, vram 24, shadow 375, …) and nitrosystem 17,122. The failure history, including
every segfault and root cause, is in
[../progress/agent-notes/p1-runtime-bringup.md](../progress/agent-notes/p1-runtime-bringup.md) and
[../progress/agent-notes/p2-wave-a-session5.md](../progress/agent-notes/p2-wave-a-session5.md).

`hgss/compat/include/` is on the shim's include path and holds HGSS names for Platinum headers. Do not
edit the vendored files for HGSS-only names: add a header or a new source (`port_pm_hgss.c`, compat C
files), and keep edits to vendored files small enough to send upstream.

### Memory arrays and host tests (patches 1301/1302)

With `SDK_PORT_3DS`, `HW_IOREG`, `HW_MAIN_MEM` and the VRAM/palette/OAM views point at eleven arrays the port provides. A host test that links an SDK object touching a register or main memory must link `tests/sdk/port_memory.c` too, or the link fails with `undefined reference to 'NitroPort_IoReg'`. Some SDK headers spell a register as a literal DS address instead of `HW_REG_BASE + offset`; patch 1302 fixed the two MI DMA ones. If another turns up, add a guarded patch and a `memory_map` check, do not map `0x04000000` in the test.

## Layout probe

The probe checks that the save-chunk structs have the same `sizeof` and `offsetof` under GCC as under MWCC. A save written by one must load on the other.

```
make layout
```

What it needs:

- The decomp checkout at `/work/pokeheartgold`, with the generated `.naix` headers under its `files/` folder. Those come from building the decomp, which the matching build does.
- pyelftools. It is in the image.
- The committed MWCC reference, `tools/layout_probe/mwcc.txt`. The probe itself runs only GCC.

MWCC and Wine are needed only with `--update-mwcc`, which rewrites that reference. Do not run it casually.

What it proves: 804 values, covering 45 save-chunk root types and everything reachable from them, match MWCC for `gcc -m32`, `gcc -m32 -malign-double` and devkitARM. The last run reported 0 differences for all three. Without patch 1001 (`u64` and `s64` 4-byte aligned), 55 values differed. Exit 1 on any difference.

## The DS-side gate

Any change to a decomp or SDK patch must keep the DS build byte-identical. [docs/progress/phase-1.md](../progress/phase-1.md) calls this check "hard rule 3". It is also in the plan as ground rule 2.

Run the matching build from a clone of the patched tree at **`/work/pokeheartgold`**. The path is not cosmetic: `tools/msl-new-partition` (and its `msl-d1-partition` / `dwc-iterator-advance` siblings) pins the *complete producer object*, and MWCC embeds the absolute source directory (`Z:\work\pokeheartgold\src\…`) in a section payload. A build from any other path fails with `complete producer section/symbol/relocation/payload drift` even though the code is identical. Move the old reference tree aside, because the clone fails if the folder exists:

```
cd /workspaces/pokeheartgold-3ds
mv /work/pokeheartgold /work/pokeheartgold.ref   # keeps the old reference ELF; this build recreates the tree
git clone -q /workspaces/pokeheartgold-3ds/build/upstream /work/pokeheartgold
tools/install-ds-toolchain.sh /work/pokeheartgold
(cd /work/pokeheartgold && make -C tools/mwasmarm_patcher)          # (git-ignored binary; absent from a clone)
(cd /work/pokeheartgold && make patch_mwasmarm MWCCVER=2.0/sp2p3)
/workspaces/pokeheartgold-3ds/tools/matching-build.sh /work/pokeheartgold HEARTGOLD
(cd /work/pokeheartgold && echo "4fcded0e2713dc03929845de631d0932ea2b5a37  build/heartgold.us/pokeheartgold.us.nds" | sha1sum -c -)
```

Run these from inside the container. The repo tools are called by absolute path because the clone does not contain them. The build must finish, and the SHA1 check must print `OK`. Repeat with `SOULSILVER` and SHA1 `f8dc38ea20c17541a43b58c5e6d18c1732c7e582` (`build/soulsilver.us/pokeheartgold.us.nds`).

Five decomp build tools verify input hashes before they run: `tools/arm7-alarm-partition/build.py`
("physical input drift"), `tools/alarm-partition/build.py` ("production closure drift"),
`tools/msl-new-partition/build.py` ("header drift" / "unregistered source" / "complete producer
… drift"), and its `tools/msl-d1-partition` and `tools/dwc-iterator-advance` siblings. If the gate
stops with one of those, a patch changed an input they pin; update the pins in that `profile.json`
and rerun. They pin more than headers: the MSL tools also pin `producers.*.source_sha256`, and
`dwc-iterator-advance` pins the upstream source that the pinned commit does not ship (that tool is
not reached by this build). Patches `1601` and `1602` are the examples: 1601 re-pinned the two
alarm profiles, 1602 the three MSL/DWC profiles (types.h from patch 1001, `tree-iterator4.h` from the
upstream bump, and the two `to91_msl_*_new.cpp` sources from patch 1505).

Status: the last full pass used 10 patches and produced the retail SHA1. The stack has grown well
past that and the gate has **not passed** since; every pin class has now been found and fixed, and the
final run at the closing patch count is the proof. Run it after any patch change; it is the only
strong evidence that the DS build is unchanged.

The phase records describe this procedure, and a local script used it. That script is not tracked in the repo (see the pitfalls below).

## Authoring a patch

Patches for the decomp and the SDK are numbered `.patch` files. Bootstrap applies them in filename order, as commits, with `git am`.

Work in a clone of the patched tree, so the commits are real and easy to export:

```
git clone -q /workspaces/pokeheartgold-3ds/build/upstream /work/wt/<name>
cd /work/wt/<name>
```

The image has no git identity configured. Set one in the clone before you commit:

```
git config user.name "<your name>"
git config user.email "<your email>"
```

Then:

1. **Edit.** Keep the original MWCC text. Put the GCC version under `SDK_PORT_3DS`:

   ```c
   #ifdef SDK_PORT_3DS
   /* GCC version: the same behaviour as the MWCC asm below */
   #else
   /* original MWCC text, unchanged */
   #endif
   ```

2. **Prove the DS build does not change.** Without `-DSDK_PORT_3DS`, `gcc -E -P` of the patched file must equal `gcc -E -P` of the original. Phase 1 used this check for every asm-to-C hunk. There is no script for it, so run it by hand.

3. **Test it.** Add or extend a suite in `tests/sdk/`, using synthetic inputs. Run it on your clone. The patch must make the suite pass and must not weaken it.

4. **Commit** in the clone. The message follows [CONTRIBUTING.md](../../CONTRIBUTING.md): say which MWCC or DS behaviour the patch reproduces, and cite the source file and line.

5. **Export** the single commit, relative to the stack's folder:

   ```
   git format-patch -1 --relative=lib/NitroSDK --start-number N -o /workspaces/pokeheartgold-3ds/patches/NitroSDK
   ```

   For NitroSystem, use `--relative=lib/NitroSystem` and `patches/NitroSystem`. For a change to the decomp outside `lib/`, drop `--relative` and write to `patches/pokeheartgold`. Bootstrap applies each stack under the matching folder. `N` is the patch number. Use `101` for `0101`.

   **The `--relative` is not optional.** A NitroSDK patch exported without it contains `lib/NitroSDK/src/...`;
   bootstrap then applies it with `--directory=lib/NitroSDK` and `git am` fails with
   `lib/NitroSDK/lib/NitroSDK/src/...: does not exist in index`. If the gate then reports "physical
   input drift" / "production closure drift", the patch touched a header that
   `tools/arm7-alarm-partition` or `tools/alarm-partition` pins; update its `profile.json`
   (see patch `1601`).

6. **Rebuild** the tree in the repo: `python3 tools/bootstrap.py --clean`. Then `make test`.

7. **Run the DS-side gate** described in the section above.

### Numbering

Patch names look like `0101-fx-C-versions-of-the-matrix-asm-functions-SDK_PORT_3.patch`. The first two digits name the area. The last two count within the area. The current areas, from [docs/decisions.md](../decisions.md):

| Number | Area |
| --- | --- |
| 01 | fx matrix functions |
| 02 | small hardware and lock functions |
| 03 | one-off SDK fixes (NitroSystem uses 0301 for clz helpers) |
| 04 | OS system functions (CPSR) |
| 05 | CP and OS context |
| 06 | port hooks |
| 07 | card SPI wait stub |
| 08 | `MATH_QSort` |
| 09 | IRQ handler |
| 10 | pokeheartgold types (`pokeheartgold/1001`, `u64` alignment) |
| 11 | NitroSystem header fix (`pokeheartgold/1101`, return type) |
| 12 | GCC-only fixes (`pokeheartgold/1201`: unnamed parameter, CRC headers, `OS_IrqHandler`, PXI tag) |
| 13 | memory map (`pokeheartgold/1301` arrays, `1302` absolute DMA registers) |

Patches `1001`-`1302` live in `patches/pokeheartgold/` because they touch more than one tree (or the
decomp root). The numbers are not shared between stacks: `NitroSDK/0301` and `NitroSystem/0301` are
different patches.

## Pitfalls

These come from things the repo already recorded.

- **Dot-named files in `tools/` are not in the repo.** `.gitignore` excludes `tools/.*`. Scratch scripts such as the local gate script live there, and a fresh clone does not have them. Do not cite them in docs or scripts. Use the commands in this page.
- **Container paths and host paths differ.** The repo is at `/workspaces/pokeheartgold-3ds` in the container, and `/private`, `/work` and `build/` exist only there. On the Mac, `build/` is an empty directory on purpose, because the container mounts a volume over it. Run builds and tests through `devcontainer exec`.
- **`build/` is a named volume.** The volume `hgss-build` is mounted over the workspace's `build/`. Files you create there from the Mac are not seen in the container, and the reverse. Bind-mounted macOS builds are slow, which is why it is a volume ([docs/devcontainer.md](../devcontainer.md)).
- **`make clean` deletes everything under `build/`.** That includes `build/upstream/` and `build/reference/`. Recovering `build/upstream/` takes a network fetch, and recovering the reference artifacts takes a rebuild. Use `--clean` on bootstrap when you only need a fresh `build/upstream/`.
- **`/work` is a named volume too.** It holds the full decomp clone, the matching builds and the ccache. Removing the volume removes them.
- **`CC` is set to `gcc-10` in the image.** That is deliberate: the vendored port's host32 build is verified with gcc-10 (its make log shows gcc-10), while the SDK surveys and most suites use plain `gcc` (13). Leave `CC` alone for the port; pass `GCC=gcc-10` to a suite to reproduce the port's compiler. Record both survey numbers when you change SDK code.
- **Bootstrap does not see patch edits.** After you change a patch's contents, use `--clean`. Editing a patch without `--clean` leaves `build/upstream` with the old text, and the tests run against the old code.
- **Outputs that contain game code stay local.** The matching ROM, the decomp build and any `.3dsx` or `.cia` belong in `/work` or on your own machine, never in the repo, a CI artifact or a message.
- **Golden frames are git-ignored.** `tests/golden/` shows game art. Only the input scripts and the capture tools are committed. See [tools/golden/README.md](../../tools/golden/README.md).
- **The vendored `3ds_port/` tests are wired and green.** `make port-tests` is real and runs `pak`,
  `nitro` and `nitrosystem` against HGSS's SDK; at 61 patches it exits 0 (nitrosystem 17,122 checks).
  To debug a vendored test, see
  [Build-Reference](Build-Reference.md#a-vendored-test-segfaults-or-hangs-at-100--cpu); never edit the
  test to make it pass.
- **Do not run `make test` and a harness build at the same time.** Both use the shared
  `build/hgss-sdk`/`build/hgss-nns` symlinks. A test that called `make_shim()` with a fake root once
  re-pointed them at `/up`, and a concurrent harness failed in 763 files with NNS incomplete-type
  errors. The test is now isolated, but the shared state remains a hazard; serialise the two.
- **Do not run `bootstrap --clean` while anything reads `build/upstream`.** It deletes the tree; the
  symptom is a header that certainly exists reported missing mid-build.
- **The DS gate must run from `/work/pokeheartgold`.** The MSL/DWC partition tools pin the complete
  producer object and MWCC embeds the absolute source path; `/work/patched` can never match. Five
  tools pin inputs (`1601`/`1602`), and the MSL `*-shape.json` debug-info pins go stale when an
  `#ifdef SDK_PORT_3DS` block shifts source lines. See
  [Build-Reference](Build-Reference.md#the-ds-gate-stops-with-physical-input-drift-production-closure-drift-or-header-drift).
- **The game build is missing one decomp stage: `tools/object_aliases.py`.** The DS build adds labels
  inside BSS objects (the five `gPokewalkerIr*`) and Thumb entry points from `.alias-map.json` files.
  The harness does not run it yet; do not hand-write those symbols.
- **A link that prints `0 unresolved symbols` may have aborted early.** Read the whole `link.log`; an
  earlier `ld` error (e.g. a non-TLS `errno` reference clashing with glibc) stops symbol resolution.
- **`MSL_C` is not all replaced by newlib.** The float/64-bit runtime and `strnicmp`/`__lower_mapC`
  have no host equivalents; they need a compat implementation with the **bit-pattern** ABI the game
  declares, not a plain C float file.
- **A host test that links an SDK object needs the port's arrays.** Patch 1301 moved the registers into `NitroPort_IoReg` and friends; link `tests/sdk/port_memory.c` (the suites already do). If a register write faults at `0x04000000`, look for an absolute address in an SDK header (patch 1302 fixed two) instead of mapping the page in the test.
- **`onCreate.sh` runs once.** Its private-file check and `wineboot` run only when the container is created. Changing the Wine version in the Dockerfile needs a new container. The private folder is a live mount, so files you add to it later are visible without a rebuild. Only the one-time checks need the new container.
- **The matching build is not `make`.** Use `tools/matching-build.sh`, or the ROM will not match. See [Getting-Started](Getting-Started.md#the-matching-sha1-does-not-match).

## See also

- [Build-Reference](Build-Reference.md): the toolchain and every build target.
- [Tools-Reference](Tools-Reference.md): every script and test runner, with inputs and outputs.
- [Getting-Started](Getting-Started.md): the setup, step by step.
- [Home](Home.md): the wiki index.
- [docs/progress/STATE.md](../progress/STATE.md): what is green today and what is next.
- [CONTRIBUTING.md](../../CONTRIBUTING.md): the rules for commits and patches.
