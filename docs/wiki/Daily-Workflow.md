# Daily workflow

How to work on the port once [Getting-Started](Getting-Started.md) is green. The short version:

1. Make sure `build/upstream` matches the pin and the patches (`bootstrap.py`).
2. Change code: a patch in `patches/`, or a file in `3ds_port/`, `tools/` or `tests/`.
3. Run `make test`. Run the survey if you touched SDK code.
4. If you touched a decomp or SDK patch, run the DS-side gate before it goes in.

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

- `make sdk-survey` runs `tools/sdk_compile_survey.py` over `lib/NitroSDK` and `lib/NitroSystem`. Last run: 270 of 284 files parse, 14 fail. The Makefile target exits 1 while any file fails, so use it as a count, not a gate. For the first errors, run `python3 tools/sdk_compile_survey.py --m32 -j 10 -v`. Add `--json /work/survey.json` to keep the per-file results outside the repo.
- `tools/port_compile_survey.py` checks the vendored `3ds_port/nitro/src` against the HGSS headers. Last run: 26 of 46 compile. The causes are in [docs/port-wiring.md](../port-wiring.md).

## Host test suites

`make test` is the gate, and CI runs it. It runs two groups:

Python tests (`make tool-tests`, 19 tests):

- `tests/test_savetool.py` (15 tests): normalize, validate and diff for `tools/savetool.py`, on synthetic save images.
- `tests/test_bootstrap.py` (4 tests): `tools/bootstrap.py` against a synthetic upstream repository.

SDK host suites (`make sdk-tests`, nine suites). Each one compiles a patched SDK function with GCC for 32-bit hosts, links it into a test program, and runs it on synthetic inputs:

- `fx`: the eight matrix functions in `fx_mtx*.c`, against a 428-case reference (NitroSDK 0101).
- `small`: C versions of small asm functions: lock IDs, CLZ, GX FIFO NOP clear, `OS_Init`, `OS_TerminateProc`. Two more files compile only (NitroSDK 0201).
- `nns_clz`: the three count-leading-zeros helpers in NitroSystem's g2d code (NitroSystem 0301).
- `os_system`: the CPSR interrupt functions in `os_system.c` (NitroSDK 0401).
- `cp_context`: OS and CP context save and restore, and the divider and square-root hooks (NitroSDK 0501).
- `port_hooks`: IRQ request clearing, software DMA start hooks, and matrix result sync (NitroSDK 0601).
- `card_spi_wait`: the `CARD_SpiWait*` stub, status `0xAA` with no peer (NitroSDK 0701).
- `math_qsort`: the C version of `MATH_QSort` (NitroSDK 0801).
- `irq_handler`: the C version of `OS_IrqHandler` (NitroSDK 0901).

To run one suite on its own:

```
bash tests/sdk/fx/run.sh build/upstream
```

`nns_clz` and `small` take a patched tree path as their argument. Pass `build/upstream` there too.

`make port-tests` is not part of `make test`. It always fails, on purpose, until the vendored port's tests are wired to HGSS. See [docs/port-wiring.md](../port-wiring.md).

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

Run the matching build on a copy of the patched tree, not on `/work/pokeheartgold`. Remove an old `/work/patched` first, because the clone fails if the folder exists:

```
cd /workspaces/pokeheartgold-3ds
rm -rf /work/patched
git clone -q /workspaces/pokeheartgold-3ds/build/upstream /work/patched
tools/install-ds-toolchain.sh /work/patched
(cd /work/patched && make patch_mwasmarm MWCCVER=2.0/sp2p3)
/workspaces/pokeheartgold-3ds/tools/matching-build.sh /work/patched HEARTGOLD
(cd /work/patched && echo "4fcded0e2713dc03929845de631d0932ea2b5a37  build/heartgold.us/pokeheartgold.us.nds" | sha1sum -c -)
```

Run these from inside the container. The repo tools are called by absolute path because the clone does not contain them. The build must finish, and the SHA1 check must print `OK`. The last full run, with patches up to 0901, passed. Patch 1001 has not had a full rebuild yet ([docs/progress/phase-1.md](../progress/phase-1.md)), so run the gate again once after you change it.

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
| 10 | pokeheartgold types (`pokeheartgold/1001`) |

Each area has one patch so far. The next patch in an area would be `0102`, `0202` and so on. That follows the existing names, but no tool enforces it.

## Pitfalls

These come from things the repo already recorded.

- **Dot-named files in `tools/` are not in the repo.** `.gitignore` excludes `tools/.*`. Scratch scripts such as the local gate script live there, and a fresh clone does not have them. Do not cite them in docs or scripts. Use the commands in this page.
- **Container paths and host paths differ.** The repo is at `/workspaces/pokeheartgold-3ds` in the container, and `/private`, `/work` and `build/` exist only there. On the Mac, `build/` is an empty directory on purpose, because the container mounts a volume over it. Run builds and tests through `devcontainer exec`.
- **`build/` is a named volume.** The volume `hgss-build` is mounted over the workspace's `build/`. Files you create there from the Mac are not seen in the container, and the reverse. Bind-mounted macOS builds are slow, which is why it is a volume ([docs/devcontainer.md](../devcontainer.md)).
- **`make clean` deletes everything under `build/`.** That includes `build/upstream/` and `build/reference/`. Recovering `build/upstream/` takes a network fetch, and recovering the reference artifacts takes a rebuild. Use `--clean` on bootstrap when you only need a fresh `build/upstream/`.
- **`/work` is a named volume too.** It holds the full decomp clone, the matching builds and the ccache. Removing the volume removes them.
- **`CC` is set to `gcc-10` in the image.** The vendored port's host build uses `CC ?= cc`, so it picks up `gcc-10`. [docs/decisions.md](../decisions.md) records that `gcc-10` fails on unnamed parameters in the Platinum code. When you build the vendored port, run `env -u CC`. The SDK suites are not affected. `nns_clz` and `small` read `CC` but still pass.
- **Bootstrap does not see patch edits.** After you change a patch's contents, use `--clean`. Editing a patch without `--clean` leaves `build/upstream` with the old text, and the tests run against the old code.
- **Outputs that contain game code stay local.** The matching ROM, the decomp build and any `.3dsx` or `.cia` belong in `/work` or on your own machine, never in the repo, a CI artifact or a message.
- **Golden frames are git-ignored.** `tests/golden/` shows game art. Only the input scripts and the capture tools are committed. See [tools/golden/README.md](../../tools/golden/README.md).
- **The vendored `3ds_port/` tests do not run for HGSS.** `make -C 3ds_port/nitro` builds against Platinum's layout. It is not a gate yet.
- **`onCreate.sh` runs once.** Its private-file check and `wineboot` run only when the container is created. Changing the Wine version in the Dockerfile needs a new container. The private folder is a live mount, so files you add to it later are visible without a rebuild. Only the one-time checks need the new container.
- **The matching build is not `make`.** Use `tools/matching-build.sh`, or the ROM will not match. See [Getting-Started](Getting-Started.md#the-matching-sha1-does-not-match).

## See also

- [Tools-Reference](Tools-Reference.md): every script and test runner, with inputs and outputs.
- [Getting-Started](Getting-Started.md): the setup, step by step.
- [Home](Home.md): the wiki index.
- [CONTRIBUTING.md](../../CONTRIBUTING.md): the rules for commits and patches.
