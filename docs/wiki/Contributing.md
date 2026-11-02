# Contributing

This is the human version of [CONTRIBUTING.md](../../CONTRIBUTING.md). It adds the hard rules the plan sets, one concrete example of each from the repo, and a review checklist for a patch.

The examples come from the committed files: patches, docs, tools and the workflow files. This page did not read the git log, because the port repo must not be touched with git during this work. If you want to find the commit that added an example, `git log -- <path>` will show it.

Related pages: [Patch-Stack](Patch-Stack.md) (how patches are numbered and applied), [Save-Compatibility](Save-Compatibility.md), [Decisions-Explained](Decisions-Explained.md), [Glossary](Glossary.md). For setup and daily commands, see [Getting-Started](Getting-Started.md), [Daily-Workflow](Daily-Workflow.md) and [Tools-Reference](Tools-Reference.md).

## The one rule that cannot be broken

Do not commit game content or proprietary tools. That means no ROMs, no files taken from a ROM, no golden screenshots (they show game art), no save files, no MWCC or Nitro SDK files, and no built game binaries ([CONTRIBUTING.md](../../CONTRIBUTING.md)).

Check the list of changed files before every commit. If a `.nds`, `.sav`, `.narc`, `.exe`, `.dll` or a game image shows up, stop.

Example: the `.gitignore` excludes these before anyone can add them by accident. It lists `*.nds`, `*.sav`, `*.pak`, `*.cia`, `*.3dsx`, `mwccarm*/`, `roms/`, `saves/`, `private/` and `tests/golden/`. The golden captures are kept out of the repo, and `tests/inputs/` holds only the four input scripts, with no images. The layout probe's reference file, `tools/layout_probe/mwcc.txt`, holds numbers and struct labels, not game data ([tools/layout_probe/compare.py](../../tools/layout_probe/compare.py), docstring).

Another example is the workflow for the proprietary build. `matching.yml` runs only on a private self-hosted runner, and it has no upload step, on purpose ([.github/workflows/matching.yml](../../.github/workflows/matching.yml)).

## Never edit the decomp in place

The decomp is fetched into `build/upstream/` at the pinned commit. Changes go into patch files under `patches/`, and the decomp checkout itself is not edited ([CONTRIBUTING.md](../../CONTRIBUTING.md), "Changing the decompilation or the SDK").

Example: `tools/matching-build.sh` needs five sections that the dev container's linker strips. It adds them as `KEEP_SECTION` blocks to the generated `main.lcf`, which is a build artifact. The script's header says the decomp checkout is not edited, only its build artifact ([tools/matching-build.sh](../../tools/matching-build.sh), header).

Another example is patch `0301`, which cites the link map (`main.lsf:624`, `main.elf.xMAP:1247`) to show which of two `OSi_AlarmHandler` definitions the ROM uses, and why the other one is left alone. A reviewer can check that claim without trusting the author ([patches/NitroSDK/0301](../../patches/NitroSDK/) message).

## The DS build stays byte-identical

Every game change is guarded by `SDK_PORT_3DS`, or it is a GCC-only fix that MWCC never compiles. The DS build must still produce the retail ROM byte for byte ([CONTRIBUTING.md](../../CONTRIBUTING.md); [docs/BASE.md](../BASE.md)).

Example: patch `1101` changes one header prototype, and its message says that `gcc -E -P` of the header is identical without the macro. Patch `0401` is the one patch that edits an existing DS line, from `#ifdef SDK_ARM9` to `#elif defined(SDK_ARM9)`. It keeps the same meaning without the macro, and its message says the preprocessed output is identical for both `SDK_ARM9` and `SDK_ARM7` ([patches/NitroSDK/0401](../../patches/NitroSDK/) message).

The byte-identical gate is a full rebuild and a SHA1 check. The gate last ran at **78 patches** and passed for both games (HG `4fcded0e…b5a37`, SS `f8dc38ea…e582`); patches `1407` and `1904`, added since, are `SDK_PORT_3DS`-only. A patch that changes DS text needs that run before merge.

## The save format never changes

The save is a 524,288-byte image with the same offsets, layouts and checksums ([gameplan.md](../../gameplan.md), ground rule 3).

Example: patch `1001` makes `u64` 4-byte aligned, but only under `SDK_PORT_3DS`. The layout probe found 55 values that differed from MWCC, and the fix is in the port's build only. The save code is not edited. Its message gives the numbers and the structs they come from ([patches/pokeheartgold/1001](../../patches/pokeheartgold/) message; [Save-Compatibility](Save-Compatibility.md)).

A second example is the decision that `savetool` discovers slot sizes from the footers instead of hard-coding them. The row says the sizes depend on compiled struct sizes, so a hard-coded value would be a guess ([docs/decisions.md](../decisions.md), 2026-10-30).

## The vendored `3ds_port/` stays byte-identical, with one recorded exception

`3ds_port/` is copied from Platinum at the commit in `platinum.lock`, and it stays identical to that commit. HGSS-only code goes in new files, or in patches meant for upstream ([CONTRIBUTING.md](../../CONTRIBUTING.md); [NOTICE.md](../../NOTICE.md)).

**Exception (2026-11-01):** the camera-plane fix is a deliberate upstream-style change in `3ds_port/nitro/src/port_gx3d.c` `stage_triangle()`, plus `test_camera_plane_triangle` in its vendored `test_gx3d.c`. An exact `(0,0,0,0)` vertex is on the camera plane; both edges to a rim vertex project to that rim point, so the triangle has zero screen area and the DS stores no polygon. Dropping exactly those triangles keeps Ho-oh intact (the earlier `w`/broad-drop attempts deformed the bird). `platinum.lock` is unchanged; the divergence and the upstream PR content are recorded in [docs/decisions.md](../decisions.md) and [agent-notes/s13-nns-volume-table.md](../progress/agent-notes/s13-nns-volume-table.md).

Example: the wiring between the vendored port and HGSS's SDK is not done by editing the vendored Makefiles. `tools/hgss_nitro.py` builds a shim under `build/` and passes command-line overrides, which keeps the vendored Makefiles unchanged ([docs/port-wiring.md](../port-wiring.md), [Build-Reference](Build-Reference.md#7-how-the-vendored-port-build-is-wired)).

Another example is the rule for tests: never change a test just to make it pass ([gameplan.md](../../gameplan.md), Phase 1, step 8). The phase-1 record gives one case. The `cp_context` test expected GCC's 8-byte-aligned struct sizes. It was corrected to the DS layout that the asm implies, an overrun guard was added, and the record says this is not a weakened assertion ([docs/progress/phase-1.md](../progress/phase-1.md), "Deviations").

## Never run a mutating git command in a shared worktree

A `git checkout -- <file>` discards uncommitted work in a tree someone else
may be editing. During this session one agent ran it on
`hgss/game/app/main_pc.c` while another was working; the file happened to carry
only an abandoned `--render-3ds` experiment, so nothing was lost, but the
outcome was luck. Inspect `git status`, name the files in `git add`, and never
use `git checkout`, `git reset` or `git stash` in a shared tree without the
owner's explicit instruction. A subagent is not exempt: "read-only" means no
mutating git commands either.

## Small commits, with the trailer

Each commit is one logical change. Each patch in `patches/` is one area, and it says what it touches in its first lines ([CONTRIBUTING.md](../../CONTRIBUTING.md), "How changes get in").

Example: patch `0201` covers small hardware and lock functions, and its message says "The patch only adds lines". A reviewer can check that from the diff stat alone, and it has zero removed lines ([patches/NitroSDK/0201](../../patches/NitroSDK/) message).

Commits written with AI help end with a `Co-Authored-By` line, as [CONTRIBUTING.md](../../CONTRIBUTING.md) specifies. The trailer in the existing patch messages names a model that differs from the form CONTRIBUTING.md gives. The phase-1 record says the trailers were normalized before commit ([docs/progress/phase-1.md](../progress/phase-1.md), "Deviations"), but the patch files in the tree still carry the other form. New patches should follow CONTRIBUTING.md.

The branch and pull-request flow in CONTRIBUTING.md is branch per change, a pull request into `main`, and CI green before merge. The repo has no git remote yet ([docs/progress/phase-0.md](../progress/phase-0.md)), so this is the intended process.

## Cite sources for hardware claims

A claim about how the DS or the SDK behaves must cite what it comes from. The sources the project uses are the decomp's `lib/` or `sub/` code, the Platinum port's header comments, and the GBATEK reference ([AI_DISCLOSURE.md](../../AI_DISCLOSURE.md)). The plan also asks that every SDK hunk be read in its surrounding code, not only in the diff ([gameplan.md](../../gameplan.md), Phase 1 pitfalls).

Example: patch `0701` cites `pokewalker_ir.c:66`, `:109`, `:164`, `:214` to `:216` and `:1099` to `:1102`, and `fieldmap.c:775` to `:776`, to show that each caller is safe with the stub. A reviewer can open each line ([patches/NitroSDK/0701](../../patches/NitroSDK/) message).

Another example is `docs/sdk-diff.md`. It marks each section either as checked, with the file cited, or as not yet done, so that a reader does not rely on a claim that was never checked ([docs/sdk-diff.md](../sdk-diff.md), header and sections).

## Code style

- **C.** Match the surrounding code. Comments explain why, not what ([CONTRIBUTING.md](../../CONTRIBUTING.md), "Code style").
- **Python.** Python 3.10 or later, standard library only, unless there is a good reason. The savetool and bootstrap tools use only the standard library. The layout probe uses `pyelftools` and the qsort oracle uses `unicorn`, and their docstrings say so ([tools/layout_probe/run_probe.py](../../tools/layout_probe/run_probe.py); [tools/qsort_rom_oracle.py](../../tools/qsort_rom_oracle.py)).

## Local checks

These are the commands the repo names. Check each one's docstring for its inputs.

| Command | What it runs | Needs |
| --- | --- | --- |
| `python3 tools/bootstrap.py` | Fetches the pinned decomp and applies the patches | git, network once |
| `make test` | Tool tests (`tests/test_*.py`) and the SDK host suites in `tests/sdk/` | `build/upstream` from bootstrap |
| `make sdk-survey` | Counts SDK files that parse under GCC | `build/upstream` |
| `make layout` | Compares GCC's struct layouts with the committed MWCC reference | `build/upstream`, pyelftools, the decomp's generated headers |
| `make port-tests` | Not implemented. Fails on purpose | Nothing |

Source: [Makefile](../../Makefile) header and targets.

## Review checklist for a patch

Use this for any patch in `patches/`. Apply the port and tool parts to changes outside the stack.

**Scope and message**

- [ ] One area, one logical change, with a message that says what it does and why.
- [ ] The message names the MWCC or DS behaviour the patch reproduces, or the GCC problem it fixes.
- [ ] The message cites the source file and line of the HGSS code it depends on, and the Platinum counterpart if one exists.

**The guard and the DS build**

- [ ] Every added line is inside `SDK_PORT_3DS`. A changed DS line is an explicit exception, and the message explains why it keeps its meaning.
- [ ] The `gcc -E -P` identity check is stated, and the reviewer can rerun it.
- [ ] The matching ROM gate has run on the patched tree, or the reason it has not is stated.
- [ ] No line of MWCC-only code is removed, unless the guard covers it.

**Behaviour**

- [ ] The HGSS code at the pin was read in context, not only the diff.
- [ ] A test exists in `tests/sdk/` or the equivalent place, and it runs. For subtle behaviour, the message shows a mutation check that the test catches.
- [ ] No test was weakened to pass.
- [ ] Every new warning in game code was read. The plan asks for `-Wall -Wuninitialized -Wmaybe-uninitialized`.

**Layout and saves**

- [ ] If a struct reachable from the save changes, `make layout` runs and shows 0 differences.
- [ ] No save format change. The save image size, offsets and checksums are as before.

**Stack and pins**

- [ ] `tools/bootstrap.py --clean` applies the patch cleanly, with its number in its stack.
- [ ] `upstream.lock` and `platinum.lock` are unchanged, unless the change is a pin bump, which is its own task.
- [ ] `3ds_port/` is unchanged, or the change is a recorded upstream-style exception with its PR content and tests (the camera-plane drop is the current one).
- [ ] After adding/editing a patch, `bootstrap.py --clean` is re-run and the applied count matches `patches/` before any build is trusted.
- [ ] After a decomp/SDK patch, the DS gate at the new patch count passes for **both** HG and SS.

**Content and trailer**

- [ ] The file list has no `.nds`, `.sav`, `.pak`, `.3dsx`, `.cia`, golden image, MWCC or SDK file.
- [ ] The commit ends with the `Co-Authored-By` line, if AI help was used.

**Docs**

- [ ] Statuses are honest: done, partial, or not started. A claim without a file to cite is marked as not verified.
